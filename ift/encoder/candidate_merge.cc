#include "ift/encoder/candidate_merge.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/compat_id.h"
#include "common/font_data.h"
#include "common/font_helper.h"
#include "common/int_set.h"
#include "common/try.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/glyph_groupings.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/segmentation_context.h"
#include "ift/glyph_keyed_diff.h"

namespace ift::encoder {

using absl::btree_map;
using absl::Status;
using absl::StatusOr;
using common::CodepointSet;
using common::CompatId;
using common::FontData;
using common::FontHelper;
using common::GlyphSet;
using common::SegmentSet;
using ift::GlyphKeyedDiff;

// Calculates the estimated size of a patch for original_face which includes
// 'gids'.
//
// Will typically overestimate the size since we use a faster, but less
// effective version of brotli (quality 9 instead of 11) to generate the
// estimate.
static StatusOr<uint32_t> EstimatePatchSizeBytes(hb_face_t* original_face,
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
static StatusOr<uint32_t> EstimatePatchSizeBytes(
    SegmentationContext& context, const SegmentSet& segment_ids) {
  GlyphSet and_gids;
  GlyphSet or_gids;
  GlyphSet exclusive_gids;
  TRYV(context.AnalyzeSegment(segment_ids, and_gids, or_gids, exclusive_gids));
  return EstimatePatchSizeBytes(context.original_face.get(), exclusive_gids);
}

StatusOr<bool> CandidateMerge::IsPatchTooSmall(
    SegmentationContext& context, segment_index_t base_segment_index,
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

std::optional<GlyphSet> CandidateMerge::Apply(SegmentationContext& context) {
  const auto& segments = context.segmentation_info.Segments();
  uint32_t size_before =
      segments[base_segment_index].Definition().codepoints.size();
  uint32_t size_after = context.segmentation_info.AssignMergedSegment(
      base_segment_index, segments_to_merge, merged_segment);
  VLOG(0) << "  Merged " << size_before << " codepoints up to " << size_after
          << " codepoints for segment " << base_segment_index
          << ". New patch size " << new_patch_size << " bytes. "
          << "Cost delta is " << cost_delta << ".";

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

static Status AddConditionAndPatchSize(
    const SegmentationContext& context, const ActivationCondition& condition,
    btree_map<ActivationCondition, uint32_t>& conditions) {
  auto existing = conditions.find(condition);
  if (existing != conditions.end()) {
    // already exists.
    return absl::OkStatus();
  }

  // TODO XXXXX add a cache for patch size lookups.
  const auto& conditions_and_glyphs =
      context.glyph_groupings.ConditionsAndGlyphs();
  auto it = conditions_and_glyphs.find(condition);
  if (it == conditions_and_glyphs.end()) {
    return absl::InternalError(
        "Condition which should be present wasn't found.");
  }

  const GlyphSet& glyphs = it->second;
  uint32_t patch_size =
      TRY(EstimatePatchSizeBytes(context.original_face.get(), glyphs));
  conditions.insert(std::pair(condition, patch_size));
  return absl::OkStatus();
}

static Status FindModifiedConditions(
    const SegmentationContext& context, const SegmentSet& merged_segments,
    btree_map<ActivationCondition, uint32_t>& removed_conditions,
    btree_map<ActivationCondition, uint32_t>& modified_conditions) {
  for (auto s : merged_segments) {
    for (const auto& c :
         context.glyph_groupings.TriggeringSegmentToConditions(s)) {
      if (c.IsFallback()) {
        // Ignore fallback for this analysis.
        continue;
      }

      auto condition_segments = c.TriggeringSegments();
      if (condition_segments.is_subset_of(merged_segments)) {
        TRYV(AddConditionAndPatchSize(context, c, removed_conditions));
        continue;
      }

      condition_segments.intersect(merged_segments);
      if (!condition_segments.empty()) {
        TRYV(AddConditionAndPatchSize(context, c, modified_conditions));
      }
    }
  }

  return absl::OkStatus();
}

static StatusOr<double> ComputeCostDelta(const SegmentationContext& context,
                                         const SegmentSet& merged_segments,
                                         const Segment& merged_segment,
                                         uint32_t new_patch_size) {
  // TODO XXXXX make this a tunable parameter taken as an input in the
  // segmentation configuration.
  constexpr uint32_t PER_REQUEST_OVERHEAD = 75;

  // These are conditions which will be removed by appying the merge.
  // A condition is removed if it is a subset of the merged_segments.
  //
  // Map value is the size of the patch associated with the condition.
  btree_map<ActivationCondition, uint32_t> removed_conditions;

  // These are conditions which will be modified, but not removed
  // by applying the merge. These are conditions that intersect
  // but are not a subset of merged_segments.
  //
  // Map value is the size of the patch associated with the condition.
  btree_map<ActivationCondition, uint32_t> modified_conditions;

  TRYV(FindModifiedConditions(context, merged_segments, removed_conditions,
                              modified_conditions));

  // Merge will introduce a new patch (merged_segment) with size
  // "new_patch_size", add the associated cost.
  double cost_delta =
      merged_segment.Probability() * (new_patch_size + PER_REQUEST_OVERHEAD);

  // Now we remove all of the cost associated with segments that are either
  // removed or modified.
  const auto& segments = context.segmentation_info.Segments();
  for (const auto& [c, size] : removed_conditions) {
    cost_delta -= TRY(c.Probability(segments)) * (size + PER_REQUEST_OVERHEAD);
  }
  for (const auto& [c, size] : modified_conditions) {
    cost_delta -= TRY(c.Probability(segments)) * (size + PER_REQUEST_OVERHEAD);
  }

  // Lastly add back the costs associated with the modified version of each
  // segment.
  for (const auto& [c, size] : modified_conditions) {
    // For modified conditions we assume the associated patch size does not
    // change, only the probability associated with the condition changes.
    cost_delta += TRY(c.MergedProbability(segments, merged_segments,
                                          merged_segment.Probability())) *
                  (size + PER_REQUEST_OVERHEAD);
  }

  return cost_delta;
}

StatusOr<std::optional<CandidateMerge>> CandidateMerge::AssessMerge(
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
    new_patch_size =
        TRY(EstimatePatchSizeBytes(context.original_face.get(), merged_glyphs));
  }
  if (new_patch_size > context.patch_size_max_bytes) {
    return std::nullopt;
  }

  double cost_delta = TRY(ComputeCostDelta(context, segments_to_merge_with_base,
                                           merged_segment, new_patch_size));

  return CandidateMerge{.base_segment_index = base_segment_index,
                        .segments_to_merge = segments_to_merge,
                        .merged_segment = std::move(merged_segment),
                        .new_segment_is_inert = new_segment_is_inert,
                        .new_patch_size = new_patch_size,
                        .cost_delta = cost_delta,
                        .invalidated_glyphs = gid_conditions_to_update};
}

}  // namespace ift::encoder
