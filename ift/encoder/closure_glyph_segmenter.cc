#include "ift/encoder/closure_glyph_segmenter.h"

#include <cstdint>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/compat_id.h"
#include "common/font_data.h"
#include "common/font_helper.h"
#include "common/hb_set_unique_ptr.h"
#include "common/int_set.h"
#include "common/try.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/glyph_keyed_diff.h"

using absl::btree_map;
using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::node_hash_map;
using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using common::CodepointSet;
using common::CompatId;
using common::FontData;
using common::FontHelper;
using common::GlyphSet;
using common::hb_set_unique_ptr;
using common::IntSet;
using common::make_hb_face;
using common::make_hb_set;
using common::SegmentSet;
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
  GlyphConditions() : and_segments(), or_segments() {}
  SegmentSet and_segments;
  SegmentSet or_segments;

  void RemoveSegments(const SegmentSet& segments) {
    and_segments.subtract(segments);
    or_segments.subtract(segments);
  }
};

class SegmentationContext;

Status AnalyzeSegment(SegmentationContext& context,
                      const CodepointSet& codepoints, GlyphSet& and_gids,
                      GlyphSet& or_gids, GlyphSet& exclusive_gids);

class SegmentationContext {
 public:
  SegmentationContext(hb_face_t* face, const CodepointSet& initial_segment,
                      const std::vector<CodepointSet>& codepoint_segments)
      : preprocessed_face(make_hb_face(hb_subset_preprocess(face))),
        original_face(make_hb_face(hb_face_reference(face))),
        segmentation(),
        all_codepoints(),
        full_closure() {
    segmentation.InitialFontCodepoints().union_set(initial_segment);
    all_codepoints.union_set(initial_segment);

    segmentation.CopySegments(codepoint_segments);
    for (const auto& s : codepoint_segments) {
      all_codepoints.union_set(s);
    }

    {
      auto closure = GlyphClosure(initial_segment);
      if (closure.ok()) {
        segmentation.InitialFontGlyphs() = std::move(*closure);
      }
    }

    auto closure = GlyphClosure(all_codepoints);
    if (closure.ok()) {
      full_closure = std::move(*closure);
    }

    gid_conditions.resize(hb_face_get_glyph_count(original_face.get()));
  }

  void ResetGroupings() {
    segmentation.UnmappedGlyphs() = {};
    and_glyph_groups = {};
    or_glyph_groups = {};
    fallback_segments = {};
  }

  StatusOr<GlyphSet> GlyphClosure(const CodepointSet& codepoints) {
    auto it = glyph_closure_cache.find(codepoints);
    if (it != glyph_closure_cache.end()) {
      glyph_closure_cache_hit++;
      return it->second;
    }

    glyph_closure_cache_miss++;
    closure_count_cumulative++;
    closure_count_delta++;

    hb_subset_input_t* input = hb_subset_input_create_or_fail();
    if (!input) {
      return absl::InternalError("Closure subset configuration failed.");
    }

    codepoints.union_into(hb_subset_input_unicode_set(input));
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

    glyph_closure_cache.insert(std::pair(codepoints, GlyphSet(gids)));

    return GlyphSet(gids);
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

  StatusOr<const GlyphSet*> CodepointsToOrGids(const CodepointSet& codepoints) {
    auto it = code_point_set_to_or_gids_cache.find(codepoints);
    if (it != code_point_set_to_or_gids_cache.end()) {
      code_point_set_to_or_gids_cache_hit++;
      return &it->second;
    }

    code_point_set_to_or_gids_cache_miss++;
    GlyphSet and_gids;
    GlyphSet or_gids;
    GlyphSet exclusive_gids;
    TRYV(AnalyzeSegment(*this, codepoints, and_gids, or_gids, exclusive_gids));

    auto [new_value, inserted] =
        code_point_set_to_or_gids_cache.insert(std::pair(codepoints, or_gids));
    return &new_value->second;
  }

  // Init
  common::hb_face_unique_ptr preprocessed_face;
  common::hb_face_unique_ptr original_face;
  GlyphSegmentation segmentation;

  CodepointSet all_codepoints;
  GlyphSet full_closure;

  uint32_t patch_size_min_bytes = 0;
  uint32_t patch_size_max_bytes = UINT32_MAX;

  // Phase 1
  flat_hash_map<uint32_t, GlyphSet>
      segment_to_gid_conditions;  // tracks which gid conditions reference a
                                  // segment.
  std::vector<GlyphConditions> gid_conditions;

  // Phase 2
  btree_map<SegmentSet, GlyphSet> and_glyph_groups;
  btree_map<SegmentSet, GlyphSet> or_glyph_groups;
  SegmentSet fallback_segments;

  // Caches and logging
  flat_hash_map<CodepointSet, GlyphSet> glyph_closure_cache;
  uint32_t glyph_closure_cache_hit = 0;
  uint32_t glyph_closure_cache_miss = 0;

  // for this cache we return pointers to the sets so need node_hash_map for
  // pointer stability.
  node_hash_map<CodepointSet, GlyphSet> code_point_set_to_or_gids_cache;
  uint32_t code_point_set_to_or_gids_cache_hit = 0;
  uint32_t code_point_set_to_or_gids_cache_miss = 0;

  uint32_t closure_count_cumulative = 0;
  uint32_t closure_count_delta = 0;
};

Status AnalyzeSegment(SegmentationContext& context,
                      const CodepointSet& codepoints, GlyphSet& and_gids,
                      GlyphSet& or_gids, GlyphSet& exclusive_gids) {
  if (codepoints.empty()) {
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
  CodepointSet except_segment = context.all_codepoints;
  except_segment.subtract(codepoints);
  auto B_except_segment_closure = TRY(context.GlyphClosure(except_segment));

  CodepointSet only_segment = context.segmentation.InitialFontCodepoints();
  only_segment.union_set(codepoints);
  auto I_only_segment_closure = TRY(context.GlyphClosure(only_segment));
  I_only_segment_closure.subtract(context.segmentation.InitialFontGlyphs());

  GlyphSet D_dropped = context.full_closure;
  D_dropped.subtract(B_except_segment_closure);

  and_gids.union_set(D_dropped);
  and_gids.subtract(I_only_segment_closure);

  or_gids.union_set(I_only_segment_closure);
  or_gids.subtract(D_dropped);

  exclusive_gids.union_set(I_only_segment_closure);
  exclusive_gids.intersect(D_dropped);

  return absl::OkStatus();
}

Status AnalyzeSegment(SegmentationContext& context,
                      segment_index_t segment_index,
                      const CodepointSet& codepoints) {
  GlyphSet and_gids;
  GlyphSet or_gids;
  GlyphSet exclusive_gids;
  TRYV(AnalyzeSegment(context, codepoints, and_gids, or_gids, exclusive_gids));

  for (uint32_t and_gid : exclusive_gids) {
    // TODO(garretrieger): if we are assigning an exclusive gid there should be
    // no other and segments, check and error if this is violated.
    context.gid_conditions[and_gid].and_segments.insert(segment_index);
    context.segment_to_gid_conditions[segment_index].insert(and_gid);
  }

  for (uint32_t and_gid : and_gids) {
    context.gid_conditions[and_gid].and_segments.insert(segment_index);
    context.segment_to_gid_conditions[segment_index].insert(and_gid);
  }

  for (uint32_t or_gid : or_gids) {
    context.gid_conditions[or_gid].or_segments.insert(segment_index);
    context.segment_to_gid_conditions[segment_index].insert(or_gid);
  }

  return absl::OkStatus();
}

Status GroupGlyphs(SegmentationContext& context) {
  SegmentSet fallback_segments_set;
  for (segment_index_t s = 0; s < context.segmentation.Segments().size(); s++) {
    if (context.segmentation.Segments()[s].empty()) {
      // Ignore empty segments.
      continue;
    }
    fallback_segments_set.insert(s);
  }

  const auto& initial_closure = context.segmentation.InitialFontGlyphs();
  auto& unmapped_glyphs = context.segmentation.UnmappedGlyphs();
  for (glyph_id_t gid = 0; gid < context.gid_conditions.size(); gid++) {
    const auto& condition = context.gid_conditions[gid];
    if (!condition.and_segments.empty()) {
      context.and_glyph_groups[condition.and_segments].insert(gid);
    }
    if (!condition.or_segments.empty()) {
      context.or_glyph_groups[condition.or_segments].insert(gid);
    }

    if (condition.and_segments.empty() && condition.or_segments.empty() &&
        !initial_closure.contains(gid) && context.full_closure.contains(gid)) {
      unmapped_glyphs.insert(gid);
    }
  }

  // Any of the or_set conditions we've generated may have some additional
  // conditions that were not detected. Therefore we need to rule out the
  // presence of these additional conditions if an or group is able to be used.
  for (auto& [or_group, glyphs] : context.or_glyph_groups) {
    CodepointSet all_other_codepoints = context.all_codepoints;
    for (uint32_t s : or_group) {
      all_other_codepoints.subtract(context.segmentation.Segments()[s]);
    }

    const GlyphSet* or_gids =
        TRY(context.CodepointsToOrGids(all_other_codepoints));

    // Any "OR" glyphs associated with all other codepoints have some additional
    // conditions to activate so we can't safely include them into this or
    // condition. They are instead moved to the set of unmapped glyphs.
    for (uint32_t gid : *or_gids) {
      if (glyphs.erase(gid) > 0) {
        unmapped_glyphs.insert(gid);
      }
    }
  }

  for (uint32_t gid : unmapped_glyphs) {
    // this glyph is not activated anywhere but is needed in the full closure
    // so add it to an activation condition of any segment.
    context.or_glyph_groups[fallback_segments_set].insert(gid);
  }

  context.fallback_segments = std::move(fallback_segments_set);

  return absl::OkStatus();
}

StatusOr<uint32_t> PatchSizeBytes(hb_face_t* original_face,
                                  const IntSet& gids) {
  FontData font_data(original_face);
  CompatId id;
  // Since this is just an estimate and we don't need ultra precise numbers run
  // at a lower brotli quality to improve performance.
  GlyphKeyedDiff diff(font_data, id,
                      {FontHelper::kGlyf, FontHelper::kGvar, FontHelper::kCFF,
                       FontHelper::kCFF2},
                      9);
  auto patch_data = TRY(diff.CreatePatch(gids));
  return patch_data.size();
}

void MergeSegments(const SegmentationContext& context, const IntSet& segments,
                   IntSet& base) {
  for (uint32_t next : segments) {
    base.union_set(context.segmentation.Segments()[next]);
  }
}

StatusOr<uint32_t> EstimatePatchSize(SegmentationContext& context,
                                     const CodepointSet& codepoints) {
  GlyphSet and_gids;
  GlyphSet or_gids;
  GlyphSet exclusive_gids;
  TRYV(AnalyzeSegment(context, codepoints, and_gids, or_gids, exclusive_gids));
  return PatchSizeBytes(context.original_face.get(), exclusive_gids);
}

StatusOr<bool> TryMerge(SegmentationContext& context,
                        segment_index_t base_segment_index,
                        const SegmentSet& to_merge_segments_) {
  // Create a merged segment, and remove all of the others
  SegmentSet to_merge_segments = to_merge_segments_;
  to_merge_segments.erase(base_segment_index);

  auto& segments = context.segmentation.Segments();
  uint32_t size_before = segments[base_segment_index].size();

  CodepointSet merged_codepoints = segments[base_segment_index];
  MergeSegments(context, to_merge_segments, merged_codepoints);

  uint32_t new_patch_size = TRY(EstimatePatchSize(context, merged_codepoints));
  if (new_patch_size > context.patch_size_max_bytes) {
    return false;
  }

  segments[base_segment_index].union_set(merged_codepoints);
  uint32_t size_after = segments[base_segment_index].size();

  VLOG(0) << "  Merged " << size_before << " codepoints up to " << size_after
          << " codepoints for segment " << base_segment_index
          << ". New patch size " << new_patch_size << " bytes.";

  for (segment_index_t segment_index : to_merge_segments) {
    // To avoid changing the indices of other segments set the ones we're
    // removing to empty sets. That effectively disables them.
    segments[segment_index].clear();
  }

  // Remove all segments we touched here from gid_conditions so they can be
  // recalculated.
  to_merge_segments.insert(base_segment_index);
  GlyphSet gid_conditions_to_update;
  for (uint32_t segment_index : to_merge_segments) {
    auto it = context.segment_to_gid_conditions.find(segment_index);
    if (it != context.segment_to_gid_conditions.end()) {
      gid_conditions_to_update.union_set(it->second);
    }
  }

  for (uint32_t condition_index : gid_conditions_to_update) {
    auto& condition = context.gid_conditions[condition_index];
    condition.RemoveSegments(to_merge_segments);
  }

  for (uint32_t segment_index : to_merge_segments) {
    context.segment_to_gid_conditions[segment_index].subtract(
        gid_conditions_to_update);
  }

  return true;
}

/*
 * Search for a composite condition which can be merged into base_segment_index.
 *
 * Returns true if one was found and the merge succeeded, false otherwise.
 */
StatusOr<bool> TryMergingACompositeCondition(
    SegmentationContext& context, segment_index_t base_segment_index,
    patch_id_t base_patch,  // TODO XXX is base_patch needed?
    const GlyphSegmentation::ActivationCondition& base_condition) {

  auto candidate_conditions = context.segmentation.TriggeringSegmentToConditions(base_segment_index);
  for (const auto* next_condition : candidate_conditions) {
    if (next_condition->IsFallback()) {
      // Merging the fallback will cause all segments to be merged into one,
      // which is undesirable so don't consider the fallback.
      continue;
    }

    if (*next_condition < base_condition || *next_condition == base_condition) {
      // all conditions before base_condition are already processed, so we only want to search after base_condition.
      continue;
    }

    SegmentSet triggering_segments = next_condition->TriggeringSegments();
    if (!triggering_segments.contains(base_segment_index)) {
      next_condition++;
      continue;
    }

    if (!TRY(TryMerge(context, base_segment_index, triggering_segments))) {
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
StatusOr<bool> TryMergingABaseSegment(SegmentationContext& context,
                                      segment_index_t base_segment_index,
                                      const ConditionIt& condition_it) {
  auto next_condition = condition_it;
  next_condition++;
  // TODO XXXXXX use an index to locate potential segments instead of a full scan.
  while (next_condition != context.segmentation.Conditions().end()) {
    if (!next_condition->IsExclusive()) {
      // Only interested in other base patches.
      next_condition++;
      continue;
    }

    SegmentSet triggering_segments = next_condition->TriggeringSegments();
    if (!TRY(TryMerge(context, base_segment_index, triggering_segments))) {
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
                               segment_index_t base_segment_index,
                               patch_id_t base_patch) {
  auto patch_glyphs = context.segmentation.GidSegments().find(base_patch);
  if (patch_glyphs == context.segmentation.GidSegments().end()) {
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
    SegmentationContext& context, uint32_t start_segment) {
  // TODO XXXX don't scan conditions before start_segment
  for (auto condition = context.segmentation.Conditions().begin();
       condition != context.segmentation.Conditions().end(); condition++) {
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

    if (!TRY(IsPatchTooSmall(context, base_segment_index, base_patch))) {
      continue;
    }

    if (TRY(TryMergingACompositeCondition(context, base_segment_index,
                                          base_patch, *condition))) {
      // Return to the parent method so it can reanalyze and reform groups
      return base_segment_index;
    }

    if (TRY(TryMergingABaseSegment(context, base_segment_index, condition))) {
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
Status ValidateSegmentation(const SegmentationContext& context) {
  IntSet visited;
  const auto& initial_closure = context.segmentation.InitialFontGlyphs();
  for (const auto& [id, gids] : context.segmentation.GidSegments()) {
    for (glyph_id_t gid : gids) {
      if (initial_closure.contains(gid)) {
        return absl::FailedPreconditionError(
            "Initial font glyph is present in a patch.");
      }
      if (visited.contains(gid)) {
        return absl::FailedPreconditionError(
            "Glyph segments are not disjoint.");
      }
      visited.insert(gid);
    }
  }

  IntSet full_minus_initial = context.full_closure;
  full_minus_initial.subtract(initial_closure);

  if (full_minus_initial != visited) {
    return absl::FailedPreconditionError(
        "Not all glyphs in the full closure have been placed.");
  }

  return absl::OkStatus();
}

StatusOr<GlyphSegmentation> ClosureGlyphSegmenter::CodepointToGlyphSegments(
    hb_face_t* face, CodepointSet initial_segment,
    std::vector<CodepointSet> codepoint_segments, uint32_t patch_size_min_bytes,
    uint32_t patch_size_max_bytes) const {
  SegmentationContext context(face, initial_segment, codepoint_segments);
  context.patch_size_min_bytes = patch_size_min_bytes;
  context.patch_size_max_bytes = patch_size_max_bytes;

  VLOG(0) << "Forming initial segmentation plan.";
  segment_index_t segment_index = 0;
  for (const auto& segment : context.segmentation.Segments()) {
    TRYV(AnalyzeSegment(context, segment_index, segment));
    segment_index++;
  }
  context.LogClosureCount("Inital segment analysis");

  TRYV(GroupGlyphs(context));
  TRYV(GlyphSegmentation::GroupsToSegmentation(
      context.and_glyph_groups, context.or_glyph_groups,
      context.fallback_segments, context.segmentation));

  context.LogClosureCount("Condition grouping");

  segment_index_t last_merged_segment_index = 0;
  while (true) {
    if (patch_size_min_bytes == 0) {
      context.LogCacheStats();
      TRYV(ValidateSegmentation(context));
      return context.segmentation;
    }

    auto merged = TRY(MergeNextBaseSegment(context, last_merged_segment_index));
    if (!merged.has_value()) {
      // Nothing was merged so we're done.
      context.LogCacheStats();
      TRYV(ValidateSegmentation(context));
      return context.segmentation;
    }

    last_merged_segment_index = *merged;
    VLOG(0) << "Re-analyzing segment " << last_merged_segment_index
            << " due to merge.";
    TRYV(AnalyzeSegment(
        context, last_merged_segment_index,
        context.segmentation.Segments()[last_merged_segment_index]));

    // TODO XXXXXX
    // at this point we've updated the segment sets, and gid_conditions, what
    // remains is
    //  - The work that GroupGlyphs would do: Unmapped Glyphs, and glyph groups,
    //  or glyph groups, fallback segments
    //  - and the work that GroupsToSegmentation does: patches and conditions.
    context.ResetGroupings();
    TRYV(GroupGlyphs(context));
    TRYV(GlyphSegmentation::GroupsToSegmentation(
        context.and_glyph_groups, context.or_glyph_groups,
        context.fallback_segments, context.segmentation));

    context.LogClosureCount("Condition grouping");
  }

  return absl::InternalError("unreachable");
}

}  // namespace ift::encoder
