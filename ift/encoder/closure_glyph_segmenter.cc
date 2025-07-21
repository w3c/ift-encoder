#include "ift/encoder/closure_glyph_segmenter.h"

#include <cstdint>
#include <optional>
#include <vector>

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
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/segmentation_context.h"
#include "ift/encoder/subset_definition.h"
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
using common::hb_face_unique_ptr;
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
// - Can we reduce # of closures for the additional conditions checks?
//   - is the full analysis needed to get the or set?
// - Use merging and/or duplication to ensure minimum patch size.
//   - composite patches (NOT STARTED)
// - Multi segment combination testing with GSUB dep analysis to guide.

static StatusOr<uint32_t> EstimatePatchSizeBytes(hb_face_t* original_face,
                                                 const GlyphSet& gids);

static StatusOr<uint32_t> EstimatePatchSizeBytes(SegmentationContext& context,
                                                 const SegmentSet& segment_ids);

/*
 * Represents a potential merge operation that can be applied to the
 * SegmentationContext.
 */
struct CandidateMerge {
  // The segment into which other segments will be merged.
  segment_index_t base_segment_index;

  // The set of segments to be merged into the base_segment_index.
  common::SegmentSet segments_to_merge;

  // The result of merge the above segments.
  Segment merged_segment;

  // If true the merge segment will be inert, that is it won't interact
  // with the closure.
  bool new_segment_is_inert;

  // Estimated size of the patch after merging.
  uint32_t new_patch_size;

  // The estimated change overall cost of the segmentation if this merge
  // were to be appiled.
  double cost_delta;

  // The set of glyphs that would be invalidated (need reprocessing) if this
  // merge is applied.
  common::GlyphSet invalidated_glyphs;

  // Applies this merge operation to the given SegmentationContext.
  std::optional<GlyphSet> Apply(SegmentationContext& context) {
    const auto& segments = context.segmentation_info.Segments();
    uint32_t size_before =
        segments[base_segment_index].Definition().codepoints.size();
    uint32_t size_after = context.segmentation_info.AssignMergedSegment(
        base_segment_index, segments_to_merge, merged_segment);
    VLOG(0) << "  Merged " << size_before << " codepoints up to " << size_after
            << " codepoints for segment " << base_segment_index
            << ". New patch size " << new_patch_size << " bytes.";

    // Remove the fallback segment or group, it will be fully recomputed by
    // GroupGlyphs
    context.glyph_groupings.RemoveFallbackSegments(segments_to_merge);

    // Regardless of wether the new segment is inert all of the information
    // associated with the segments removed by the merge should be removed.
    context.InvalidateGlyphInformation(invalidated_glyphs, segments_to_merge);

    if (new_segment_is_inert) {
      // The newly formed segment will be inert which means we can construct the
      // new condition sets and glyph groupings here instead of using the
      // closure analysis to do it. The new segment is simply the union of all
      // glyphs associated with each segment that is part of the merge.
      // (gid_conditons_to_update)
      context.inert_segments.insert(base_segment_index);
      for (glyph_id_t gid : invalidated_glyphs) {
        context.glyph_condition_set.AddAndCondition(gid, base_segment_index);
      }
      context.glyph_groupings.AddGlyphsToExclusiveGroup(base_segment_index,
                                                        invalidated_glyphs);

      // We've now fully updated information for these glyphs so don't need to
      // return them.
      invalidated_glyphs.clear();
    } else {
      context.inert_segments.erase(base_segment_index);
    }

    return invalidated_glyphs;
  }

  static bool WouldMixFeaturesAndCodepoints(
      const RequestedSegmentationInformation& segment_info,
      segment_index_t base_segment_index, const SegmentSet& segments) {
    const auto& base = segment_info.Segments()[base_segment_index].Definition();
    bool base_codepoints_only =
        !base.codepoints.empty() && base.feature_tags.empty();
    bool base_features_only =
        base.codepoints.empty() && !base.feature_tags.empty();

    if (!base_codepoints_only && !base_features_only) {
      return false;
    }

    for (segment_index_t id : segments) {
      const auto& s = segment_info.Segments()[id].Definition();

      if (base_codepoints_only && !s.feature_tags.empty()) {
        return true;
      } else if (base_features_only && !s.codepoints.empty()) {
        return true;
      }
    }

    return false;
  }

  static void MergeSegments(
      const RequestedSegmentationInformation& segmentation_info,
      const SegmentSet& segments, Segment& base) {
    // Merged segments are activated disjunctively (s1 or ... or sn)
    //
    // We can compute the probability by first determining the probability
    // that none of the individual segments are matched (!s1 and ... and !sn)
    // and then inverting that to get the probability that at least one of the
    // individual segments was matched.
    //
    // This gives:
    // P(merged) = 1 - (1 - P(s1)) * ... * (1 - P(sn))
    double probability_not_matched = 1.0 - base.Probability();
    for (segment_index_t next : segments) {
      const auto& s = segmentation_info.Segments()[next];
      probability_not_matched *= 1.0 - s.Probability();
      base.Definition().Union(s.Definition());
    }
    base.SetProbability(1.0 - probability_not_matched);
  }

  static StatusOr<std::optional<CandidateMerge>> AssessMerge(
      SegmentationContext& context, segment_index_t base_segment_index,
      const SegmentSet& segments_to_merge_) {
    if (WouldMixFeaturesAndCodepoints(context.segmentation_info,
                                      base_segment_index, segments_to_merge_)) {
      // Because we don't yet have a good cost function for evaluating potential
      // mergers: the merger if it doesn't find a previous merge candidate will
      // try to merge together segments that are composed of codepoints with a
      // segment that adds an optional feature. Since this feature segments are
      // likely rarely used this will inflate the size of the patches for those
      // codepoint segments unnecessarily.
      //
      // So for now just don't merge cases where we would be combining codepoint
      // only segments with feature only segments.
      VLOG(0) << "  Merge would mix features into a codepoint only segment, "
                 "skipping.";
      return std::nullopt;
    }

    // Create a merged segment, and remove all of the others
    SegmentSet segments_to_merge = segments_to_merge_;
    SegmentSet segments_to_merge_with_base = segments_to_merge_;
    segments_to_merge.erase(base_segment_index);
    segments_to_merge_with_base.insert(base_segment_index);

    bool new_segment_is_inert =
        context.inert_segments.contains(base_segment_index) &&
        segments_to_merge.is_subset_of(context.inert_segments);
    if (new_segment_is_inert) {
      VLOG(0) << "  Merged segment will be inert, closure analysis will be "
                 "skipped.";
    }

    const auto& segments = context.segmentation_info.Segments();

    Segment merged_segment = segments[base_segment_index];
    MergeSegments(context.segmentation_info, segments_to_merge, merged_segment);

    GlyphSet gid_conditions_to_update;
    for (segment_index_t segment_index : segments_to_merge) {
      // segments which are being removed/changed may appear in gid_conditions,
      // we need to update those (and the down stream and/or glyph groups) to
      // reflect the removal/change and allow recalculation during the
      // GroupGlyphs steps
      //
      // Changes caused by adding new segments into the base segment will be
      // handled by the next AnalyzeSegment step.
      gid_conditions_to_update.union_set(
          context.glyph_condition_set.GlyphsWithSegment(segment_index));
    }

    uint32_t new_patch_size = 0;
    if (!new_segment_is_inert) {
      new_patch_size =
          TRY(EstimatePatchSizeBytes(context, segments_to_merge_with_base));
    } else {
      // For inert patches we can precompute the glyph set saving a closure
      // operation
      GlyphSet merged_glyphs = gid_conditions_to_update;
      merged_glyphs.union_set(
          context.glyph_condition_set.GlyphsWithSegment(base_segment_index));
      new_patch_size = TRY(
          EstimatePatchSizeBytes(context.original_face.get(), merged_glyphs));
    }
    if (new_patch_size > context.patch_size_max_bytes) {
      return std::nullopt;
    }

    return CandidateMerge{.base_segment_index = base_segment_index,
                          .segments_to_merge = segments_to_merge,
                          .merged_segment = std::move(merged_segment),
                          .new_segment_is_inert = new_segment_is_inert,
                          .new_patch_size = new_patch_size,
                          .cost_delta = 0.0,  // TODO XXXX compute this.
                          .invalidated_glyphs = gid_conditions_to_update};
  }
};

// Calculates the estimated size of a patch for original_face which includes
// 'gids'.
//
// Will typically overestimate the size since we use a faster, but less
// effective version of brotli (quality 9 instead of 11) to generate the
// estimate.
StatusOr<uint32_t> EstimatePatchSizeBytes(hb_face_t* original_face,
                                          const GlyphSet& gids) {
  FontData font_data(original_face);
  CompatId id;
  // Since this is just an estimate and we don't need ultra precise numbers run
  // at a lower brotli quality to improve performance.
  GlyphKeyedDiff diff(font_data, id,
                      {FontHelper::kGlyf, FontHelper::kGvar, FontHelper::kCFF,
                       FontHelper::kCFF2},
                      8);

  auto patch_data = TRY(diff.CreatePatch(gids));
  return patch_data.size();
}

// Calculates the estimated size of a patch which includes all glyphs exclusive
// to the listed codepoints. Will typically overestimate the size since we use
// a faster, but less effective version of brotli to generate the estimate.
StatusOr<uint32_t> EstimatePatchSizeBytes(SegmentationContext& context,
                                          const SegmentSet& segment_ids) {
  GlyphSet and_gids;
  GlyphSet or_gids;
  GlyphSet exclusive_gids;
  TRYV(context.AnalyzeSegment(segment_ids, and_gids, or_gids, exclusive_gids));
  return EstimatePatchSizeBytes(context.original_face.get(), exclusive_gids);
}

// Attempt to merge to_merge_semgents into base_segment_index. If maximum
// patch size would be exceeded does not merge and returns nullopt.
//
// Otherwise the segment definitions are merged and any affected downstream info
// (glyph conditions and glyph groupings) are invalidated. The set of
// invalidated glyph ids is returned.
StatusOr<std::optional<GlyphSet>> TryMerge(
    SegmentationContext& context, segment_index_t base_segment_index,
    const SegmentSet& to_merge_segments_) {
  auto maybe_candidate_merge = TRY(CandidateMerge::AssessMerge(
      context, base_segment_index, to_merge_segments_));
  if (!maybe_candidate_merge.has_value()) {
    return std::nullopt;
  }
  auto& candidate_merge = maybe_candidate_merge.value();
  // TODO XXXXX
  // - Then above this function we should iterate through all candidate merges,
  // generating
  //   a list of proposed merges which are then sorted by cost and the lowest
  //   negative cost option is selected and applied (positive cost options
  //   should be thrown out)
  // - When estimating cost deltas we can collect the set of glyphs in the newly
  // merged patch
  //   and exclude those from all other non merged patches to generate a rough
  //   estimate of their new size.
  // - caches that may be useful: glyph set -> patch size, condition ->
  // probability
  return candidate_merge.Apply(context);
}

/*
 * Search for a composite condition which can be merged into base_segment_index.
 *
 * Returns the set of glyphs invalidated by the merge if found and the merge
 * suceeded.
 */
StatusOr<std::optional<GlyphSet>> TryMergingACompositeCondition(
    SegmentationContext& context, segment_index_t base_segment_index,
    const ActivationCondition& base_condition) {
  auto candidate_conditions =
      context.glyph_groupings.TriggeringSegmentToConditions(base_segment_index);
  for (ActivationCondition next_condition : candidate_conditions) {
    if (next_condition.IsFallback()) {
      // Merging the fallback will cause all segments to be merged into one,
      // which is undesirable so don't consider the fallback.
      continue;
    }

    if (next_condition < base_condition || next_condition == base_condition) {
      // all conditions before base_condition are already processed, so we only
      // want to search after base_condition.
      continue;
    }

    SegmentSet triggering_segments = next_condition.TriggeringSegments();
    if (!triggering_segments.contains(base_segment_index)) {
      continue;
    }

    auto modified_gids =
        TRY(TryMerge(context, base_segment_index, triggering_segments));
    if (!modified_gids.has_value()) {
      continue;
    }

    VLOG(0) << "  Merging segments from composite patch into segment "
            << base_segment_index << ": " << next_condition.ToString();
    return modified_gids;
  }

  return std::nullopt;
}

/*
 * Search for a base segment after base_segment_index which can be merged into
 * base_segment_index without exceeding the maximum patch size.
 *
 * Returns the set of glyphs invalidated by the merge if found and the merge
 * suceeded.
 */
template <typename ConditionAndGlyphIt>
StatusOr<std::optional<GlyphSet>> TryMergingABaseSegment(
    SegmentationContext& context, segment_index_t base_segment_index,
    const ConditionAndGlyphIt& condition_it) {
  // TODO(garretrieger): this currently merges at most one segment at a time
  //  into base. we could likely significantly improve performance (ie.
  //  reducing number of closure and brotli ops) by choosing multiple segments
  //  at once if it seems likely the new patch size will be within the
  //  thresholds. A rough estimate of patch size can be generated by summing the
  //  individual patch sizes of the existing patches for each segment. Finally
  //  we can run the merge, and check if the actual patch size is within bounds.
  //
  //  As part of this we should start caching patch size results so the
  //  individual patch sizes don't need to be recomputed later on.
  auto next_condition_it = condition_it;
  next_condition_it++;
  while (next_condition_it !=
         context.glyph_groupings.ConditionsAndGlyphs().end()) {
    auto next_condition = next_condition_it->first;
    if (!next_condition.IsExclusive()) {
      // Only interested in other base patches.
      next_condition_it++;
      continue;
    }

    SegmentSet triggering_segments = next_condition.TriggeringSegments();

    auto modified_gids =
        TRY(TryMerge(context, base_segment_index, triggering_segments));
    if (!modified_gids.has_value()) {
      next_condition_it++;
      continue;
    }

    VLOG(0) << "  Merging segments from base patch into segment "
            << base_segment_index << ": " << next_condition.ToString();
    return modified_gids;
  }

  return std::nullopt;
}

// Computes the estimated size of the patch for a segment and returns true if it
// is below the minimum.
StatusOr<bool> IsPatchTooSmall(SegmentationContext& context,
                               segment_index_t base_segment_index,
                               const GlyphSet& glyphs) {
  uint32_t patch_size_bytes =
      TRY(EstimatePatchSizeBytes(context.original_face.get(), glyphs));
  if (patch_size_bytes >= context.patch_size_min_bytes) {
    return false;
  }

  VLOG(0) << "Patch for segment " << base_segment_index << " is too small "
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
StatusOr<std::optional<std::pair<segment_index_t, GlyphSet>>>
MergeNextBaseSegment(SegmentationContext& context, uint32_t start_segment) {
  auto start_condition =
      ActivationCondition::exclusive_segment(start_segment, 0);
  for (auto it = context.glyph_groupings.ConditionsAndGlyphs().lower_bound(
           start_condition);
       it != context.glyph_groupings.ConditionsAndGlyphs().end(); it++) {
    const auto& condition = it->first;
    if (!condition.IsExclusive()) {
      continue;
    }

    segment_index_t base_segment_index =
        (*condition.conditions().begin()->begin());
    if (base_segment_index < start_segment) {
      // Already processed, skip
      continue;
    }

    if (!TRY(IsPatchTooSmall(context, base_segment_index, it->second))) {
      continue;
    }

    auto modified_gids = TRY(
        TryMergingACompositeCondition(context, base_segment_index, condition));
    if (modified_gids.has_value()) {
      // Return to the parent method so it can reanalyze and reform groups
      return std::pair(base_segment_index, *modified_gids);
    }

    modified_gids =
        TRY(TryMergingABaseSegment(context, base_segment_index, it));
    if (modified_gids.has_value()) {
      // Return to the parent method so it can reanalyze and reform groups
      return std::pair(base_segment_index, *modified_gids);
    }

    VLOG(0) << "Unable to get segment " << base_segment_index
            << " above minimum size. Continuing to next segment.";
  }

  return std::nullopt;
}

/*
 * Checks that the incrementally generated glyph conditions and groupings in
 * context match what would have been produced by a non incremental process.
 *
 * Returns OkStatus() if they match.
 */
Status ValidateIncrementalGroupings(hb_face_t* face,
                                    const SegmentationContext& context) {
  SegmentationContext non_incremental_context(
      face, context.segmentation_info.InitFontSegment(),
      context.segmentation_info.Segments());
  non_incremental_context.patch_size_min_bytes = 0;
  non_incremental_context.patch_size_max_bytes = UINT32_MAX;

  // Compute the glyph groupings/conditions from scratch to compare against the
  // incrementall produced ones.
  for (segment_index_t segment_index = 0;
       segment_index < context.segmentation_info.Segments().size();
       segment_index++) {
    TRY(non_incremental_context.ReprocessSegment(segment_index));
  }

  GlyphSet all_glyphs;
  uint32_t glyph_count = hb_face_get_glyph_count(face);
  all_glyphs.insert_range(0, glyph_count - 1);
  TRYV(non_incremental_context.GroupGlyphs(all_glyphs));

  if (non_incremental_context.glyph_groupings.ConditionsAndGlyphs() !=
      context.glyph_groupings.ConditionsAndGlyphs()) {
    return absl::FailedPreconditionError(
        "conditions_and_glyphs aren't correct.");
  }

  if (non_incremental_context.glyph_condition_set !=
      context.glyph_condition_set) {
    return absl::FailedPreconditionError("glyph_condition_set isn't correct.");
  }

  if (non_incremental_context.glyph_groupings.AndGlyphGroups() !=
      context.glyph_groupings.AndGlyphGroups()) {
    return absl::FailedPreconditionError("and_glyph groups aren't correct.");
  }

  if (non_incremental_context.glyph_groupings.OrGlyphGroups() !=
      context.glyph_groupings.OrGlyphGroups()) {
    return absl::FailedPreconditionError("or_glyph groups aren't correct.");
  }

  return absl::OkStatus();
}

StatusOr<GlyphSegmentation> ClosureGlyphSegmenter::CodepointToGlyphSegments(
    hb_face_t* face, SubsetDefinition initial_segment,
    std::vector<Segment> codepoint_segments, uint32_t patch_size_min_bytes,
    uint32_t patch_size_max_bytes) const {
  uint32_t glyph_count = hb_face_get_glyph_count(face);
  if (!glyph_count) {
    return absl::InvalidArgumentError("Provided font has no glyphs.");
  }

  // The IFT compiler has a set of defaults always included in the initial font
  // add them here so we correctly factor them into the generated segmentation.
  AddInitSubsetDefaults(initial_segment);

  SegmentationContext context(face, initial_segment, codepoint_segments);
  context.patch_size_min_bytes = patch_size_min_bytes;
  context.patch_size_max_bytes = patch_size_max_bytes;

  // ### Generate the initial conditions and groupings by processing all
  // segments and glyphs. ###
  VLOG(0) << "Forming initial segmentation plan.";
  for (segment_index_t segment_index = 0;
       segment_index < context.segmentation_info.Segments().size();
       segment_index++) {
    TRY(context.ReprocessSegment(segment_index));
  }
  context.glyph_closure_cache.LogClosureCount("Inital segment analysis");

  GlyphSet all_glyphs;
  all_glyphs.insert_range(0, glyph_count - 1);
  TRYV(context.GroupGlyphs(all_glyphs));
  context.glyph_closure_cache.LogClosureCount("Condition grouping");

  if (patch_size_min_bytes == 0) {
    // No merging will be needed so we're done.
    return context.ToGlyphSegmentation();
  }

  // ### Iteratively merge segments and incrementally reprocess affected data.
  segment_index_t last_merged_segment_index = 0;
  while (true) {
    auto merged = TRY(MergeNextBaseSegment(context, last_merged_segment_index));
    if (!merged.has_value()) {
      // Nothing was merged so we're done.
      TRYV(ValidateIncrementalGroupings(face, context));
      return context.ToGlyphSegmentation();
    }

    const auto& [merged_segment_index, modified_gids] = *merged;
    last_merged_segment_index = merged_segment_index;

    GlyphSet analysis_modified_gids;
    if (!context.inert_segments.contains(last_merged_segment_index)) {
      VLOG(0) << "Re-analyzing segment " << last_merged_segment_index
              << " due to merge.";
      analysis_modified_gids =
          TRY(context.ReprocessSegment(last_merged_segment_index));
    }
    analysis_modified_gids.union_set(modified_gids);

    TRYV(context.GroupGlyphs(analysis_modified_gids));

    context.glyph_closure_cache.LogClosureCount("Condition grouping");
  }

  return absl::InternalError("unreachable");
}

}  // namespace ift::encoder
