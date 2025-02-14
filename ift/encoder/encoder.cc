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
#include "common/hb_set_unique_ptr.h"
#include "common/try.h"
#include "common/woff2.h"
#include "hb-subset.h"
#include "ift/glyph_keyed_diff.h"
#include "ift/proto/ift_table.h"
#include "ift/proto/patch_encoding.h"
#include "ift/proto/patch_map.h"
#include "ift/url_template.h"

using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
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
using common::hb_set_unique_ptr;
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

void Encoder::AddCombinations(const std::vector<const SubsetDefinition*>& in,
                              uint32_t choose,
                              std::vector<SubsetDefinition>& out) {
  if (!choose || in.size() < choose) {
    return;
  }

  if (choose == 1) {
    for (auto item : in) {
      out.push_back(*item);
    }
    return;
  }

  for (auto it = in.begin(); it != in.end(); it++) {
    auto it_inner = it + 1;
    std::vector<const SubsetDefinition*> remaining;
    std::copy(it_inner, in.end(), std::back_inserter(remaining));

    std::vector<SubsetDefinition> combinations;
    AddCombinations(remaining, choose - 1, combinations);
    for (auto& s : combinations) {
      s.Union(**it);
      out.push_back(std::move(s));
    }
  }
}

StatusOr<FontData> Encoder::FullyExpandedSubset(
    const ProcessingContext& context) const {
  SubsetDefinition all;
  all.Union(base_subset_);

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

  return CutSubset(context, face_.get(), all);
}

bool is_subset(const flat_hash_set<uint32_t>& a,
               const flat_hash_set<uint32_t>& b) {
  return std::all_of(b.begin(), b.end(),
                     [&a](const uint32_t& v) { return a.count(v) > 0; });
}

std::vector<SubsetDefinition> Encoder::OutgoingEdges(
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

  std::vector<SubsetDefinition> result;
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

Status Encoder::AddGlyphDataPatch(uint32_t id,
                                  const absl::btree_set<uint32_t>& gids) {
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

Status Encoder::SetBaseSubsetFromPatches(
    const flat_hash_set<uint32_t>& included_glyph_data) {
  design_space_t empty;
  return SetBaseSubsetFromPatches(included_glyph_data, empty);
}

Status Encoder::SetBaseSubsetFromPatches(
    const flat_hash_set<uint32_t>& included_glyph_data,
    const design_space_t& design_space) {
  // TODO(garretrieger): support also providing initial features.
  if (!face_) {
    return absl::FailedPreconditionError("Encoder must have a face set.");
  }

  if (!base_subset_.empty()) {
    return absl::FailedPreconditionError("Base subset has already been set.");
  }

  for (uint32_t patch_id : included_glyph_data) {
    if (!glyph_data_patches_.contains(patch_id)) {
      return absl::InvalidArgumentError(StrCat("Glyph data patch, ", patch_id,
                                               ", not added to the encoder."));
    }
  }

  auto included = SubsetDefinitionForPatches(included_glyph_data);
  if (!included.ok()) {
    return included.status();
  }

  base_subset_ = *included;
  base_subset_.design_space = design_space;

  // Glyph keyed patches can't change the glyph count in the font (and hence
  // loca len) so always include the last gid in the base subset to force the
  // loca table to remain at the full length from the start.
  //
  // TODO(garretrieger): this unnecessarily includes the last gid in the subset,
  //                     should update the subsetter to retain the glyph count
  //                     but not actually keep the last gid.
  //
  // TODO(garretrieger): instead of forcing max glyph count here we can utilize
  //                     table keyed patches to change loca len/glyph count to
  //                     the max for any currently reachable segments. This
  //                     would improve efficiency slightly by avoid including
  //                     extra space in the initial font.
  uint32_t gid_count = hb_face_get_glyph_count(face_.get());
  if (gid_count > 0) base_subset_.gids.insert(gid_count - 1);

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

StatusOr<SubsetDefinition> Encoder::SubsetDefinitionForPatches(
    const flat_hash_set<uint32_t>& patch_ids) const {
  auto gid_to_unicode = FontHelper::GidToUnicodeMap(face_.get());

  SubsetDefinition result;
  for (uint32_t patch_id : patch_ids) {
    auto p = glyph_data_patches_.find(patch_id);
    if (p == glyph_data_patches_.end()) {
      return absl::InvalidArgumentError(
          StrCat("Glyph data patches, ", patch_id, ", not found."));
    }

    for (uint32_t gid : p->second) {
      auto cp = gid_to_unicode.find(gid);
      if (cp != gid_to_unicode.end()) {
        result.codepoints.insert(cp->second);
      }
      result.gids.insert(gid);
    }
  }

  return result;
}

StatusOr<Encoder::Encoding> Encoder::Encode() const {
  if (!face_) {
    return absl::FailedPreconditionError("Encoder must have a face set.");
  }

  ProcessingContext context(next_id_);
  context.force_long_loca_and_gvar_ = false;
  auto expanded = FullyExpandedSubset(context);
  if (!expanded.ok()) {
    return expanded.status();
  }

  context.fully_expanded_subset_.shallow_copy(*expanded);
  auto expanded_face = expanded->face();
  context.force_long_loca_and_gvar_ =
      FontHelper::HasLongLoca(expanded_face.get()) ||
      FontHelper::HasWideGvar(expanded_face.get());

  auto init_font = Encode(context, base_subset_, true);
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

  flat_hash_set<uint32_t> reachable_segments;
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
                        {FontHelper::kGlyf, FontHelper::kGvar});

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

  std::vector<SubsetDefinition> subsets =
      OutgoingEdges(base_subset, jump_ahead_);

  // The first subset forms the base file, the remaining subsets are made
  // reachable via patches.
  auto full_face = context.fully_expanded_subset_.face();
  auto base = CutSubset(context, full_face.get(), base_subset);
  if (!base.ok()) {
    return base.status();
  }

  if (subsets.empty() && !IsMixedMode()) {
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

  std::vector<uint32_t> ids;
  PatchMap& table_keyed_patch_map = table_keyed.GetPatchMap();
  PatchEncoding encoding =
      IsMixedMode() ? TABLE_KEYED_PARTIAL : TABLE_KEYED_FULL;
  for (const auto& s : subsets) {
    uint32_t id = context.next_id_++;
    ids.push_back(id);

    PatchMap::Coverage coverage = s.ToCoverage();
    TRYV(table_keyed_patch_map.AddEntry(coverage, id, encoding));
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

  uint32_t i = 0;
  for (const auto& s : subsets) {
    uint32_t id = ids[i++];
    SubsetDefinition combined_subset = Combine(base_subset, s);
    auto next = Encode(context, combined_subset, false);
    if (!next.ok()) {
      return next.status();
    }

    // Check if the main table URL will change with this subset
    std::string next_glyph_keyed_uri_template;
    CompatId next_glyph_keyed_compat_id;
    auto sc = EnsureGlyphKeyedPatchesPopulated(
        context, base_subset.design_space, glyph_keyed_uri_template,
        glyph_keyed_compat_id);
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

StatusOr<hb_face_unique_ptr> Encoder::CutSubsetFaceBuilder(
    const ProcessingContext& context, hb_face_t* font,
    const SubsetDefinition& def) const {
  hb_subset_input_t* input = hb_subset_input_create_or_fail();
  if (!input) {
    return absl::InternalError("Failed to create subset input.");
  }

  def.ConfigureInput(input, font);

  SetMixedModeSubsettingFlagsIfNeeded(context, input);

  hb_face_unique_ptr result = make_hb_face(hb_subset_or_fail(font, input));
  if (!result.get()) {
    return absl::InternalError("Harfbuzz subsetting operation failed.");
  }

  hb_subset_input_destroy(input);
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
  SubsetDefinition subset = base_subset_;
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
      hb_face_reference_table(face_builder->get(), HB_TAG('g', 'v', 'a', 'r')));
  FontData result(gvar_blob.get());
  return result;
}

void Encoder::SetMixedModeSubsettingFlagsIfNeeded(
    const ProcessingContext& context, hb_subset_input_t* input) const {
  if (IsMixedMode()) {
    // Mixed mode requires stable gids set flags accordingly.
    hb_subset_input_set_flags(
        input, hb_subset_input_get_flags(input) | HB_SUBSET_FLAGS_RETAIN_GIDS |
                   HB_SUBSET_FLAGS_NOTDEF_OUTLINE |
                   HB_SUBSET_FLAGS_PASSTHROUGH_UNRECOGNIZED);

    if (context.force_long_loca_and_gvar_) {
      // IFTB requirements flag has the side effect of forcing long loca and
      // gvar.
      hb_subset_input_set_flags(input, hb_subset_input_get_flags(input) |
                                           HB_SUBSET_FLAGS_IFTB_REQUIREMENTS);
    }
  }
}

StatusOr<FontData> Encoder::CutSubset(const ProcessingContext& context,
                                      hb_face_t* font,
                                      const SubsetDefinition& def) const {
  auto result = CutSubsetFaceBuilder(context, font, def);
  if (!result.ok()) {
    return result.status();
  }

  if (IsMixedMode() && def.IsVariable()) {
    // In mixed mode glyph keyed patches handles gvar, except for when design
    // space is expanded, in which case a gvar table should be patched in that
    // only has coverage of the base (root) subset definition + the current
    // design space.
    //
    // Create such a gvar table here and overwrite the one that was otherwise
    // generated by the normal subsetting operation. The patch generation will
    // handle including a replacement gvar patch when needed.
    auto base_gvar = GenerateBaseGvar(context, font, def.design_space);
    if (!base_gvar.ok()) {
      return base_gvar.status();
    }

    hb_blob_unique_ptr gvar_blob = base_gvar->blob();
    hb_face_builder_add_table(result->get(), HB_TAG('g', 'v', 'a', 'r'),
                              gvar_blob.get());
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
