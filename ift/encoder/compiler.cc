#include "ift/encoder/compiler.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/binary_diff.h"
#include "common/compat_id.h"
#include "common/font_data.h"
#include "common/font_helper.h"
#include "common/int_set.h"
#include "common/try.h"
#include "common/woff2.h"
#include "hb-subset.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"
#include "ift/feature_registry/feature_registry.h"
#include "ift/glyph_keyed_diff.h"
#include "ift/proto/ift_table.h"
#include "ift/proto/patch_encoding.h"
#include "ift/proto/patch_map.h"
#include "ift/url_template.h"

using absl::btree_set;
using absl::flat_hash_map;
using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using absl::string_view;
using common::BinaryDiff;
using common::CompatId;
using common::FontData;
using common::FontHelper;
using common::hb_blob_unique_ptr;
using common::hb_face_unique_ptr;
using common::IntSet;
using common::make_hb_blob;
using common::make_hb_face;
using common::make_hb_set;
using common::SegmentSet;
using common::Woff2;
using ift::GlyphKeyedDiff;
using ift::feature_registry::DefaultFeatureTags;
using ift::proto::GLYPH_KEYED;
using ift::proto::IFTTable;
using ift::proto::PatchEncoding;
using ift::proto::PatchMap;
using ift::proto::TABLE_KEYED_FULL;
using ift::proto::TABLE_KEYED_PARTIAL;

namespace ift::encoder {

// Configures a subset definition to contain all of the default, always included
// items (eg. https://w3c.github.io/IFT/Overview.html#feature-tag-list)
void Compiler::AddInitSubsetDefaults(SubsetDefinition& subset_definition) {
  std::copy(DefaultFeatureTags().begin(), DefaultFeatureTags().end(),
            std::inserter(subset_definition.feature_tags,
                          subset_definition.feature_tags.begin()));
}

static void AddCombinations(const std::vector<const SubsetDefinition*>& in,
                            uint32_t choose, std::vector<Compiler::Edge>& out) {
  if (!choose || in.size() < choose) {
    return;
  }

  if (choose == 1) {
    for (auto item : in) {
      out.push_back(Compiler::Edge{*item});
    }
    return;
  }

  for (auto it = in.begin(); it != in.end(); it++) {
    auto it_inner = it + 1;
    std::vector<const SubsetDefinition*> remaining;
    std::copy(it_inner, in.end(), std::back_inserter(remaining));

    std::vector<Compiler::Edge> combinations;
    AddCombinations(remaining, choose - 1, combinations);
    for (auto& edge : combinations) {
      edge.Add(**it);
      out.push_back(std::move(edge));
    }
  }
}

StatusOr<FontData> Compiler::FullyExpandedSubset(
    const ProcessingContext& context) const {
  SubsetDefinition all;
  all.Union(context.init_subset_);

  for (const auto& s : extension_subsets_) {
    all.Union(s);
  }

  for (const auto& [id, gids] : glyph_data_patches_) {
    all.gids.insert(gids.begin(), gids.end());
  }

  // Union doesn't work completely correctly with respect to design spaces so
  // clear out design space which will just include the full original design
  // space.
  // TODO(garretrieger): once union works correctly remove this.
  all.design_space.clear();

  return CutSubset(context, face_.get(), all, false);
}

std::vector<Compiler::Edge> Compiler::OutgoingEdges(
    const SubsetDefinition& node_subset, uint32_t choose) const {
  std::vector<SubsetDefinition> remaining_subsets;
  for (const auto& s : extension_subsets_) {
    SubsetDefinition filtered = s;
    filtered.Subtract(node_subset);
    if (filtered.Empty()) {
      continue;
    }

    remaining_subsets.push_back(std::move(filtered));
  }

  std::vector<const SubsetDefinition*> input;
  for (const auto& s : remaining_subsets) {
    input.push_back(&s);
  }

  std::vector<Edge> result;
  for (uint32_t i = 1; i <= choose; i++) {
    AddCombinations(input, i, result);
  }

  return result;
}

SubsetDefinition Compiler::Combine(const SubsetDefinition& s1,
                                   const SubsetDefinition& s2) const {
  SubsetDefinition result;
  result.Union(s1);
  result.Union(s2);
  return result;
}

Status Compiler::AddGlyphDataPatch(uint32_t id, const IntSet& gids) {
  if (!face_) {
    return absl::FailedPreconditionError("Encoder must have a face set.");
  }

  if (glyph_data_patches_.contains(id)) {
    return absl::FailedPreconditionError(
        StrCat("A segment with id, ", id, ", has already been supplied."));
  }

  uint32_t glyph_count = hb_face_get_glyph_count(face_.get());

  for (uint32_t gid : gids) {
    if (gid >= glyph_count) {
      return absl::InvalidArgumentError(
          StrCat("Patch has gid, ", gid, ", which is not in the font."));
    }
  }

  glyph_data_patches_[id] = gids;
  next_id_ = std::max(next_id_, id + 1);
  return absl::OkStatus();
}

Status Compiler::AddGlyphDataPatchCondition(PatchMap::Entry condition) {
  if (condition.encoding != PatchEncoding::GLYPH_KEYED) {
    return absl::InvalidArgumentError(
        "Glyph data patch condition must be glyph keyed.");
  }

  uint32_t activated_patch_id = 0;
  if (condition.patch_indices.size() == 1) {
    activated_patch_id = condition.patch_indices[0];
  } else {
    return absl::InvalidArgumentError(
        "Glyph data patches must have exactly one associated patch id.");
  }

  uint32_t new_index = glyph_patch_conditions_.size();
  for (uint32_t child_index : condition.coverage.child_indices) {
    if (child_index >= new_index) {
      return absl::InvalidArgumentError(
          StrCat("Child conditions must only references previous conditions: ",
                 child_index, " >= ", new_index));
    }
  }

  if (!condition.ignored && !glyph_data_patches_.contains(activated_patch_id)) {
    // All entries have an associated patch ids, but on ignored entries the id
    // isn't used so only check for a associated patch on non-ignored entries.
    return absl::InvalidArgumentError(
        StrCat("Glyh data patch ", activated_patch_id,
               " has not been supplied via AddGlyphDataPatch()"));
  }

  glyph_patch_conditions_.push_back(condition);
  return absl::OkStatus();
}

void Compiler::AddFeatureGroupSegment(const btree_set<hb_tag_t>& feature_tags) {
  SubsetDefinition def;
  def.feature_tags = feature_tags;
  extension_subsets_.push_back(def);
}

void Compiler::AddDesignSpaceSegment(const design_space_t& space) {
  SubsetDefinition def;
  def.design_space = space;
  extension_subsets_.push_back(def);
}

StatusOr<Compiler::Encoding> Compiler::Compile() const {
  // See ../../docs/experimental/compiler.md for a detailed discussion of
  // how this implementation works.
  if (!face_) {
    return absl::FailedPreconditionError("Encoder must have a face set.");
  }

  ProcessingContext context(next_id_);
  context.init_subset_ = init_subset_;
  AddInitSubsetDefaults(context.init_subset_);
  if (IsMixedMode()) {
    // Glyph keyed patches can't change the glyph count in the font (and hence
    // loca len) so always include the last gid in the init subset to force the
    // loca table to remain at the full length from the start.
    //
    // TODO(garretrieger): this unnecessarily includes the last gid in the
    // subset,
    //                     should update the subsetter to retain the glyph count
    //                     but not actually keep the last gid.
    //
    // TODO(garretrieger): instead of forcing max glyph count here we can
    // utilize
    //                     table keyed patches to change loca len/glyph count to
    //                     the max for any currently reachable segments. This
    //                     would improve efficiency slightly by avoid including
    //                     extra space in the initial font. However, it would
    //                     require us to examine conditions against each subset
    //                     to determine patch reachability.
    //
    // TODO(garretrieger): in the mean time we can use the max glyph id from
    // fully
    //                     expanded subset instead. this will at least prune
    //                     glyphs not used at any extension level.
    uint32_t gid_count = hb_face_get_glyph_count(face_.get());
    if (gid_count > 0) context.init_subset_.gids.insert(gid_count - 1);
  }

  // TODO(garretrieger): when generating the fully expanded subset don't use
  // retain
  //                     gids. Save the resulting glyph mapping and use it to
  //                     translate encoder config gids into the space used by
  //                     fully expanded subset. This will optimize for cases
  //                     that don't include the entire original font.
  context.force_long_loca_and_gvar_ = false;
  auto expanded = FullyExpandedSubset(context);
  if (!expanded.ok()) {
    return expanded.status();
  }

  context.fully_expanded_subset_.shallow_copy(*expanded);
  auto expanded_face = expanded->face();

  // TODO(garretrieger): we don't need to force long gvar anymore. The client is
  // now capable of
  //                     upgrading the offset size as needed. Forcing long loca
  //                     is still needed though.
  context.force_long_loca_and_gvar_ =
      FontHelper::HasLongLoca(expanded_face.get()) ||
      FontHelper::HasWideGvar(expanded_face.get());

  auto init_font = Compile(context, context.init_subset_, true);
  if (!init_font.ok()) {
    return init_font.status();
  }

  Encoding result;

  if (woff2_encode_) {
    // Glyph transforms in woff2 encoding aren't safe if we are patching glyf
    // with a table keyed patch otherwise they are safe to use. See:
    // https://w3c.github.io/IFT/Overview.html#ift-and-compression
    hb_face_unique_ptr face = init_font->face();
    auto tags = FontHelper::GetTags(face.get());
    bool has_glyf =
        tags.contains(FontHelper::kGlyf) || tags.contains(FontHelper::kLoca);
    result.init_font = TRY(common::Woff2::EncodeWoff2(
        init_font->str(), IsMixedMode() || !has_glyf));
  } else {
    result.init_font.shallow_copy(*init_font);
  }
  result.patches = std::move(context.patches_);
  return result;
}

bool Compiler::AllocatePatchSet(ProcessingContext& context,
                                const design_space_t& design_space,
                                std::vector<uint8_t>& url_template,
                                CompatId& compat_id) const {
  auto uri_it = context.patch_set_url_templates_.find(design_space);
  auto compat_id_it = context.glyph_keyed_compat_ids_.find(design_space);

  bool has_uri = (uri_it != context.patch_set_url_templates_.end());
  bool has_compat_id = (compat_id_it != context.glyph_keyed_compat_ids_.end());

  if (has_uri && has_compat_id) {
    // already created, return existing.
    url_template = uri_it->second;
    compat_id = compat_id_it->second;
    return false;
  }

  url_template = UrlTemplate(context.next_patch_set_id_++);
  compat_id = context.GenerateCompatId();

  context.patch_set_url_templates_[design_space] = url_template;
  context.glyph_keyed_compat_ids_[design_space] = compat_id;
  return true;
}

Status Compiler::EnsureGlyphKeyedPatchesPopulated(
    ProcessingContext& context, const design_space_t& design_space,
    std::vector<uint8_t>& url_template, CompatId& compat_id) const {
  if (glyph_data_patches_.empty()) {
    return absl::OkStatus();
  }

  IntSet reachable_segments;
  for (const auto& condition : glyph_patch_conditions_) {
    if (!condition.ignored && condition.patch_indices.size() > 0) {
      reachable_segments.insert(condition.patch_indices[0]);
    }
  }

  if (!AllocatePatchSet(context, design_space, url_template, compat_id)) {
    // Patches have already been populated for this design space.
    return absl::OkStatus();
  }

  auto full_face = context.fully_expanded_subset_.face();
  FontData instance;
  instance.set(full_face.get());

  if (!design_space.empty()) {
    // If a design space is provided, apply it.
    auto result = Instance(context, full_face.get(), design_space);
    if (!result.ok()) {
      return result.status();
    }
    instance.shallow_copy(*result);
  }

  GlyphKeyedDiff differ(instance, compat_id,
                        {FontHelper::kGlyf, FontHelper::kGvar, FontHelper::kCFF,
                         FontHelper::kCFF2});

  for (uint32_t index : reachable_segments) {
    auto e = glyph_data_patches_.find(index);
    if (e == glyph_data_patches_.end()) {
      return absl::InvalidArgumentError(
          StrCat("Glyph data segment ", index, " was not provided."));
    }

    std::string url = TRY(URLTemplate::PatchToUrl(url_template, index));

    const auto& gids = e->second;
    auto patch = differ.CreatePatch(gids);
    if (!patch.ok()) {
      return patch.status();
    }

    context.patches_[url].shallow_copy(*patch);
  }

  return absl::OkStatus();
}

Status Compiler::PopulateGlyphKeyedPatchMap(PatchMap& patch_map) const {
  if (glyph_data_patches_.empty()) {
    return absl::OkStatus();
  }

  for (const auto& condition : glyph_patch_conditions_) {
    TRYV(patch_map.AddEntry(condition));
  }

  return absl::OkStatus();
}

// Converts outgoing edges for a given node into a list of activation conditions
// and associated segments.
std::vector<ActivationCondition> Compiler::EdgesToActivationConditions(
    ProcessingContext& context, const SubsetDefinition& node_subset,
    absl::Span<const Compiler::Edge> edges, PatchEncoding encoding,
    flat_hash_map<segment_index_t, SubsetDefinition>& segments) const {
  flat_hash_map<SubsetDefinition, segment_index_t> subset_def_to_segment_index;

  std::vector<ActivationCondition> result;
  segment_index_t next_segment_index = 0;
  for (const auto& e : edges) {
    SegmentSet segment_ids;
    for (const auto& s : e.Subsets()) {
      auto [it, did_insert] =
          subset_def_to_segment_index.insert(std::pair(s, next_segment_index));
      if (did_insert) {
        segments[next_segment_index++] = s;
      }
      segment_ids.insert(it->second);
    }

    std::vector<patch_id_t> edge_patches;
    for (Compiler::Jump& j : e.Jumps(node_subset, this->use_prefetch_lists_)) {
      auto [it, did_insert] = context.table_keyed_patch_id_map_.insert(
          std::pair(std::move(j), context.next_id_));
      if (did_insert) {
        context.next_id_++;
      }
      edge_patches.push_back(it->second);
    }

    // Conjunctive matching is used for composite conditions. In the context of
    // table keyed patch maps composite entries are used to add multiple
    // segments in a single patch. There will always be other entries for the
    // individual segments. As a result a composite entry should only be matched
    // and loaded on the client when each component segment is matched, thus
    // conjunctive matching is used.
    //
    // If disjunctive matching was used it would be possible for a composite
    // entry to be selected by the client when only one of the component
    // segments was present, which is wasteful. It would have been better to
    // select the entry with only the single matched segment.
    auto condition =
        ActivationCondition::and_segments(segment_ids, edge_patches.front());

    PatchEncoding edge_encoding = encoding;
    if (edge_encoding == TABLE_KEYED_PARTIAL &&
        e.ChangesDesignSpace(node_subset)) {
      // This edge will result in a change to design space which requires the
      // glyph keyed patch mapping to be updated with a new compat id, which
      // means this patch will need to be fully invalidating.
      edge_encoding = TABLE_KEYED_FULL;
    }
    condition.SetEncoding(edge_encoding);
    condition.AddPrefetches(
        absl::Span<const patch_id_t>(edge_patches).subspan(1));

    result.push_back(condition);
  }

  return result;
}

Status Compiler::PopulateTableKeyedPatchMap(
    ProcessingContext& context, const SubsetDefinition& node_subset,
    const std::vector<Compiler::Edge>& edges, PatchEncoding encoding,
    PatchMap& table_keyed_patch_map) const {
  // To create the table keyed patch mappings we use the activation condition
  // compiler. The outgoing edges for this node are converted into an activation
  // condition list and then compiled into mapping entries.
  flat_hash_map<segment_index_t, SubsetDefinition> segments;
  auto conditions = EdgesToActivationConditions(context, node_subset, edges,
                                                encoding, segments);
  auto entries = TRY(ActivationCondition::ActivationConditionsToPatchMapEntries(
      conditions, segments));
  for (auto e : entries) {
    TRYV(table_keyed_patch_map.AddEntry(e));
  }
  return absl::OkStatus();
}

StatusOr<FontData> Compiler::Compile(ProcessingContext& context,
                                     const SubsetDefinition& node_subset,
                                     bool is_root) const {
  // See ../../docs/experimental/compiler.md for a detailed discussion of
  // how this implementation works.
  auto it = context.built_subsets_.find(node_subset);
  if (it != context.built_subsets_.end()) {
    FontData copy;
    copy.shallow_copy(it->second);
    return copy;
  }

  std::vector<uint8_t> table_keyed_url_template = UrlTemplate(0);
  CompatId table_keyed_compat_id = context.GenerateCompatId();
  std::vector<uint8_t> glyph_keyed_url_template;
  CompatId glyph_keyed_compat_id;
  TRYV(EnsureGlyphKeyedPatchesPopulated(context, node_subset.design_space,
                                        glyph_keyed_url_template,
                                        glyph_keyed_compat_id));

  std::vector<Edge> edges = OutgoingEdges(node_subset, jump_ahead_);

  // The first subset forms the base file, the remaining subsets are made
  // reachable via patches.
  auto full_face = context.fully_expanded_subset_.face();
  auto node_data =
      TRY(CutSubset(context, full_face.get(), node_subset, IsMixedMode()));

  if (edges.empty() && !IsMixedMode()) {
    // This is a leaf node, a IFT table isn't needed.
    context.built_subsets_[node_subset].shallow_copy(node_data);
    return node_data;
  }

  IFTTable table_keyed;
  IFTTable glyph_keyed;
  table_keyed.SetId(table_keyed_compat_id);
  table_keyed.SetUrlTemplate(table_keyed_url_template);
  glyph_keyed.SetId(glyph_keyed_compat_id);
  glyph_keyed.SetUrlTemplate(glyph_keyed_url_template);

  PatchMap& glyph_keyed_patch_map = glyph_keyed.GetPatchMap();
  TRYV(PopulateGlyphKeyedPatchMap(glyph_keyed_patch_map));

  PatchMap& table_keyed_patch_map = table_keyed.GetPatchMap();
  PatchEncoding encoding =
      IsMixedMode() ? TABLE_KEYED_PARTIAL : TABLE_KEYED_FULL;
  TRYV(PopulateTableKeyedPatchMap(context, node_subset, edges, encoding,
                                  table_keyed_patch_map));

  auto face = node_data.face();
  std::optional<IFTTable*> ext =
      IsMixedMode() ? std::optional(&glyph_keyed) : std::nullopt;
  auto new_node_data = TRY(IFTTable::AddToFont(face.get(), table_keyed, ext));

  if (is_root) {
    // For the root node round trip the font through woff2 so that the base for
    // patching can be a decoded woff2 font file.
    node_data = TRY(RoundTripWoff2(new_node_data.str(), false));
  } else {
    node_data.shallow_copy(new_node_data);
  }

  context.built_subsets_[node_subset].shallow_copy(node_data);

  for (const auto& edge : edges) {
    SubsetDefinition current_node_subset = node_subset;
    FontData current_node_data;
    current_node_data.shallow_copy(node_data);

    for (const auto& j : edge.Jumps(node_subset, use_prefetch_lists_)) {
      uint32_t id = context.table_keyed_patch_id_map_[j];

      if (j.start != current_node_subset) {
        return absl::InternalError("Base mismatch with the current jump.");
      }

      auto next = TRY(Compile(context, j.end, false));
      if (context.built_table_keyed_patches_.contains(id)) {
        current_node_subset = j.end;
        current_node_data = std::move(next);
        continue;
      }

      // Check if the main table URL will change with this subset
      std::vector<uint8_t> next_glyph_keyed_url_template;
      CompatId next_glyph_keyed_compat_id;
      TRYV(EnsureGlyphKeyedPatchesPopulated(context, j.end.design_space,
                                            next_glyph_keyed_url_template,
                                            next_glyph_keyed_compat_id));

      bool replace_url_template =
          IsMixedMode() &&
          (next_glyph_keyed_url_template != glyph_keyed_url_template);

      FontData patch;
      auto differ =
          TRY(GetDifferFor(next, table_keyed_compat_id, replace_url_template));

      TRYV((*differ).Diff(current_node_data, next, &patch));

      std::string url =
          TRY(URLTemplate::PatchToUrl(table_keyed_url_template, id));
      context.patches_[url].shallow_copy(patch);
      context.built_table_keyed_patches_.insert(id);

      current_node_data = std::move(next);
      current_node_subset = j.end;
    }
  }

  return node_data;
}

StatusOr<std::unique_ptr<const BinaryDiff>> Compiler::GetDifferFor(
    const FontData& font_data, CompatId compat_id,
    bool replace_url_template) const {
  if (!IsMixedMode()) {
    return std::unique_ptr<const BinaryDiff>(
        Compiler::FullFontTableKeyedDiff(compat_id));
  }

  if (replace_url_template) {
    return std::unique_ptr<const BinaryDiff>(
        Compiler::ReplaceIftMapTableKeyedDiff(compat_id));
  }

  return std::unique_ptr<const BinaryDiff>(
      Compiler::MixedModeTableKeyedDiff(compat_id));
}

StatusOr<hb_subset_plan_t*> Compiler::CreateSubsetPlan(
    const ProcessingContext& context, hb_face_t* font,
    const SubsetDefinition& def) const {
  hb_subset_input_t* input = hb_subset_input_create_or_fail();
  if (!input) {
    return absl::InternalError("Failed to create subset input.");
  }

  def.ConfigureInput(input, font);
  SetMixedModeSubsettingFlagsIfNeeded(context, input);

  hb_subset_plan_t* plan = hb_subset_plan_create_or_fail(font, input);
  hb_subset_input_destroy(input);
  if (!plan) {
    return absl::InternalError("Harfbuzz subsetting plan generation failed.");
  }

  return plan;
}

StatusOr<hb_face_unique_ptr> Compiler::CutSubsetFaceBuilder(
    const ProcessingContext& context, hb_face_t* font,
    const SubsetDefinition& def) const {
  hb_subset_plan_t* plan = TRY(CreateSubsetPlan(context, font, def));

  hb_face_unique_ptr result =
      make_hb_face(hb_subset_plan_execute_or_fail(plan));
  hb_subset_plan_destroy(plan);
  if (!result.get()) {
    return absl::InternalError("Harfbuzz subsetting operation failed.");
  }

  return result;
}

StatusOr<FontData> Compiler::GenerateBaseGvar(
    const ProcessingContext& context, hb_face_t* font,
    const design_space_t& design_space) const {
  // When generating a gvar table for use with glyph keyed patches care
  // must be taken to ensure that the shared tuples in the gvar
  // header match the shared tuples used in the per glyph data
  // in the previously created (via GlyphKeyedDiff) glyph keyed
  // patches. However, we also want the gvar table to only contain
  // the glyphs from base_subset_. If you ran a single subsetting
  // operation through hb which reduced the glyphs and instanced
  // the design space the set of shared tuples may change.
  //
  // To keep the shared tuples correct we subset in two steps:
  // 1. Run instancing only, keeping everything else, this matches
  //    the processing done in EnsureGlyphKeyedPatchesPopulated()
  //    and will result in the same shared tuples.
  // 2. Run the glyph base subset, with no instancing specified.
  //    if there is no specified instancing then harfbuzz will
  //    not modify shared tuples.

  // Step 1: Instancing
  auto instance = Instance(context, font, design_space);
  if (!instance.ok()) {
    return instance.status();
  }

  // Step 2: glyph subsetting
  SubsetDefinition subset = context.init_subset_;
  // We don't want to apply any instancing here as it was done in step 1
  // so clear out the design space.
  subset.design_space = {};

  hb_face_unique_ptr instanced_face = instance->face();
  auto face_builder =
      CutSubsetFaceBuilder(context, instanced_face.get(), subset);
  if (!face_builder.ok()) {
    return face_builder.status();
  }

  // Step 3: extract gvar table.
  hb_blob_unique_ptr gvar_blob = make_hb_blob(
      hb_face_reference_table(face_builder->get(), FontHelper::kGvar));
  FontData result(gvar_blob.get());
  return result;
}

// Generate a CFF2 CharStrings index that retains glyph ids, but contains
// glyph data from face only for gids.
absl::StatusOr<std::string> GenerateCharStringsTable(hb_face_t* face,
                                                     const IntSet& gids) {
  // Create the per glyph data and offsets
  std::string charstrings_per_glyph;

  uint32_t glyph_count = hb_face_get_glyph_count(face);
  uint32_t current_offset = 1;
  std::vector<uint32_t> offsets;
  for (uint32_t gid = 0; gid < glyph_count; gid++) {
    offsets.push_back(current_offset);
    if (!gids.contains(gid)) {
      continue;
    }

    auto glyph_data = FontHelper::Cff2Data(face, gid);
    charstrings_per_glyph += glyph_data.str();
    current_offset += glyph_data.size();
  }
  offsets.push_back(current_offset);  // one extra offset at the end.

  if (offsets.size() != glyph_count + 1) {
    return absl::InternalError("Wrong number of offsets generated.");
  }

  // Determine offset size
  uint64_t offset_size = 1;
  for (; offset_size <= 4; offset_size++) {
    uint64_t max_value = (1 << (8 * offset_size)) - 1;
    if (current_offset <= max_value) {
      break;
    }
  }
  if (offset_size > 4) {
    return absl::InvalidArgumentError(
        "Offset overflow generating CFF2 charstrings.");
  }

  // Serialization, reference:
  // https://learn.microsoft.com/en-us/typography/opentype/spec/cff2#index-data
  std::string charstrings;

  FontHelper::WriteUInt32(glyph_count, charstrings);
  FontHelper::WriteUInt8(offset_size, charstrings);

  for (uint32_t offset : offsets) {
    switch (offset_size) {
      case 1:
        FontHelper::WriteUInt8(offset, charstrings);
        break;
      case 2:
        FontHelper::WriteUInt16(offset, charstrings);
        break;
      case 3:
        FontHelper::WriteUInt24(offset, charstrings);
        break;
      case 4:
      default:
        FontHelper::WriteUInt32(offset, charstrings);
        break;
    }
  }

  charstrings += charstrings_per_glyph;
  return charstrings;
}

StatusOr<FontData> Compiler::GenerateBaseCff2(
    const ProcessingContext& context, hb_face_t* font,
    const design_space_t& design_space) const {
  // The base CFF2 table is made by combining all of the non charstrings data
  // from 'font' which has only been instanced to 'design_space' with the
  // charstrings data for any glyphs retained by the base subset definition.
  //
  // To accomplish this we manually craft a new charstring table. This works
  // because the IFT spec requires charstrings data is at the end of the table
  // and doesn't overlap. so we are free to replace the charstrings table with
  // our own.

  // Step 1: Instancing
  auto instance = Instance(context, font, design_space);
  if (!instance.ok()) {
    return instance.status();
  }
  auto instance_face = instance->face();

  // Step 2: find the glyph closure for the base subset.
  SubsetDefinition subset = context.init_subset_;
  hb_subset_plan_t* plan = TRY(CreateSubsetPlan(context, font, subset));
  hb_map_t* old_to_new = hb_subset_plan_old_to_new_glyph_mapping(plan);

  int index = -1;
  uint32_t old_gid = HB_MAP_VALUE_INVALID;
  uint32_t new_gid = HB_MAP_VALUE_INVALID;
  IntSet gids;
  while (hb_map_next(old_to_new, &index, &old_gid, &new_gid)) {
    gids.insert(old_gid);
  }
  hb_subset_plan_destroy(plan);

  // Step 3: locate charstrings data
  FontData instance_non_charstrings;
  FontData instance_charstrings;
  TRYV(FontHelper::Cff2GetCharstrings(
      instance_face.get(), instance_non_charstrings, instance_charstrings));

  // Step 4: construct a new charstrings table.
  // This charstring table includes charstring data from "instance_face" for all
  // glyphs in "gids".
  std::string charstrings =
      TRY(GenerateCharStringsTable(instance_face.get(), gids));

  // Step 5: assemble the composite table.
  std::string composite_table = instance_non_charstrings.string() + charstrings;

  FontData result;
  result.copy(composite_table);
  return result;
}

void Compiler::SetMixedModeSubsettingFlagsIfNeeded(
    const ProcessingContext& context, hb_subset_input_t* input) const {
  if (IsMixedMode()) {
    // Mixed mode requires stable gids set flags accordingly.
    hb_subset_input_set_flags(
        input,
        hb_subset_input_get_flags(input) | HB_SUBSET_FLAGS_RETAIN_GIDS |
            HB_SUBSET_FLAGS_NOTDEF_OUTLINE |
            HB_SUBSET_FLAGS_PASSTHROUGH_UNRECOGNIZED |
            // CFF tables are always desubroutinized for mixed mode encoding.
            // This ensures that for each glyph all data for that glyph is fully
            // self contained. See: https://w3c.github.io/IFT/Overview.html#cff
            //
            // Note: a non desubroutinized mode could be supported, but a
            // special base CFF table would need to be generated in a similar
            // style to "GenerateBaseGvar()"
            HB_SUBSET_FLAGS_DESUBROUTINIZE);

    if (context.force_long_loca_and_gvar_) {
      // IFTB requirements flag has the side effect of forcing long loca and
      // gvar.
      hb_subset_input_set_flags(input, hb_subset_input_get_flags(input) |
                                           HB_SUBSET_FLAGS_IFTB_REQUIREMENTS);
    }
  }
}

// Creates a subset for a given subset definition.
//
// If 'generate_glyph_keyed_bases' is true then for tables such as gvar and CFF2
// which have common data, the subsetted tables will be generated in a way that
// preserves that common data in order to retain compatibility with glyph keyed
// patching. See the comments in this function for more details.
//
// Additionally the set of glyphs in these tables will be set to the set of
// glyphs in the base subset rather then what's in def since glyph keyed patches
// are responsible for populating those.
//
// Special casing isn't needed for glyf or CFF since those are never patched
// by table keyed patches and don't have common data (CFF is desubroutinized)
// so we can just ignore them here.
StatusOr<FontData> Compiler::CutSubset(const ProcessingContext& context,
                                       hb_face_t* font,
                                       const SubsetDefinition& def,
                                       bool generate_glyph_keyed_bases) const {
  auto result = CutSubsetFaceBuilder(context, font, def);
  if (!result.ok()) {
    return result.status();
  }

  auto tags = FontHelper::GetTags(font);
  if (generate_glyph_keyed_bases && def.IsVariable() &&
      tags.contains(FontHelper::kGvar)) {
    // In mixed mode glyph keyed patches handles gvar, except for when design
    // space is expanded, in which case a gvar table should be patched in that
    // only has coverage of the base (root) subset definition + the current
    // design space.
    //
    // Create such a gvar table here and overwrite the one that was otherwise
    // generated by the normal subsetting operation. The patch generation will
    // handle including a replacement gvar patch when needed.
    auto base_gvar = TRY(GenerateBaseGvar(context, font, def.design_space));
    hb_blob_unique_ptr gvar_blob = base_gvar.blob();
    hb_face_builder_add_table(result->get(), FontHelper::kGvar,
                              gvar_blob.get());
  }

  if (generate_glyph_keyed_bases && tags.contains(FontHelper::kCFF2)) {
    // In mixed mode glyph keyed patches handles CFF2 per glyph data. However,
    // the CFF2 table may contain shared variation data outside of the glyphs.
    // So when creating a subsetted CFF2 table here we need to ensure the shared
    // variation data will match whatever the glyph keyed patches were cut from.
    auto base_cff2 = TRY(GenerateBaseCff2(context, font, def.design_space));
    hb_blob_unique_ptr cff2_blob = base_cff2.blob();
    hb_face_builder_add_table(result->get(), FontHelper::kCFF2,
                              cff2_blob.get());
  }

  hb_blob_unique_ptr blob = make_hb_blob(hb_face_reference_blob(result->get()));

  FontData subset(blob.get());
  return subset;
}

StatusOr<FontData> Compiler::Instance(
    const ProcessingContext& context, hb_face_t* face,
    const design_space_t& design_space) const {
  hb_subset_input_t* input = hb_subset_input_create_or_fail();

  // Keep everything in this subset, except for applying the design space.
  hb_subset_input_keep_everything(input);
  SetMixedModeSubsettingFlagsIfNeeded(context, input);

  for (const auto& [tag, range] : design_space) {
    hb_subset_input_set_axis_range(input, face, tag, range.start(), range.end(),
                                   NAN);
  }

  hb_face_unique_ptr subset = make_hb_face(hb_subset_or_fail(face, input));
  hb_subset_input_destroy(input);

  if (!subset.get()) {
    return absl::InternalError("Instancing failed.");
  }

  hb_blob_unique_ptr out = make_hb_blob(hb_face_reference_blob(subset.get()));

  FontData result(out.get());
  return result;
}

StatusOr<FontData> Compiler::RoundTripWoff2(string_view font,
                                            bool glyf_transform) {
  auto r = Woff2::EncodeWoff2(font, glyf_transform);
  if (!r.ok()) {
    return r.status();
  }

  return Woff2::DecodeWoff2(r->str());
}

CompatId Compiler::ProcessingContext::GenerateCompatId() {
  return CompatId(
      this->random_values_(this->gen_), this->random_values_(this->gen_),
      this->random_values_(this->gen_), this->random_values_(this->gen_));
}

}  // namespace ift::encoder
