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
#include "common/woff2.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/glyph_groupings.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/segmentation_context.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"
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
using common::Woff2;
using ift::GlyphKeyedDiff;

StatusOr<bool> CandidateMerge::IsPatchTooSmall(
    SegmentationContext& context, segment_index_t base_segment_index,
    const GlyphSet& glyphs) {
  uint32_t patch_size_bytes =
      TRY(context.patch_size_cache->GetPatchSize(glyphs));
  if (patch_size_bytes >= context.GetMergeStrategy().PatchSizeMinBytes()) {
    return false;
  }

  VLOG(0) << "Patch for segment " << base_segment_index << " is too small "
          << "(" << patch_size_bytes << " < "
          << context.GetMergeStrategy().PatchSizeMinBytes() << "). Merging...";

  return true;
}

GlyphSet CandidateMerge::Apply(SegmentationContext& context) {
  const auto& segments = context.SegmentationInfo().Segments();
  uint32_t size_before =
      segments[base_segment_index].Definition().codepoints.size();
  uint32_t size_after =
      context.AssignMergedSegment(base_segment_index, segments_to_merge,
                                  merged_segment, new_segment_is_inert);

  VLOG(0) << "  Merged " << size_before << " codepoints up to " << size_after
          << " codepoints for segment " << base_segment_index << "."
          << std::endl
          << "  New patch size " << new_patch_size << " bytes. " << std::endl
          << "  Cost delta is " << cost_delta << "." << std::endl
          << "  New probability is "
          << merged_segment.ProbabilityBound().ToString();

  // Regardless of wether the new segment is inert all of the information
  // associated with the segments removed by the merge should be removed.
  context.InvalidateGlyphInformation(invalidated_glyphs, segments_to_merge);

  // Remove the fallback segment or group, it will be fully recomputed by
  // GroupGlyphs. This needs to happen after invalidation because in some
  // cases invalidation may need to find conditions associated with the
  // fallback segment.
  context.glyph_groupings.RemoveFallbackSegments(segments_to_merge);

  if (new_segment_is_inert) {
    // The newly formed segment will be inert which means we can construct the
    // new condition sets and glyph groupings here instead of using the
    // closure analysis to do it. The new segment is simply the union of all
    // glyphs associated with each segment that is part of the merge.
    // (gid_conditons_to_update)
    for (glyph_id_t gid : invalidated_glyphs) {
      context.glyph_condition_set.AddAndCondition(gid, base_segment_index);
    }
    context.glyph_groupings.AddGlyphsToExclusiveGroup(base_segment_index,
                                                      invalidated_glyphs);

    // We've now fully updated information for these glyphs so don't need to
    // return them.
    invalidated_glyphs.clear();
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

static void MergeSegments(const SegmentationContext& context,
                          const SegmentSet& segments, Segment& base) {
  std::vector<const Segment*> merged_segments{&base};
  const auto& segmentation_info = context.SegmentationInfo();

  SubsetDefinition union_def = base.Definition();
  for (segment_index_t next : segments) {
    const auto& s = segmentation_info.Segments()[next];
    union_def.Union(s.Definition());
    merged_segments.push_back(&s);
  }

  const auto* calculator = context.GetMergeStrategy().ProbabilityCalculator();
  const auto& bound = calculator->ComputeMergedProbability(merged_segments);
  base.Definition() = std::move(union_def);
  base.SetProbability(bound);
}

static Status AddConditionAndPatchSize(
    const SegmentationContext& context, const ActivationCondition& condition,
    btree_map<ActivationCondition, uint32_t>& conditions) {
  auto existing = conditions.find(condition);
  if (existing != conditions.end()) {
    // already exists.
    return absl::OkStatus();
  }

  const auto& conditions_and_glyphs =
      context.glyph_groupings.ConditionsAndGlyphs();
  auto it = conditions_and_glyphs.find(condition);
  if (it == conditions_and_glyphs.end()) {
    return absl::InternalError(
        "Condition which should be present wasn't found.");
  }

  const GlyphSet& glyphs = it->second;
  uint32_t patch_size = TRY(context.patch_size_cache->GetPatchSize(glyphs));
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

StatusOr<uint32_t> CandidateMerge::Woff2SizeOf(hb_face_t* original_face,
                                               const SubsetDefinition& def,
                                               int quality) {
  hb_subset_input_t* input = hb_subset_input_create_or_fail();
  if (!input) {
    return absl::InternalError("Failed to create subset input.");
  }
  def.ConfigureInput(input, original_face);

  hb_face_t* init_face = hb_subset_or_fail(original_face, input);
  hb_subset_input_destroy(input);
  if (!init_face) {
    return absl::InternalError("Failed to create initial face subset.");
  }

  FontData init_data(init_face);
  hb_face_destroy(init_face);

  FontData woff2 = TRY(Woff2::EncodeWoff2(init_data.str(), false, quality));
  return (double)woff2.size();
}

static StatusOr<int64_t> InitFontDelta(const SegmentationContext& context,
                                       const SegmentSet& merged_segments) {
  int quality = context.GetMergeStrategy().BrotliQuality();
  SubsetDefinition init_def = context.SegmentationInfo().InitFontSegment();
  int64_t before = TRY(CandidateMerge::Woff2SizeOf(context.original_face.get(),
                                                   init_def, quality));

  for (segment_index_t s : merged_segments) {
    init_def.Union(context.SegmentationInfo().Segments().at(s).Definition());
  }

  int64_t after = TRY(CandidateMerge::Woff2SizeOf(context.original_face.get(),
                                                  init_def, quality));

  return after - before;
}

StatusOr<double> CandidateMerge::ComputeCostDelta(
    const SegmentationContext& context, const SegmentSet& merged_segments,
    std::optional<const Segment*> merged_segment, uint32_t new_patch_size) {
  bool moving_to_init_font = !merged_segment.has_value();
  const uint32_t per_request_overhead =
      context.GetMergeStrategy().NetworkOverheadCost();

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

  double cost_delta = 0.0;
  VLOG(1) << "cost_delta for merge of " << merged_segments.ToString() << " =";
  if (!moving_to_init_font) {
    // Merge will introduce a new patch (merged_segment) with size
    // "new_patch_size", add the associated cost.
    double p = (*merged_segment)->Probability();
    double s = new_patch_size + per_request_overhead;
    cost_delta += p * s;
    VLOG(1) << "    + (" << p << " * " << s << ") -> " << cost_delta
            << " [merged patch]";
  } else {
    // Otherwise the merged segments are being moved to the init font, compute
    // the resulting size delta.
    double delta = TRY(InitFontDelta(context, merged_segments));
    VLOG(1) << "    + " << delta << " [init font increase]";
    cost_delta += delta;
  }

  // Now we remove all of the cost associated with segments that are either
  // removed or modified.
  const auto& segments = context.SegmentationInfo().Segments();
  const auto* calculator = context.GetMergeStrategy().ProbabilityCalculator();
  for (const auto& [c, size] : removed_conditions) {
    double p = TRY(c.Probability(segments, *calculator));
    double s = (size + per_request_overhead);
    double d = p * s;
    cost_delta -= d;
    VLOG(1) << "    - (" << p << " * " << s << ") -> " << d
            << " [removed patch " << c.ToString() << "]";
  }
  for (const auto& [c, size] : modified_conditions) {
    double p = TRY(c.Probability(segments, *calculator));
    double s = size + per_request_overhead;
    double d = p * s;
    VLOG(1) << "    - (" << p << " * " << s << " ) -> " << d
            << " [modified patch " << c.ToString() << "]";
    cost_delta -= d;
  }

  // Lastly add back the costs associated with the modified version of each
  // segment.
  for (const auto& [c, size] : modified_conditions) {
    if (moving_to_init_font) {
      if (c.conditions().size() == 1) {
        // When segments are moved into the init font then a condition that
        // is a union will also get moved into the init font since it will
        // always be needed. As a result the cost for this patch is already
        // accounted for in the InitFontDelta() computed above.
        continue;
      }

      // For the conjunctive case we need to remove the segments being
      // moved to the init font from the condition. Then we can compute
      // the cost addition as usual.
      SegmentSet condition_segments = c.TriggeringSegments();
      condition_segments.subtract(merged_segments);
      if (condition_segments.empty()) {
        continue;
      }

      ActivationCondition new_condition =
          ActivationCondition::and_segments(condition_segments, 0);
      double p = TRY(c.Probability(
          segments, *context.GetMergeStrategy().ProbabilityCalculator()));
      double d = p * (size + per_request_overhead);
      VLOG(1) << "    + " << d << " [modified patch " << c.ToString() << "]";
      cost_delta += d;
      continue;
    }

    // For modified conditions we assume the associated patch size does not
    // change, only the probability associated with the condition changes.
    double d = TRY(c.MergedProbability(segments, merged_segments,
                                       **merged_segment, *calculator)) *
               (size + per_request_overhead);
    VLOG(1) << "    + " << d << " [modified patch " << c.ToString() << "]";
    cost_delta += d;
  }
  VLOG(1) << "    = " << cost_delta;

  return cost_delta;
}

StatusOr<std::optional<CandidateMerge>> CandidateMerge::AssessMerge(
    SegmentationContext& context, segment_index_t base_segment_index,
    const SegmentSet& segments_to_merge_,
    const std::optional<CandidateMerge>& best_merge_candidate) {
  if (!context.GetMergeStrategy().UseCosts() &&
      WouldMixFeaturesAndCodepoints(context.SegmentationInfo(),
                                    base_segment_index, segments_to_merge_)) {
    // With the heuristic merger if it doesn't find a previous merge candidate
    // will try to merge together segments that are composed of codepoints with
    // a segment that adds an optional feature. Since this feature segments are
    // likely rarely used this will inflate the size of the patches for those
    // codepoint segments unnecessarily.
    //
    // So don't merge cases where we would be combining codepoint
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

  bool segments_to_merge_are_inert =
      segments_to_merge.is_subset_of(context.InertSegments());
  bool new_segment_is_inert =
      context.InertSegments().contains(base_segment_index) &&
      segments_to_merge_are_inert;

  const auto& segments = context.SegmentationInfo().Segments();

  Segment merged_segment = segments[base_segment_index];
  MergeSegments(context, segments_to_merge, merged_segment);

  if (context.GetMergeStrategy().UseCosts() && segments_to_merge_are_inert &&
      segments_to_merge.size() == 1 && best_merge_candidate.has_value()) {
    // Given an existing best merge candidate we can compute a probability
    // threshold on the segments to be merged that will allow us to quickly
    // discard merges which can't possibily beat the current best.
    unsigned segment_to_merge = segments_to_merge.min().value();
    const GlyphSet& glyphs =
        context.glyph_condition_set.GlyphsWithSegment(segment_to_merge);
    unsigned segment_to_merge_size = 0;
    if (!glyphs.empty()) {
      segment_to_merge_size =
          TRY(context.patch_size_cache->GetPatchSize(glyphs));
    }
    double threshold = best_merge_candidate->InertProbabilityThreshold(
        segment_to_merge_size, merged_segment.Probability());
    if (segments[segment_to_merge].Probability() <= threshold) {
      // No chance for this merge to beat the current best.
      return std::nullopt;
    }
  }

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
    GlyphSet and_gids, or_gids, exclusive_gids;
    TRYV(context.AnalyzeSegment(segments_to_merge_with_base, and_gids, or_gids,
                                exclusive_gids));
    new_patch_size =
        TRY(context.patch_size_cache->GetPatchSize(exclusive_gids));
  } else {
    // For inert patches we can precompute the glyph set saving a closure
    // operation
    GlyphSet merged_glyphs = gid_conditions_to_update;
    merged_glyphs.union_set(
        context.glyph_condition_set.GlyphsWithSegment(base_segment_index));
    new_patch_size = TRY(context.patch_size_cache->GetPatchSize(merged_glyphs));
  }

  if (!context.GetMergeStrategy().UseCosts() &&
      new_patch_size > context.GetMergeStrategy().PatchSizeMaxBytes()) {
    return std::nullopt;
  }

  double cost_delta = 0.0;
  if (context.GetMergeStrategy().UseCosts()) {
    // Cost delta values are only needed when using cost based merge strategy.
    cost_delta = TRY(ComputeCostDelta(context, segments_to_merge_with_base,
                                      &merged_segment, new_patch_size));
  }

  if (best_merge_candidate.has_value() &&
      cost_delta >= best_merge_candidate.value().cost_delta) {
    // Our delta is not smaller, don't bother returning a candidate.
    return std::nullopt;
  }

  auto candidate =
      CandidateMerge{.base_segment_index = base_segment_index,
                     .segments_to_merge = segments_to_merge,
                     .merged_segment = std::move(merged_segment),
                     .new_segment_is_inert = new_segment_is_inert,
                     .new_patch_size = new_patch_size,
                     .cost_delta = cost_delta,
                     .invalidated_glyphs = gid_conditions_to_update};

  if (context.GetMergeStrategy().UseCosts()) {
    const GlyphSet& base_segment_glyphs =
        context.glyph_condition_set.GlyphsWithSegment(base_segment_index);
    candidate.base_size =
        TRY(context.patch_size_cache->GetPatchSize(base_segment_glyphs));
    candidate.base_probability = segments[base_segment_index].Probability();
    candidate.network_overhead =
        context.GetMergeStrategy().NetworkOverheadCost();
  }

  return candidate;
}

}  // namespace ift::encoder
