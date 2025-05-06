#include "ift/encoder/encoder.h"

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
#include "common/axis_range.h"
#include "common/binary_diff.h"
#include "common/compat_id.h"
#include "common/font_data.h"
#include "common/font_helper.h"
#include "common/int_set.h"
#include "common/try.h"
#include "common/woff2.h"
#include "hb-subset.h"
#include "ift/encoder/subset_definition.h"
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
using common::AxisRange;
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
using common::Woff2;
using ift::GlyphKeyedDiff;
using ift::proto::GLYPH_KEYED;
using ift::proto::IFTTable;
using ift::proto::PatchEncoding;
using ift::proto::PatchMap;
using ift::proto::TABLE_KEYED_FULL;
using ift::proto::TABLE_KEYED_PARTIAL;

namespace ift::encoder {

static void AddCombinations(const std::vector<const SubsetDefinition*>& in,
                            uint32_t choose, std::vector<Encoder::Edge>& out) {
  if (!choose || in.size() < choose) {
    return;
  }

  if (choose == 1) {
    for (auto item : in) {
      out.push_back(Encoder::Edge{*item});
    }
    return;
  }

  for (auto it = in.begin(); it != in.end(); it++) {
    auto it_inner = it + 1;
    std::vector<const SubsetDefinition*> remaining;
    std::copy(it_inner, in.end(), std::back_inserter(remaining));

    std::vector<Encoder::Edge> combinations;
    AddCombinations(remaining, choose - 1, combinations);
    for (auto& edge : combinations) {
      edge.Add(**it);
      out.push_back(std::move(edge));
    }
  }
}

StatusOr<FontData> Encoder::FullyExpandedSubset(
    const ProcessingContext& context) const {
  SubsetDefinition all;
  all.Union(context.base_subset_);

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

std::vector<Encoder::Edge> Encoder::OutgoingEdges(
    const SubsetDefinition& base_subset, uint32_t choose) const {
  std::vector<SubsetDefinition> remaining_subsets;
  for (const auto& s : extension_subsets_) {
    SubsetDefinition filtered = s;
    filtered.Subtract(base_subset);
    if (filtered.empty()) {
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

SubsetDefinition Encoder::Combine(const SubsetDefinition& s1,
                                  const SubsetDefinition& s2) const {
  SubsetDefinition result;
  result.Union(s1);
  result.Union(s2);
  return result;
}

Status Encoder::AddGlyphDataPatch(uint32_t id, const IntSet& gids) {
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

Status Encoder::AddGlyphDataPatchCondition(Condition condition) {
  uint32_t new_index = glyph_patch_conditions_.size();
  for (uint32_t child_index : condition.child_conditions) {
    if (child_index >= new_index) {
      return absl::InvalidArgumentError(
          StrCat("Child conditions must only references previous conditions: ",
                 child_index, " >= ", new_index));
    }
  }

  if (condition.activated_patch_id.has_value() &&
      !glyph_data_patches_.contains(*condition.activated_patch_id)) {
    return absl::InvalidArgumentError(
        StrCat("Glyh data patch ", *condition.activated_patch_id,
               " has not been supplied via AddGlyphDataPatch()"));
  }

  glyph_patch_conditions_.push_back(condition);
  return absl::OkStatus();
}

void Encoder::AddFeatureGroupSegment(const btree_set<hb_tag_t>& feature_tags) {
  SubsetDefinition def;
  def.feature_tags = feature_tags;
  extension_subsets_.push_back(def);
}

void Encoder::AddDesignSpaceSegment(const design_space_t& space) {
  SubsetDefinition def;
  def.design_space = space;
  extension_subsets_.push_back(def);
}

StatusOr<Encoder::Encoding> Encoder::Encode() const {
  if (!face_) {
    return absl::FailedPreconditionError("Encoder must have a face set.");
  }

  ProcessingContext context(next_id_);
  context.base_subset_ = base_subset_;
  if (IsMixedMode()) {
    // Glyph keyed patches can't change the glyph count in the font (and hence
    // loca len) so always include the last gid in the base subset to force the
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
    if (gid_count > 0) context.base_subset_.gids.insert(gid_count - 1);
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

  auto init_font = Encode(context, context.base_subset_, true);
  if (!init_font.ok()) {
    return init_font.status();
  }

  Encoding result;
  result.init_font.shallow_copy(*init_font);
  result.patches = std::move(context.patches_);
  return result;
}

bool Encoder::AllocatePatchSet(ProcessingContext& context,
                               const design_space_t& design_space,
                               std::string& uri_template,
                               CompatId& compat_id) const {
  auto uri_it = context.patch_set_uri_templates_.find(design_space);
  auto compat_id_it = context.glyph_keyed_compat_ids_.find(design_space);

  bool has_uri = (uri_it != context.patch_set_uri_templates_.end());
  bool has_compat_id = (compat_id_it != context.glyph_keyed_compat_ids_.end());

  if (has_uri && has_compat_id) {
    // already created, return existing.
    uri_template = uri_it->second;
    compat_id = compat_id_it->second;
    return false;
  }

  uri_template = UrlTemplate(context.next_patch_set_id_++);
  compat_id = context.GenerateCompatId();

  context.patch_set_uri_templates_[design_space] = uri_template;
  context.glyph_keyed_compat_ids_[design_space] = compat_id;
  return true;
}

Status Encoder::EnsureGlyphKeyedPatchesPopulated(
    ProcessingContext& context, const design_space_t& design_space,
    std::string& uri_template, CompatId& compat_id) const {
  if (glyph_data_patches_.empty()) {
    return absl::OkStatus();
  }

  IntSet reachable_segments;
  for (const auto& condition : glyph_patch_conditions_) {
    if (condition.activated_patch_id.has_value()) {
      reachable_segments.insert(*condition.activated_patch_id);
    }
  }

  if (!AllocatePatchSet(context, design_space, uri_template, compat_id)) {
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

    std::string url = URLTemplate::PatchToUrl(uri_template, index);

    const auto& gids = e->second;
    auto patch = differ.CreatePatch(gids);
    if (!patch.ok()) {
      return patch.status();
    }

    context.patches_[url].shallow_copy(*patch);
  }

  return absl::OkStatus();
}

Status Encoder::PopulateGlyphKeyedPatchMap(PatchMap& patch_map) const {
  if (glyph_data_patches_.empty()) {
    return absl::OkStatus();
  }

  uint32_t last_patch_index = 0;
  for (const auto& condition : glyph_patch_conditions_) {
    auto coverage = condition.subset_definition.ToCoverage();
    coverage.child_indices.insert(condition.child_conditions.begin(),
                                  condition.child_conditions.end());
    coverage.conjunctive = condition.conjunctive;

    if (condition.activated_patch_id.has_value()) {
      last_patch_index = *condition.activated_patch_id;
      TRYV(patch_map.AddEntry(coverage, last_patch_index, GLYPH_KEYED));
    } else {
      TRYV(patch_map.AddEntry(coverage, ++last_patch_index, GLYPH_KEYED, true));
    }
  }

  return absl::OkStatus();
}

StatusOr<FontData> Encoder::Encode(ProcessingContext& context,
                                   const SubsetDefinition& base_subset,
                                   bool is_root) const {
  auto it = context.built_subsets_.find(base_subset);
  if (it != context.built_subsets_.end()) {
    FontData copy;
    copy.shallow_copy(it->second);
    return copy;
  }

  std::string table_keyed_uri_template = UrlTemplate(0);
  CompatId table_keyed_compat_id = context.GenerateCompatId();
  std::string glyph_keyed_uri_template;
  CompatId glyph_keyed_compat_id;
  auto sc = EnsureGlyphKeyedPatchesPopulated(context, base_subset.design_space,
                                             glyph_keyed_uri_template,
                                             glyph_keyed_compat_id);
  if (!sc.ok()) {
    return sc;
  }

  std::vector<Edge> edges = OutgoingEdges(base_subset, jump_ahead_);

  // The first subset forms the base file, the remaining subsets are made
  // reachable via patches.
  auto full_face = context.fully_expanded_subset_.face();
  auto base = CutSubset(context, full_face.get(), base_subset, IsMixedMode());
  if (!base.ok()) {
    return base.status();
  }

  if (edges.empty() && !IsMixedMode()) {
    // This is a leaf node, a IFT table isn't needed.
    context.built_subsets_[base_subset].shallow_copy(*base);
    return base;
  }

  IFTTable table_keyed;
  IFTTable glyph_keyed;
  table_keyed.SetId(table_keyed_compat_id);
  table_keyed.SetUrlTemplate(table_keyed_uri_template);
  glyph_keyed.SetId(glyph_keyed_compat_id);
  glyph_keyed.SetUrlTemplate(glyph_keyed_uri_template);

  PatchMap& glyph_keyed_patch_map = glyph_keyed.GetPatchMap();
  sc = PopulateGlyphKeyedPatchMap(glyph_keyed_patch_map);
  if (!sc.ok()) {
    return sc;
  }

  // TODO(garretrieger): XXXXX extract this to a helper.
  flat_hash_map<Jump, uint32_t> id_map;
  PatchMap& table_keyed_patch_map = table_keyed.GetPatchMap();
  PatchEncoding encoding =
      IsMixedMode() ? TABLE_KEYED_PARTIAL : TABLE_KEYED_FULL;
  for (const auto& edge : edges) {
    std::vector<uint32_t> edge_patches;
    for (Jump& j : edge.Jumps(base_subset, use_preload_lists_)) {
      auto [it, did_insert] = id_map.insert(std::pair(std::move(j), context.next_id_));
      if (did_insert) {
        context.next_id_++;
      }
      edge_patches.push_back(it->second);
    }

    if (!edge_patches.empty()) {
      PatchMap::Coverage coverage = edge.Combined().ToCoverage();
      // TODO XXXXX add multi id patch map entry using edge_ids.
      TRYV(table_keyed_patch_map.AddEntry(coverage, edge_patches[0], encoding));
    }
  }

  auto face = base->face();
  std::optional<IFTTable*> ext =
      IsMixedMode() ? std::optional(&glyph_keyed) : std::nullopt;
  auto new_base = IFTTable::AddToFont(face.get(), table_keyed, ext);

  if (!new_base.ok()) {
    return new_base.status();
  }

  if (is_root) {
    // For the root node round trip the font through woff2 so that the base for
    // patching can be a decoded woff2 font file.
    base = RoundTripWoff2(new_base->str(), false);
    if (!base.ok()) {
      return base.status();
    }
  } else {
    base->shallow_copy(*new_base);
  }

  context.built_subsets_[base_subset].shallow_copy(*base);
  IntSet built_patches;

  for (const auto& edge : edges) {
    for (const auto& j : edge.Jumps(base_subset, use_preload_lists_)) {
      uint32_t id = id_map[j];
      if (built_patches.contains(id)) {
        continue;
      }

      auto next = Encode(context, j.target, false);
      if (!next.ok()) {
        return next.status();
      }

      // Check if the main table URL will change with this subset
      std::string next_glyph_keyed_uri_template;
      CompatId next_glyph_keyed_compat_id;
      auto sc = EnsureGlyphKeyedPatchesPopulated(context, j.target.design_space,
                                                 next_glyph_keyed_uri_template,
                                                 next_glyph_keyed_compat_id);
      if (!sc.ok()) {
        return sc;
      }

      bool replace_url_template =
          IsMixedMode() &&
          (next_glyph_keyed_uri_template != glyph_keyed_uri_template);

      FontData patch;
      auto differ =
          GetDifferFor(*next, table_keyed_compat_id, replace_url_template);
      if (!differ.ok()) {
        return differ.status();
      }
      sc = (*differ)->Diff(*base, *next, &patch);
      if (!sc.ok()) {
        return sc;
      }

      std::string url = URLTemplate::PatchToUrl(table_keyed_uri_template, id);
      context.patches_[url].shallow_copy(patch);
      built_patches.insert(id);
    }
  }

  return base;
}

StatusOr<std::unique_ptr<const BinaryDiff>> Encoder::GetDifferFor(
    const FontData& font_data, CompatId compat_id,
    bool replace_url_template) const {
  if (!IsMixedMode()) {
    return std::unique_ptr<const BinaryDiff>(
        Encoder::FullFontTableKeyedDiff(compat_id));
  }

  if (replace_url_template) {
    return std::unique_ptr<const BinaryDiff>(
        Encoder::ReplaceIftMapTableKeyedDiff(compat_id));
  }

  return std::unique_ptr<const BinaryDiff>(
      Encoder::MixedModeTableKeyedDiff(compat_id));
}

StatusOr<hb_subset_plan_t*> Encoder::CreateSubsetPlan(
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

StatusOr<hb_face_unique_ptr> Encoder::CutSubsetFaceBuilder(
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

StatusOr<FontData> Encoder::GenerateBaseGvar(
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
  SubsetDefinition subset = context.base_subset_;
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

StatusOr<FontData> Encoder::GenerateBaseCff2(
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
  SubsetDefinition subset = context.base_subset_;
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

void Encoder::SetMixedModeSubsettingFlagsIfNeeded(
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
StatusOr<FontData> Encoder::CutSubset(const ProcessingContext& context,
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

StatusOr<FontData> Encoder::Instance(const ProcessingContext& context,
                                     hb_face_t* face,
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

StatusOr<FontData> Encoder::RoundTripWoff2(string_view font,
                                           bool glyf_transform) {
  auto r = Woff2::EncodeWoff2(font, glyf_transform);
  if (!r.ok()) {
    return r.status();
  }

  return Woff2::DecodeWoff2(r->str());
}

CompatId Encoder::ProcessingContext::GenerateCompatId() {
  return CompatId(
      this->random_values_(this->gen_), this->random_values_(this->gen_),
      this->random_values_(this->gen_), this->random_values_(this->gen_));
}

}  // namespace ift::encoder
