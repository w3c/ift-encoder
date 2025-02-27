#include "ift/encoder/closure_glyph_segmenter.h"

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/compat_id.h"
#include "common/font_data.h"
#include "common/font_helper.h"
#include "common/hb_set_unique_ptr.h"
#include "common/try.h"
#include "ift/glyph_keyed_diff.h"

using absl::btree_map;
using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using common::CompatId;
using common::FontData;
using common::FontHelper;
using common::hb_set_unique_ptr;
using common::make_hb_face;
using common::make_hb_set;
using common::to_btree_set;
using common::to_hash_set;
using ift::GlyphKeyedDiff;

namespace ift::encoder {

// An indepth description of how this segmentation implementation works can
// be found in ../../docs/closure_glyph_segmentation.md.

// TODO(garretrieger): extensions/improvements that could be made:
// - Make a HbSet class which implements hash and equality so we can use in map
// keys and sets.
// - Can we reduce # of closures for the additional conditions checks?
//   - is the full analysis needed to get the or set?
// - Add logging
//   - timing info
// - Use merging and/or duplication to ensure minimum patch size.
//   - composite patches (NOT STARTED)
// - Multi segment combination testing with GSUB dep analysis to guide.

class GlyphConditions {
 public:
  GlyphConditions() : and_segments(make_hb_set()), or_segments(make_hb_set()) {}
  hb_set_unique_ptr and_segments;
  hb_set_unique_ptr or_segments;

  void RemoveSegments(const hb_set_t* segments) {
    hb_set_subtract(and_segments.get(), segments);
    hb_set_subtract(or_segments.get(), segments);
  }
};

class SegmentationContext;

Status AnalyzeSegment(SegmentationContext& context, const hb_set_t* codepoints,
                      hb_set_t* and_gids, hb_set_t* or_gids,
                      hb_set_t* exclusive_gids);

class SegmentationContext {
 public:
  SegmentationContext(
      hb_face_t* face, const flat_hash_set<uint32_t>& initial_segment,
      const std::vector<flat_hash_set<uint32_t>>& codepoint_segments)
      : preprocessed_face(make_hb_face(hb_subset_preprocess(face))),
        original_face(make_hb_face(hb_face_reference(face))),
        segments(),
        initial_codepoints(make_hb_set(initial_segment)),
        all_codepoints(make_hb_set()),
        full_closure(make_hb_set()),
        initial_closure(make_hb_set()) {
    for (const auto& s : codepoint_segments) {
      segments.push_back(make_hb_set(s));
    }

    hb_set_union(all_codepoints.get(), initial_codepoints.get());
    for (const auto& s : segments) {
      hb_set_union(all_codepoints.get(), s.get());
    }

    {
      auto closure = GlyphClosure(initial_codepoints.get());
      if (closure.ok()) {
        initial_closure.reset(closure->release());
      }
    }

    auto closure = GlyphClosure(all_codepoints.get());
    if (closure.ok()) {
      full_closure.reset(closure->release());
    }

    gid_conditions.resize(hb_face_get_glyph_count(original_face.get()));
  }

  void ResetGroupings() {
    unmapped_glyphs = {};
    and_glyph_groups = {};
    or_glyph_groups = {};
    fallback_segments = {};
  }

  StatusOr<hb_set_unique_ptr> GlyphClosure(const hb_set_t* codepoints) {
    auto cache_key = to_hash_set(codepoints);

    auto it = glyph_closure_cache.find(cache_key);
    if (it != glyph_closure_cache.end()) {
      glyph_closure_cache_hit++;
      hb_set_unique_ptr result = make_hb_set();
      hb_set_union(result.get(), it->second.get());
      return result;
    }

    glyph_closure_cache_miss++;
    closure_count_cumulative++;
    closure_count_delta++;

    hb_subset_input_t* input = hb_subset_input_create_or_fail();
    if (!input) {
      return absl::InternalError("Closure subset configuration failed.");
    }

    hb_set_union(hb_subset_input_unicode_set(input), codepoints);
    // TODO(garretrieger): configure features (and other settings) appropriately
    // based on the IFT default feature list.

    hb_subset_plan_t* plan =
        hb_subset_plan_create_or_fail(preprocessed_face.get(), input);
    hb_subset_input_destroy(input);
    if (!plan) {
      return absl::InternalError("Closure calculation failed.");
    }

    hb_map_t* new_to_old = hb_subset_plan_new_to_old_glyph_mapping(plan);
    hb_set_unique_ptr gids = make_hb_set();
    hb_map_values(new_to_old, gids.get());
    hb_subset_plan_destroy(plan);

    hb_set_unique_ptr cached_gids = make_hb_set();
    hb_set_union(cached_gids.get(), gids.get());
    glyph_closure_cache.insert(std::pair(cache_key, std::move(cached_gids)));

    return gids;
  }

  void LogClosureCount(absl::string_view operation) {
    VLOG(0) << operation << ": cumulative number of glyph closures "
            << closure_count_cumulative << " (+" << closure_count_delta << ")";
    closure_count_delta = 0;
  }

  void LogCacheStats() {
    double hit_rate = 100.0 * ((double)code_point_set_to_or_gids_cache_hit) /
                      ((double)(code_point_set_to_or_gids_cache_hit +
                                code_point_set_to_or_gids_cache_miss));
    VLOG(0) << "Codepoints to or_gids cache hit rate: " << hit_rate << "% ("
            << code_point_set_to_or_gids_cache_hit << " hits, "
            << code_point_set_to_or_gids_cache_miss << " misses)";

    double closure_hit_rate =
        100.0 * ((double)glyph_closure_cache_hit) /
        ((double)(glyph_closure_cache_hit + glyph_closure_cache_miss));
    VLOG(0) << "Glyph closure cache hit rate: " << closure_hit_rate << "% ("
            << glyph_closure_cache_hit << " hits, " << glyph_closure_cache_miss
            << " misses)";
  }

  StatusOr<const hb_set_t*> CodepointsToOrGids(const hb_set_t* codepoints) {
    auto hash_set_codepoints = common::to_hash_set(codepoints);

    auto it = code_point_set_to_or_gids_cache.find(hash_set_codepoints);
    if (it != code_point_set_to_or_gids_cache.end()) {
      code_point_set_to_or_gids_cache_hit++;
      return it->second.get();
    }

    code_point_set_to_or_gids_cache_miss++;
    hb_set_unique_ptr and_gids = make_hb_set();
    hb_set_unique_ptr or_gids = make_hb_set();
    hb_set_unique_ptr exclusive_gids = make_hb_set();
    TRYV(AnalyzeSegment(*this, codepoints, and_gids.get(), or_gids.get(),
                        exclusive_gids.get()));

    const hb_set_t* or_gids_ptr = or_gids.get();
    code_point_set_to_or_gids_cache.insert(
        std::pair(hash_set_codepoints, std::move(or_gids)));
    return or_gids_ptr;
  }

  // Init
  common::hb_face_unique_ptr preprocessed_face;
  common::hb_face_unique_ptr original_face;
  std::vector<hb_set_unique_ptr> segments;

  hb_set_unique_ptr initial_codepoints;
  hb_set_unique_ptr all_codepoints;
  hb_set_unique_ptr full_closure;
  hb_set_unique_ptr initial_closure;

  uint32_t patch_size_min_bytes = 0;
  uint32_t patch_size_max_bytes = UINT32_MAX;

  // Phase 1
  std::vector<GlyphConditions> gid_conditions;

  // Phase 2
  btree_set<glyph_id_t> unmapped_glyphs;
  btree_map<btree_set<segment_index_t>, btree_set<glyph_id_t>> and_glyph_groups;
  btree_map<btree_set<segment_index_t>, btree_set<glyph_id_t>> or_glyph_groups;
  btree_set<segment_index_t> fallback_segments;

  // Caches and logging
  flat_hash_map<flat_hash_set<uint32_t>, hb_set_unique_ptr> glyph_closure_cache;
  uint32_t glyph_closure_cache_hit = 0;
  uint32_t glyph_closure_cache_miss = 0;

  flat_hash_map<flat_hash_set<uint32_t>, hb_set_unique_ptr>
      code_point_set_to_or_gids_cache;
  uint32_t code_point_set_to_or_gids_cache_hit = 0;
  uint32_t code_point_set_to_or_gids_cache_miss = 0;

  uint32_t closure_count_cumulative = 0;
  uint32_t closure_count_delta = 0;
};

Status AnalyzeSegment(SegmentationContext& context, const hb_set_t* codepoints,
                      hb_set_t* and_gids, hb_set_t* or_gids,
                      hb_set_t* exclusive_gids) {
  if (hb_set_is_empty(codepoints)) {
    // Skip empty sets, they will never contribute any conditions.
    return absl::OkStatus();
  }

  // This function tests various closures using the segment codepoints to
  // determine what conditions are present for the inclusion of closure glyphs.
  //
  // At a high level we do the following (where s_i is the segment being
  // tested):
  //
  // * Set A: glyph closure on original font of the union of all segments.
  // * Set B: glyph closure on original font of the union of all segments except
  //          for s_i
  // * Set I: (glyph closure on original font of s_0 union s_i) - (glyph closure
  //           on original font of s_0)
  // * Set D: A - B, the set of glyphs that are dropped when s_i is removed.
  //
  // Then we know the following:
  // * Glyphs in I should be included whenever s_i is activated.
  // * s_i is necessary for glyphs in D to be required, but other segments may
  //   be needed too.
  //
  // Furthermore we can intersect I and D to produce three sets:
  // * D - I: the activation condition for these glyphs is s_i AND …
  //          Where … is one or more additional segments.
  // * I - D: the activation conditions for these glyphs is s_i OR …
  //          Where … is one or more additional segments.
  // * D intersection I: the activation conditions for these glyphs is only s_i
  hb_set_unique_ptr except_segment = make_hb_set();
  hb_set_union(except_segment.get(), context.all_codepoints.get());
  hb_set_subtract(except_segment.get(), codepoints);
  auto B_except_segment_closure =
      TRY(context.GlyphClosure(except_segment.get()));

  hb_set_unique_ptr only_segment = make_hb_set();
  hb_set_union(only_segment.get(), context.initial_codepoints.get());
  hb_set_union(only_segment.get(), codepoints);
  auto I_only_segment_closure = TRY(context.GlyphClosure(only_segment.get()));
  hb_set_subtract(I_only_segment_closure.get(), context.initial_closure.get());

  hb_set_unique_ptr D_dropped = make_hb_set();
  hb_set_union(D_dropped.get(), context.full_closure.get());
  hb_set_subtract(D_dropped.get(), B_except_segment_closure.get());

  hb_set_union(and_gids, D_dropped.get());
  hb_set_subtract(and_gids, I_only_segment_closure.get());

  hb_set_union(or_gids, I_only_segment_closure.get());
  hb_set_subtract(or_gids, D_dropped.get());

  hb_set_union(exclusive_gids, I_only_segment_closure.get());
  hb_set_intersect(exclusive_gids, D_dropped.get());

  return absl::OkStatus();
}

Status AnalyzeSegment(SegmentationContext& context,
                      segment_index_t segment_index,
                      const hb_set_t* codepoints) {
  hb_set_unique_ptr and_gids = make_hb_set();
  hb_set_unique_ptr or_gids = make_hb_set();
  hb_set_unique_ptr exclusive_gids = make_hb_set();
  TRYV(AnalyzeSegment(context, codepoints, and_gids.get(), or_gids.get(),
                      exclusive_gids.get()));

  hb_codepoint_t and_gid = HB_SET_VALUE_INVALID;
  while (hb_set_next(exclusive_gids.get(), &and_gid)) {
    // TODO(garretrieger): if we are assigning an exclusive gid there should be
    // no other and segments, check and error if this is violated.
    hb_set_add(context.gid_conditions[and_gid].and_segments.get(),
               segment_index);
  }
  while (hb_set_next(and_gids.get(), &and_gid)) {
    hb_set_add(context.gid_conditions[and_gid].and_segments.get(),
               segment_index);
  }

  hb_codepoint_t or_gid = HB_SET_VALUE_INVALID;
  while (hb_set_next(or_gids.get(), &or_gid)) {
    hb_set_add(context.gid_conditions[or_gid].or_segments.get(), segment_index);
  }

  return absl::OkStatus();
}

Status GroupGlyphs(SegmentationContext& context) {
  btree_set<segment_index_t> fallback_segments_set;
  for (segment_index_t s = 0; s < context.segments.size(); s++) {
    if (hb_set_is_empty(context.segments[s].get())) {
      // Ignore empty segments.
      continue;
    }
    fallback_segments_set.insert(s);
  }

  for (glyph_id_t gid = 0; gid < context.gid_conditions.size(); gid++) {
    const auto& condition = context.gid_conditions[gid];
    if (!hb_set_is_empty(condition.and_segments.get())) {
      auto set = to_btree_set(condition.and_segments.get());
      context.and_glyph_groups[set].insert(gid);
    }
    if (!hb_set_is_empty(condition.or_segments.get())) {
      auto set = to_btree_set(condition.or_segments.get());
      context.or_glyph_groups[set].insert(gid);
    }

    if (hb_set_is_empty(condition.and_segments.get()) &&
        hb_set_is_empty(condition.or_segments.get()) &&
        !hb_set_has(context.initial_closure.get(), gid) &&
        hb_set_has(context.full_closure.get(), gid)) {
      context.unmapped_glyphs.insert(gid);
    }
  }

  // Any of the or_set conditions we've generated may have some additional
  // conditions that were not detected. Therefore we need to rule out the
  // presence of these additional conditions if an or group is able to be used.
  for (auto& [or_group, glyphs] : context.or_glyph_groups) {
    hb_set_unique_ptr all_other_codepoints = make_hb_set();
    hb_set_union(all_other_codepoints.get(), context.all_codepoints.get());
    for (uint32_t s : or_group) {
      hb_set_subtract(all_other_codepoints.get(), context.segments[s].get());
    }

    const hb_set_t* or_gids =
        TRY(context.CodepointsToOrGids(all_other_codepoints.get()));

    // Any "OR" glyphs associated with all other codepoints have some additional
    // conditions to activate so we can't safely include them into this or
    // condition. They are instead moved to the set of unmapped glyphs.
    uint32_t gid = HB_SET_VALUE_INVALID;
    while (hb_set_next(or_gids, &gid)) {
      if (glyphs.erase(gid) > 0) {
        context.unmapped_glyphs.insert(gid);
      }
    }
  }

  for (uint32_t gid : context.unmapped_glyphs) {
    // this glyph is not activated anywhere but is needed in the full closure
    // so add it to an activation condition of any segment.
    context.or_glyph_groups[fallback_segments_set].insert(gid);
  }

  context.fallback_segments = std::move(fallback_segments_set);

  return absl::OkStatus();
}

StatusOr<uint32_t> PatchSizeBytes(hb_face_t* original_face,
                                  const absl::btree_set<glyph_id_t>& gids) {
  FontData font_data(original_face);
  CompatId id;
  // Since this is just an estimate and we don't need ultra precise numbers run
  // at a lower brotli quality to improve performance.
  GlyphKeyedDiff diff(font_data, id, {FontHelper::kGlyf, FontHelper::kGvar}, 9);
  auto patch_data = TRY(diff.CreatePatch(gids));
  return patch_data.size();
}

void MergeSegments(const SegmentationContext& context, const hb_set_t* segments,
                   hb_set_t* base) {
  segment_index_t next = HB_SET_VALUE_INVALID;
  while (hb_set_next(segments, &next)) {
    const hb_set_t* codepoints = context.segments[next].get();
    hb_set_union(base, codepoints);
  }
}

StatusOr<uint32_t> EstimatePatchSize(SegmentationContext& context,
                                     const hb_set_t* codepoints) {
  hb_set_unique_ptr and_gids = make_hb_set();
  hb_set_unique_ptr or_gids = make_hb_set();
  hb_set_unique_ptr exclusive_gids = make_hb_set();
  TRYV(AnalyzeSegment(context, codepoints, and_gids.get(), or_gids.get(),
                      exclusive_gids.get()));

  auto btree_gids = to_btree_set(exclusive_gids.get());
  return PatchSizeBytes(context.original_face.get(), btree_gids);
}

StatusOr<bool> TryMerge(SegmentationContext& context,
                        segment_index_t base_segment_index,
                        const hb_set_t* segments) {
  // Create a merged segment, and remove all of the others
  hb_set_unique_ptr to_merge_segments = make_hb_set(hb_set_copy(segments));
  hb_set_del(to_merge_segments.get(), base_segment_index);

  uint32_t size_before =
      hb_set_get_population(context.segments[base_segment_index].get());
  hb_set_unique_ptr merged_codepoints = make_hb_set();
  hb_set_union(merged_codepoints.get(),
               context.segments[base_segment_index].get());
  MergeSegments(context, to_merge_segments.get(), merged_codepoints.get());

  uint32_t new_patch_size =
      TRY(EstimatePatchSize(context, merged_codepoints.get()));
  if (new_patch_size > context.patch_size_max_bytes) {
    return false;
  }

  hb_set_union(context.segments[base_segment_index].get(),
               merged_codepoints.get());
  uint32_t size_after =
      hb_set_get_population(context.segments[base_segment_index].get());

  VLOG(0) << "  Merged " << size_before << " codepoints up to " << size_after
          << " codepoints for segment " << base_segment_index
          << ". New patch size " << new_patch_size << " bytes.";

  segment_index_t segment_index = HB_SET_VALUE_INVALID;
  while (hb_set_next(to_merge_segments.get(), &segment_index)) {
    // To avoid changing the indices of other segments set the ones we're
    // removing to empty sets. That effectively disables them.
    hb_set_clear(context.segments[segment_index].get());
  }

  // Remove all segments we touched here from gid_conditions so they can be
  // recalculated.
  hb_set_add(to_merge_segments.get(), base_segment_index);
  for (auto& condition : context.gid_conditions) {
    condition.RemoveSegments(to_merge_segments.get());
  }

  return true;
}

/*
 * Search for a composite condition which can be merged into base_segment_index.
 *
 * Returns true if one was found and the merge succeeded, false otherwise.
 */
template <typename ConditionIt>
StatusOr<bool> TryMergingACompositeCondition(
    SegmentationContext& context,
    const GlyphSegmentation& candidate_segmentation,
    segment_index_t base_segment_index,
    patch_id_t base_patch,  // TODO XXX is base_patch needed?
    const ConditionIt& condition_it) {
  auto next_condition = condition_it;
  next_condition++;
  while (next_condition != candidate_segmentation.Conditions().end()) {
    if (next_condition->IsFallback()) {
      // Merging the fallback will cause all segments to be merged into one,
      // which is undesirable so don't consider the fallback.
      next_condition++;
      continue;
    }

    hb_set_unique_ptr triggering_segments = make_hb_set();
    next_condition->TriggeringSegments(triggering_segments.get());
    if (!hb_set_has(triggering_segments.get(), base_segment_index)) {
      next_condition++;
      continue;
    }

    if (!TRY(
            TryMerge(context, base_segment_index, triggering_segments.get()))) {
      next_condition++;
      continue;
    }

    VLOG(0) << "  Merging segments from composite patch into segment "
            << base_segment_index << ": " << next_condition->ToString();
    return true;
  }

  return false;
}

/*
 * Search for a base segment after base_segment_index which can be merged into
 * base_segment_index without exceeding the maximum patch size.
 *
 * Returns true if found and the merge suceeded.
 */
template <typename ConditionIt>
StatusOr<bool> TryMergingABaseSegment(
    SegmentationContext& context,
    const GlyphSegmentation& candidate_segmentation,
    segment_index_t base_segment_index, const ConditionIt& condition_it) {
  auto next_condition = condition_it;
  next_condition++;
  while (next_condition != candidate_segmentation.Conditions().end()) {
    if (!next_condition->IsExclusive()) {
      // Only interested in other base patches.
      next_condition++;
      continue;
    }

    hb_set_unique_ptr triggering_segments = make_hb_set();
    next_condition->TriggeringSegments(triggering_segments.get());

    if (!TRY(
            TryMerge(context, base_segment_index, triggering_segments.get()))) {
      next_condition++;
      continue;
    }

    VLOG(0) << "  Merging segments from base patch into segment "
            << base_segment_index << ": " << next_condition->ToString();
    return true;
  }

  return false;
}

StatusOr<bool> IsPatchTooSmall(SegmentationContext& context,
                               const GlyphSegmentation& candidate_segmentation,
                               segment_index_t base_segment_index,
                               patch_id_t base_patch) {
  auto patch_glyphs = candidate_segmentation.GidSegments().find(base_patch);
  if (patch_glyphs == candidate_segmentation.GidSegments().end()) {
    return absl::InternalError(StrCat("patch ", base_patch, " not found."));
  }
  uint32_t patch_size_bytes =
      TRY(PatchSizeBytes(context.original_face.get(), patch_glyphs->second));
  if (patch_size_bytes >= context.patch_size_min_bytes) {
    return false;
  }

  VLOG(0) << "Patch " << base_patch << " (segment " << base_segment_index
          << ") is too small "
          << "(" << patch_size_bytes << " < " << context.patch_size_min_bytes
          << "). Merging...";

  return true;
}

/*
 * Searches segments starting from start_segment for the next who's exclusive
 * gids patch is too small. If found, try increasing the size of the patch via
 * merging.
 *
 * If a merge was performed returns the segment which was modified to allow
 * groupings to be updated.
 */
StatusOr<std::optional<segment_index_t>> MergeNextBaseSegment(
    SegmentationContext& context,
    const GlyphSegmentation& candidate_segmentation, uint32_t start_segment) {
  hb_set_unique_ptr triggering_patches = make_hb_set();
  for (auto condition = candidate_segmentation.Conditions().begin();
       condition != candidate_segmentation.Conditions().end(); condition++) {
    if (!condition->IsExclusive()) {
      continue;
    }

    patch_id_t base_patch = condition->activated();
    segment_index_t base_segment_index =
        (*condition->conditions().begin()->begin());
    if (base_segment_index < start_segment) {
      // Already processed, skip
      continue;
    }

    if (!TRY(IsPatchTooSmall(context, candidate_segmentation,
                             base_segment_index, base_patch))) {
      continue;
    }

    if (TRY(TryMergingACompositeCondition(context, candidate_segmentation,
                                          base_segment_index, base_patch,
                                          condition))) {
      // Return to the parent method so it can reanalyze and reform groups
      return base_segment_index;
    }

    if (TRY(TryMergingABaseSegment(context, candidate_segmentation,
                                   base_segment_index, condition))) {
      // Return to the parent method so it can reanalyze and reform groups
      return base_segment_index;
    }

    VLOG(0) << "Unable to get segment " << base_segment_index
            << " above minimum size. Continuing to next segment.";
  }

  return std::nullopt;
}

/*
 * Ensures that the produce segmentation is:
 * - Disjoint (no duplicated glyphs) and doesn't overlap what's in the initial
 * font.
 * - Fully covers the full closure.
 */
Status ValidateSegmentation(const SegmentationContext& context,
                            const GlyphSegmentation& segementation) {
  hb_set_unique_ptr visited = make_hb_set();
  for (const auto& [id, gids] : segementation.GidSegments()) {
    for (glyph_id_t gid : gids) {
      if (hb_set_has(context.initial_closure.get(), gid)) {
        return absl::FailedPreconditionError(
            "Initial font glyph is present in a patch.");
      }
      if (hb_set_has(visited.get(), gid)) {
        return absl::FailedPreconditionError(
            "Glyph segments are not disjoint.");
      }
      hb_set_add(visited.get(), gid);
    }
  }

  hb_set_unique_ptr full_minus_initial = make_hb_set();
  hb_set_union(full_minus_initial.get(), context.full_closure.get());
  hb_set_subtract(full_minus_initial.get(), context.initial_closure.get());

  if (!hb_set_is_equal(full_minus_initial.get(), visited.get())) {
    return absl::FailedPreconditionError(
        "Not all glyphs in the full closure have been placed.");
  }

  return absl::OkStatus();
}

StatusOr<GlyphSegmentation> ClosureGlyphSegmenter::CodepointToGlyphSegments(
    hb_face_t* face, flat_hash_set<hb_codepoint_t> initial_segment,
    std::vector<flat_hash_set<hb_codepoint_t>> codepoint_segments,
    uint32_t patch_size_min_bytes, uint32_t patch_size_max_bytes) const {
  SegmentationContext context(face, initial_segment, codepoint_segments);
  context.patch_size_min_bytes = patch_size_min_bytes;
  context.patch_size_max_bytes = patch_size_max_bytes;

  VLOG(0) << "Forming initial segmentation plan.";
  segment_index_t segment_index = 0;
  for (const auto& segment : context.segments) {
    TRYV(AnalyzeSegment(context, segment_index, segment.get()));
    segment_index++;
  }
  context.LogClosureCount("Inital segment analysis");

  segment_index_t last_merged_segment_index = 0;
  while (true) {
    context.ResetGroupings();
    TRYV(GroupGlyphs(context));

    GlyphSegmentation segmentation(
        to_btree_set(context.initial_codepoints.get()),
        to_btree_set(context.initial_closure.get()), context.unmapped_glyphs);
    segmentation.CopySegments(context.segments);

    TRYV(GlyphSegmentation::GroupsToSegmentation(
        context.and_glyph_groups, context.or_glyph_groups,
        context.fallback_segments, segmentation));
    context.LogClosureCount("Condition grouping");

    if (patch_size_min_bytes == 0) {
      context.LogCacheStats();
      TRYV(ValidateSegmentation(context, segmentation));
      return segmentation;
    }

    auto merged = TRY(
        MergeNextBaseSegment(context, segmentation, last_merged_segment_index));
    if (!merged.has_value()) {
      // Nothing was merged so we're done.
      context.LogCacheStats();
      TRYV(ValidateSegmentation(context, segmentation));
      return segmentation;
    }

    last_merged_segment_index = *merged;
    VLOG(0) << "Re-analyzing segment " << last_merged_segment_index
            << " due to merge.";
    TRYV(AnalyzeSegment(context, last_merged_segment_index,
                        context.segments[last_merged_segment_index].get()));
  }

  return absl::InternalError("unreachable");
}

void GlyphSegmentation::CopySegments(
    const std::vector<hb_set_unique_ptr>& segments) {
  segments_.clear();
  for (const auto& set : segments) {
    segments_.push_back(to_btree_set(set.get()));
  }
}

}  // namespace ift::encoder