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
#include "ift/encoder/merger.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/segment.h"
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
    Merger& merger, segment_index_t base_segment_index,
    const GlyphSet& glyphs) {
  uint32_t patch_size_bytes =
      TRY(merger.Context().patch_size_cache->GetPatchSize(glyphs));
  if (patch_size_bytes >= merger.Strategy().PatchSizeMinBytes()) {
    return false;
  }

  VLOG(0) << "Patch for segment " << base_segment_index << " is too small "
          << "(" << patch_size_bytes << " < "
          << merger.Strategy().PatchSizeMinBytes() << "). Merging...";

  return true;
}

StatusOr<GlyphSet> CandidateMerge::Apply(Merger& merger) {
  if (!merged_segment_.has_value()) {
    TRYV(ApplyPatchMerge(merger));
    return GlyphSet{};
  }

  const auto& segments = merger.Context().SegmentationInfo().Segments();
  uint32_t size_before =
      segments[base_segment_index_].Definition().codepoints.size();
  uint32_t size_after =
      merger.AssignMergedSegment(base_segment_index_, segments_to_merge_,
                                 *merged_segment_, new_segment_is_inert_);

  VLOG(0) << "  Merged " << size_before << " codepoints up to " << size_after
          << " codepoints for segment " << base_segment_index_ << "."
          << std::endl
          << "  New patch size " << new_patch_size_ << " bytes. " << std::endl
          << "  Cost delta is " << cost_delta_ << "." << std::endl
          << "  New probability is "
          << merged_segment_->ProbabilityBound().ToString();

  // Regardless of wether the new segment is inert all of the information
  // associated with the segments removed by the merge should be removed.
  merger.Context().InvalidateGlyphInformation(invalidated_glyphs_,
                                              segments_to_merge_);

  // Remove the fallback segment or group, it will be fully recomputed by
  // GroupGlyphs. This needs to happen after invalidation because in some
  // cases invalidation may need to find conditions associated with the
  // fallback segment.
  merger.Context().glyph_groupings.RemoveFallbackSegments(segments_to_merge_);

  if (new_segment_is_inert_) {
    // The newly formed segment will be inert which means we can construct the
    // new condition sets and glyph groupings here instead of using the
    // closure analysis to do it. The new segment is simply the union of all
    // glyphs associated with each segment that is part of the merge.
    // (gid_conditons_to_update)
    for (glyph_id_t gid : invalidated_glyphs_) {
      merger.Context().glyph_condition_set.AddAndCondition(gid,
                                                           base_segment_index_);
    }
    merger.Context().glyph_groupings.AddGlyphsToExclusiveGroup(
        base_segment_index_, invalidated_glyphs_);

    // We've now fully updated information for these glyphs so don't need to
    // return them.
    invalidated_glyphs_.clear();
  }

  return invalidated_glyphs_;
}

Status CandidateMerge::ApplyPatchMerge(Merger& merger) {
  const GlyphSet& base_glyphs =
      merger.Context().glyph_groupings.ExclusiveGlyphs(base_segment_index_);
  const GlyphSet& other_glyphs =
      merger.Context().glyph_groupings.ConditionsAndGlyphs().at(
          ActivationCondition::or_segments(segments_to_merge_, 0));

  VLOG(0) << "  Merged patches from "
          << ActivationCondition::exclusive_segment(base_segment_index_, 0)
                 .ToString()
          << " (" << base_glyphs.size() << " glyphs) with "
          << ActivationCondition::or_segments(segments_to_merge_, 0).ToString()
          << " (" << other_glyphs.size() << " glyphs) with "
          << "." << std::endl
          << "  New patch size " << new_patch_size_ << " bytes. " << std::endl
          << "  Cost delta is " << cost_delta_ << "." << std::endl;

  // CombinePatches() will do invalidation as needed, so nothing else needs to
  // be done to apply this merge.
  return merger.Context().glyph_groupings.CombinePatches(base_glyphs,
                                                         other_glyphs);
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

static void MergeSegments(const Merger& merger, const SegmentSet& segments,
                          Segment& base) {
  std::vector<const Segment*> merged_segments{&base};
  const auto& segmentation_info = merger.Context().SegmentationInfo();

  SubsetDefinition union_def = base.Definition();
  for (segment_index_t next : segments) {
    const auto& s = segmentation_info.Segments()[next];
    union_def.Union(s.Definition());
    merged_segments.push_back(&s);
  }

  const auto* calculator = merger.Strategy().ProbabilityCalculator();
  const auto& bound = calculator->ComputeMergedProbability(merged_segments);
  base.Definition() = std::move(union_def);
  base.SetProbability(bound);
}

static Status AddConditionAndPatchSize(
    const Merger& merger, const ActivationCondition& condition,
    btree_map<ActivationCondition, uint32_t>& conditions) {
  auto existing = conditions.find(condition);
  if (existing != conditions.end()) {
    // already exists.
    return absl::OkStatus();
  }

  const auto& conditions_and_glyphs =
      merger.Context().glyph_groupings.ConditionsAndGlyphs();
  auto it = conditions_and_glyphs.find(condition);
  if (it == conditions_and_glyphs.end()) {
    return absl::InternalError(
        "Condition which should be present wasn't found.");
  }

  const GlyphSet& glyphs = it->second;
  uint32_t patch_size =
      TRY(merger.Context().patch_size_cache->GetPatchSize(glyphs));
  conditions.insert(std::pair(condition, patch_size));
  return absl::OkStatus();
}

static Status FindModifiedConditions(
    const Merger& merger, const SegmentSet& merged_segments,
    btree_map<ActivationCondition, uint32_t>& removed_conditions,
    btree_map<ActivationCondition, uint32_t>& modified_conditions) {
  for (auto s : merged_segments) {
    for (const auto& c :
         merger.Context().glyph_groupings.TriggeringSegmentToConditions(s)) {
      if (c.IsFallback()) {
        // Ignore fallback for this analysis.
        continue;
      }

      auto condition_segments = c.TriggeringSegments();
      if (condition_segments.is_subset_of(merged_segments)) {
        TRYV(AddConditionAndPatchSize(merger, c, removed_conditions));
        continue;
      }

      condition_segments.intersect(merged_segments);
      if (!condition_segments.empty()) {
        TRYV(AddConditionAndPatchSize(merger, c, modified_conditions));
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

static StatusOr<int64_t> InitFontDelta(const Merger& merger,
                                       const SegmentSet& merged_segments) {
  int quality = merger.Context().BrotliQuality();
  SubsetDefinition init_def =
      merger.Context().SegmentationInfo().InitFontSegment();
  int64_t before = TRY(CandidateMerge::Woff2SizeOf(
      merger.Context().original_face.get(), init_def, quality));

  for (segment_index_t s : merged_segments) {
    init_def.Union(
        merger.Context().SegmentationInfo().Segments().at(s).Definition());
  }

  int64_t after = TRY(CandidateMerge::Woff2SizeOf(
      merger.Context().original_face.get(), init_def, quality));

  return after - before;
}

StatusOr<double> CandidateMerge::ComputeCostDelta(
    const Merger& merger, const SegmentSet& merged_segments,
    std::optional<const Segment*> merged_segment, uint32_t new_patch_size) {
  bool moving_to_init_font = !merged_segment.has_value();
  const uint32_t per_request_overhead = merger.Strategy().NetworkOverheadCost();

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

  TRYV(FindModifiedConditions(merger, merged_segments, removed_conditions,
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
    double delta = TRY(InitFontDelta(merger, merged_segments));
    VLOG(1) << "    + " << delta << " [init font increase]";
    cost_delta += delta;
  }

  // Now we remove all of the cost associated with segments that are either
  // removed or modified.
  const auto& segments = merger.Context().SegmentationInfo().Segments();
  const auto* calculator = merger.Strategy().ProbabilityCalculator();
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
      double p = TRY(
          c.Probability(segments, *merger.Strategy().ProbabilityCalculator()));
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

StatusOr<double> CandidateMerge::ComputePatchMergeCostDelta(
    const Merger& merger, segment_index_t base_segment,
    const GlyphSet& base_glyphs, const SegmentSet& target_segments,
    const GlyphSet& target_glyphs, const GlyphSet& merged_glyphs) {
  // For a patch merge only three things are affected:
  // 1. Remove the exclusive patch associated with base_segment.
  // 2. Remove the disjunctive patch with condition equal to target_segments.
  // 3. Add a new combined patch that contains all of the glyphs of 1 + 2.
  //    New condition is {base} union {merged}, with corresponding new
  //    probability.

  double network_overhead = merger.Strategy().NetworkOverheadCost();
  double base_patch_size =
      TRY(merger.Context().patch_size_cache->GetPatchSize(base_glyphs));
  base_patch_size += network_overhead;
  double base_probability = merger.Context()
                                .SegmentationInfo()
                                .Segments()
                                .at(base_segment)
                                .Probability();

  ActivationCondition target_condition =
      ActivationCondition::or_segments(target_segments, 0);
  double target_patch_size =
      TRY(merger.Context().patch_size_cache->GetPatchSize(target_glyphs));
  target_patch_size += network_overhead;
  double target_probability = TRY(target_condition.Probability(
      merger.Context().SegmentationInfo().Segments(),
      *merger.Strategy().ProbabilityCalculator()));

  SegmentSet merged_segments = target_segments;
  merged_segments.insert(base_segment);
  ActivationCondition merged_condition =
      ActivationCondition::or_segments(merged_segments, 0);
  double merged_patch_size =
      TRY(merger.Context().patch_size_cache->GetPatchSize(merged_glyphs));
  merged_patch_size += network_overhead;
  double merged_probability = TRY(merged_condition.Probability(
      merger.Context().SegmentationInfo().Segments(),
      *merger.Strategy().ProbabilityCalculator()));

  VLOG(1) << "cost_delta for patch merge of " << base_segment << " with "
          << merged_segments.ToString() << " =";
  double cost_delta = 0.0;

  cost_delta += merged_probability * merged_patch_size;
  VLOG(1) << "    + (" << merged_probability << " * " << merged_patch_size
          << ") -> " << cost_delta << " [merged patch]";

  cost_delta -= base_probability * base_patch_size;
  VLOG(1) << "    - (" << base_probability << " * " << base_patch_size
          << ") -> " << cost_delta << " [removed patch]";

  cost_delta -= target_probability * target_patch_size;
  VLOG(1) << "    - (" << target_probability << " * " << target_patch_size
          << ") -> " << cost_delta << " [removed patch]";

  VLOG(1) << "    = " << cost_delta;
  return cost_delta;
}

StatusOr<std::optional<CandidateMerge>> CandidateMerge::AssessSegmentMerge(
    Merger& merger, segment_index_t base_segment_index,
    const SegmentSet& segments_to_merge_,
    const std::optional<CandidateMerge>& best_merge_candidate) {
  if (!merger.Strategy().UseCosts() &&
      WouldMixFeaturesAndCodepoints(merger.Context().SegmentationInfo(),
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
      segments_to_merge.is_subset_of(merger.Context().InertSegments());
  bool new_segment_is_inert =
      merger.Context().InertSegments().contains(base_segment_index) &&
      segments_to_merge_are_inert;

  const auto& segments = merger.Context().SegmentationInfo().Segments();

  Segment merged_segment = segments[base_segment_index];
  MergeSegments(merger, segments_to_merge, merged_segment);

  if (merger.Strategy().UseCosts() && segments_to_merge_are_inert &&
      segments_to_merge.size() == 1 && best_merge_candidate.has_value()) {
    // Given an existing best merge candidate we can compute a probability
    // threshold on the segments to be merged that will allow us to quickly
    // discard merges which can't possibily beat the current best.
    unsigned segment_to_merge = segments_to_merge.min().value();
    const GlyphSet& glyphs =
        merger.Context().glyph_condition_set.GlyphsWithSegment(
            segment_to_merge);
    unsigned segment_to_merge_size = 0;
    if (!glyphs.empty()) {
      segment_to_merge_size =
          TRY(merger.Context().patch_size_cache->GetPatchSize(glyphs));
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
        merger.Context().glyph_condition_set.GlyphsWithSegment(segment_index));
  }

  uint32_t new_patch_size = 0;
  if (!new_segment_is_inert) {
    GlyphSet and_gids, or_gids, exclusive_gids;
    TRYV(merger.Context().AnalyzeSegment(segments_to_merge_with_base, and_gids,
                                         or_gids, exclusive_gids));
    new_patch_size =
        TRY(merger.Context().patch_size_cache->GetPatchSize(exclusive_gids));
  } else {
    // For inert patches we can precompute the glyph set saving a closure
    // operation
    GlyphSet merged_glyphs = gid_conditions_to_update;
    merged_glyphs.union_set(
        merger.Context().glyph_condition_set.GlyphsWithSegment(
            base_segment_index));
    new_patch_size =
        TRY(merger.Context().patch_size_cache->GetPatchSize(merged_glyphs));
  }

  if (!merger.Strategy().UseCosts() &&
      new_patch_size > merger.Strategy().PatchSizeMaxBytes()) {
    return std::nullopt;
  }

  double cost_delta = 0.0;
  if (merger.Strategy().UseCosts()) {
    // Cost delta values are only needed when using cost based merge strategy.
    cost_delta = TRY(ComputeCostDelta(merger, segments_to_merge_with_base,
                                      &merged_segment, new_patch_size));
  }

  if (best_merge_candidate.has_value() &&
      cost_delta >= best_merge_candidate.value().cost_delta_) {
    // Our delta is not smaller, don't bother returning a candidate.
    return std::nullopt;
  }

  CandidateMerge candidate(std::move(merged_segment));
  candidate.base_segment_index_ = base_segment_index;
  candidate.segments_to_merge_ = segments_to_merge;
  candidate.new_segment_is_inert_ = new_segment_is_inert;
  candidate.new_patch_size_ = new_patch_size;
  candidate.cost_delta_ = cost_delta;
  candidate.invalidated_glyphs_ = gid_conditions_to_update;

  if (merger.Strategy().UseCosts()) {
    const GlyphSet& base_segment_glyphs =
        merger.Context().glyph_condition_set.GlyphsWithSegment(
            base_segment_index);
    candidate.base_size_ = TRY(
        merger.Context().patch_size_cache->GetPatchSize(base_segment_glyphs));
    candidate.base_probability_ = segments[base_segment_index].Probability();
    candidate.network_overhead_ = merger.Strategy().NetworkOverheadCost();
  }

  return candidate;
}

StatusOr<std::optional<CandidateMerge>> CandidateMerge::AssessPatchMerge(
    Merger& merger, segment_index_t base_segment_index,
    const SegmentSet& segments_to_merge,
    const std::optional<CandidateMerge>& best_merge_candidate) {
  const auto& segments = merger.Context().SegmentationInfo().Segments();

  const GlyphSet& base_glyphs =
      merger.Context().glyph_groupings.ExclusiveGlyphs(base_segment_index);
  auto other_glyphs_it =
      merger.Context().glyph_groupings.ConditionsAndGlyphs().find(
          ActivationCondition::or_segments(segments_to_merge, 0));
  if (base_glyphs.empty() ||
      other_glyphs_it ==
          merger.Context().glyph_groupings.ConditionsAndGlyphs().end()) {
    // Can only merge if both base patch and the segments_to_merge patch exist.
    return std::nullopt;
  }
  const GlyphSet& other_glyphs = other_glyphs_it->second;

  // A patch merge is straightforward, just the glyphs from the two merged
  // patches are combined.
  GlyphSet combined_glyphs = base_glyphs;
  combined_glyphs.union_set(other_glyphs);
  uint32_t new_patch_size =
      TRY(merger.Context().patch_size_cache->GetPatchSize(combined_glyphs));

  if (!merger.Strategy().UseCosts() &&
      new_patch_size > merger.Strategy().PatchSizeMaxBytes()) {
    return std::nullopt;
  }

  double cost_delta = 0.0;
  if (merger.Strategy().UseCosts()) {
    // Cost delta values are only needed when using cost based merge strategy.
    cost_delta = TRY(ComputePatchMergeCostDelta(merger, base_segment_index,
                                                base_glyphs, segments_to_merge,
                                                other_glyphs, combined_glyphs));
  }

  if (best_merge_candidate.has_value() &&
      cost_delta >= best_merge_candidate.value().cost_delta_) {
    // Our delta is not smaller, don't bother returning a candidate.
    return std::nullopt;
  }

  CandidateMerge candidate;
  candidate.base_segment_index_ = base_segment_index;
  candidate.segments_to_merge_ = segments_to_merge;
  candidate.new_segment_is_inert_ = false;
  candidate.new_patch_size_ = new_patch_size;
  candidate.cost_delta_ = cost_delta;

  // Since patch merges trigger full recomputation (of the combined patches), no
  // glyphs need to be invalidated by this merge.
  candidate.invalidated_glyphs_ = {};

  if (merger.Strategy().UseCosts()) {
    candidate.base_size_ =
        TRY(merger.Context().patch_size_cache->GetPatchSize(base_glyphs));
    candidate.base_probability_ = segments[base_segment_index].Probability();
    candidate.network_overhead_ = merger.Strategy().NetworkOverheadCost();
  }

  return candidate;
}

}  // namespace ift::encoder
