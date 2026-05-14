#include "ift/encoder/candidate_merge.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "ift/common/compat_id.h"
#include "ift/common/font_data.h"
#include "ift/common/font_helper.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/common/woff2.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/glyph_condition_set.h"
#include "ift/encoder/invalidation_set.h"
#include "ift/encoder/merger.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/segmentation_context.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"
#include "ift/glyph_keyed_diff.h"

namespace ift::encoder {

using absl::flat_hash_set;
using absl::flat_hash_map;
using absl::Status;
using absl::StatusOr;
using ift::GlyphKeyedDiff;
using ift::common::CodepointSet;
using ift::common::CompatId;
using ift::common::FontData;
using ift::common::FontHelper;
using ift::common::GlyphSet;
using ift::common::SegmentSet;
using ift::common::Woff2;

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

StatusOr<InvalidationSet> CandidateMerge::Apply(Merger& merger) {
  if (!merged_segment_.has_value()) {
    TRYV(ApplyPatchMerge(merger));
    return InvalidationSet(base_segment_index_);
  }

  // Upon application of this merger if all of the input segments were inert
  // then it's likely that the new segment will also be inert (though not
  // gauranteed). We can check for this case by running a closure analysis
  // and checking there are no non-exclusive gids. If the input segments are
  // inert and the new segment is inert then we can directly compute the result
  // of the merge without needing to invalidate and reprocess.
  SegmentSet segments_to_merge_with_base = segments_to_merge_;
  segments_to_merge_with_base.insert(base_segment_index_);
  bool new_segment_is_inert = false;
  if (input_segments_are_inert_) {
    GlyphSet and_gids, or_gids, exclusive_gids;
    TRYV(merger.Context().AnalyzeSegment(segments_to_merge_with_base, and_gids,
                                         or_gids, exclusive_gids));
    new_segment_is_inert = (and_gids.empty() && or_gids.empty());
    if (new_segment_is_inert) {
      // When the new segment is inert then invalidated glyphs is
      // used to shortcut closure analysis and directly construct
      // the new condition and glyph mappings, save all of the
      // closure glyphs to it.
      invalidated_glyphs_.union_set(exclusive_gids);
    }
  }

  const auto& segments = merger.Context().SegmentationInfo().Segments();
  uint32_t size_before =
      segments[base_segment_index_].Definition().codepoints.size();
  uint32_t size_after =
      merger.AssignMergedSegment(base_segment_index_, segments_to_merge_,
                                 *merged_segment_, new_segment_is_inert);

  VLOG(0) << "  Merged " << size_before << " codepoints up to " << size_after
          << " codepoints for segment " << base_segment_index_ << "."
          << std::endl
          << "  New patch size " << new_patch_size_ << " bytes. " << std::endl
          << "  Cost delta is " << cost_delta_ << "." << std::endl
          << "  New probability is "
          << merged_segment_->ProbabilityBound().ToString();

  // Regardless of wether the new segment is inert all of the information
  // associated with the segments removed by the merge should be removed.
  TRYV(merger.Context().InvalidateGlyphInformationForMerge(
      invalidated_glyphs_, segments_to_merge_with_base, base_segment_index_));

  if (new_segment_is_inert) {
    // The newly formed segment will be inert which means we can construct the
    // new condition sets and glyph groupings here instead of using the
    // closure analysis to do it. The new segment is simply the union of all
    // glyphs associated with each segment that is part of the merge.
    // (gid_conditons_to_update)
    for (glyph_id_t gid : invalidated_glyphs_) {
      merger.Context().glyph_condition_set.AddAndCondition(gid,
                                                           base_segment_index_);
    }
    TRYV(merger.Context().glyph_groupings.AddGlyphsToExclusiveGroup(
        base_segment_index_, invalidated_glyphs_));

    // We've now fully updated information for these glyphs so don't need to
    // return them.
    invalidated_glyphs_.clear();
  }

  return InvalidationSet(invalidated_glyphs_, segments_to_merge_,
                         base_segment_index_);
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
          << " (" << other_glyphs.size() << " glyphs)." << std::endl
          << "  New patch size " << new_patch_size_ << " bytes. " << std::endl
          << "  Cost delta is " << cost_delta_ << ".";

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

  // Compute probability before modifying base since it's in the merged segments array.
  const auto* calculator = merger.Strategy().ProbabilityCalculator();
  const auto& bound = calculator->ComputeMergedProbability(merged_segments);
  base.Definition() = std::move(union_def);
  base.SetProbability(bound);
}

static Status FindModifiedConditions(
    const Merger& merger, const SegmentSet& merged_segments,
    flat_hash_map<ActivationCondition, const GlyphSet*>& modified_conditions) {
  const auto& groupings = merger.Context().glyph_groupings;
  const auto& conditions_and_glyphs = groupings.ConditionsAndGlyphs();

  for (auto s : merged_segments) {
    for (const auto& c : groupings.TriggeringSegmentToConditions(s)) {
      if (c.IsFallback()) {
        // Ignore fallback for this analysis.
        continue;
      }

      auto [it, inserted] = modified_conditions.try_emplace(c, nullptr);
      if (inserted) {
        auto glyph_it = conditions_and_glyphs.find(c);
        if (glyph_it == conditions_and_glyphs.end()) {
          return absl::InternalError(
              "Condition which should be present wasn't found.");
        }
        it->second = &glyph_it->second;
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

// Finds the set of patches which intersect either gids or segments.
static flat_hash_map<ActivationCondition, GlyphSet> PatchesWithGlyphsOrSegments(
    const SegmentationContext& context, const GlyphSet& gids, const SegmentSet& segments) {
  // To more efficiently target our search we can use the glyph_condition_set to
  // locate conditions that intersect with gids.
  GlyphSet fallback_glyphs = context.glyph_groupings.UnmappedGlyphs();
  flat_hash_set<ActivationCondition> conditions_of_interest;
  for (glyph_id_t gid : gids) {
    if (fallback_glyphs.contains(gid)) {
      // Fallback glyphs are handled separately at the end since the
      // conditions in the glyph condition set associated with a fallback
      // glyph are not accurate.
      continue;
    }

    auto result = context.glyph_groupings.GlyphToCondition(gid);
    if (!result.has_value()) {
      continue;
    }

    conditions_of_interest.insert(*result);
  }

  for (segment_index_t s : segments) {
    const auto& conditions = context.glyph_groupings.TriggeringSegmentToConditions(s);
    conditions_of_interest.insert(conditions.begin(), conditions.end());
  }

  flat_hash_map<ActivationCondition, GlyphSet> result;
  for (const auto& condition : conditions_of_interest) {
    result.insert(std::make_pair(
        condition,
        context.glyph_groupings.ConditionsAndGlyphs().at(condition)));
  }

  // We also need to check if there's a fallback patch and it intersects gids.
  if (!fallback_glyphs.empty() && fallback_glyphs.intersects(gids)) {
    ActivationCondition condition =
        ActivationCondition::or_segments({}, 0, true);
    result.insert(std::make_pair(condition, fallback_glyphs));
  }

  return result;
}

Status CandidateMerge::ComputeInitFontGlyphDelta(
    Merger& merger, const GlyphSet& moved_glyphs, GlyphSet& new_glyph_closure,
    GlyphSet& glyph_closure_delta, CodepointSet& codepoint_closure_delta) {
  SubsetDefinition inital_subset =
      merger.Context().SegmentationInfo().InitFontSegment();
  inital_subset.gids.union_set(moved_glyphs);

  SubsetDefinition expanded =
      TRY(merger.Context().glyph_closure_cache->ExpandClosure(inital_subset));
  new_glyph_closure = std::move(expanded.gids);

  glyph_closure_delta = new_glyph_closure;
  glyph_closure_delta.subtract(
      merger.Context().SegmentationInfo().InitFontGlyphs());

  codepoint_closure_delta = std::move(expanded.codepoints);
  codepoint_closure_delta.subtract(inital_subset.codepoints);

  return absl::OkStatus();
}

static std::optional<std::pair<ActivationCondition, GlyphSet>> FindExistingCondition(
  const flat_hash_map<ActivationCondition, GlyphSet>& condition_and_glyphs,
  const ActivationCondition& condition) {

  auto it = condition_and_glyphs.find(condition);
  if (it != condition_and_glyphs.end()) {
    return *it;
  }

  if (condition.IsUnitary()) {
    // For unitary conditions, the existing one might exist under a different is_exclusive value
    segment_index_t s = *condition.TriggeringSegments().min();
    ActivationCondition secondary = ActivationCondition::exclusive_segment(s, 0);
    if (condition.IsExclusive()) {
      secondary = ActivationCondition::or_segments({s}, 0);
    }
    it = condition_and_glyphs.find(secondary);
    if (it != condition_and_glyphs.end()) {
      return *it;
    }
  }

  return std::nullopt;
}

static StatusOr<double> CostFor(Merger& merger, const ActivationCondition& condition, const GlyphSet& glyphs) {
  double probability = 1.0;
  if (!condition.IsFallback()) {
    probability = TRY(
          condition.Probability(merger.Context().SegmentationInfo().Segments(),
                                *merger.Strategy().ProbabilityCalculator()));
  }

  double patch_size =
      TRY(merger.Context().patch_size_cache_for_init_font->GetPatchSize(
          glyphs)) + merger.Strategy().NetworkOverheadCost();

  double cost = probability * patch_size;
  VLOG(1) << "    - (" << probability << " * " << patch_size
          << ") -> " << cost << " [modified patch]";
  return cost;
}

StatusOr<std::pair<double, GlyphSet>> CandidateMerge::ComputeInitFontCostDelta(
    Merger& merger, uint32_t existing_init_font_size,
    const GlyphSet& moved_glyphs,
    absl::flat_hash_map<ift::common::GlyphSet, uint32_t>& smallest_size_increases
  ) {
  // Brotli compression results can be a bit noisy and it's common to see very different
  // size increase (in both directions) for adding the same glyphs as the base changes.
  // So to help control some of this noise we use smallest_size_increases to track the smallest
  // size increase we've seen for moving a specific set of glyphs.

  VLOG(1) << "cost_delta for move of glyphs " << moved_glyphs.ToString()
          << " to the initial font =";

  // For this analysis we only care about the glyph data size in the initial
  // font since all 'no glyph' data cost will be incurred via table keyed
  // patches or in the initial font and thus isn't relevant to whether the gids
  // are in the initial font or a patch. So we utilize the glyph keyed patch
  // size of the init font closure as a proxy to measure the cost of glyph data
  // in the initial font.

  // Moving glyphs to the initial font has the following affects:
  // 1. Based on the input moved_glyphs an expanded set of glyphs and codepoints are found
  //    and moved to the initial font.
  // 2. This affects condition -> patch costs by changing the contained glyphs and in
  //    some cases modifying the activation conditions. Conditions are changed by either
  //    segments being fully moved to init font (becoming always true), or more rarely
  //    by segment definitions changing which in turn affects the probability.

  GlyphSet new_glyph_closure, glyph_closure_delta;
  CodepointSet codepoint_closure_delta;
  TRYV(ComputeInitFontGlyphDelta(merger, moved_glyphs, new_glyph_closure,
                                 glyph_closure_delta, codepoint_closure_delta));

  uint32_t after = TRY(merger.Context().patch_size_cache_for_init_font->GetPatchSize(
          new_glyph_closure));
  uint32_t init_increase = 0;

  if (after >= existing_init_font_size) {
    init_increase = after - existing_init_font_size;
    auto [it, _] = smallest_size_increases.emplace(glyph_closure_delta, UINT32_MAX);
    if (init_increase < it->second) {
      it->second = init_increase;
    } else {
      init_increase = it->second;
    }
  }

  double total_delta = init_increase;

  VLOG(1) << "    + " << total_delta << " [init font increase]";

  SegmentSet modified_segments = merger.Context().SegmentationInfo().SegmentsForCodepoints(codepoint_closure_delta);
  SegmentSet removed_segments;
  for (segment_index_t s : modified_segments) {
    const auto& segment = merger.Context().SegmentationInfo().Segments().at(s).Definition();
    if (segment.feature_tags.empty() && segment.codepoints.is_subset_of(codepoint_closure_delta)) {
      // all codepoints are being moved to init font, so this segment is going to be removed.
      removed_segments.insert(s);
    }
  }

  flat_hash_map<ActivationCondition, GlyphSet> new_conditions;

  const uint32_t per_request_overhead = merger.Strategy().NetworkOverheadCost();
  auto affected_conditions = PatchesWithGlyphsOrSegments(merger.Context(), glyph_closure_delta, modified_segments);
  for (const auto& [condition, glyphs] : affected_conditions) {

    GlyphSet glyphs_after = glyphs;
    glyphs_after.subtract(glyph_closure_delta);

    total_delta -= TRY(CostFor(merger, condition, glyphs));

    if (glyphs_after.empty()) {
      continue;
    }

    // Remove segments effectively become always true, so update the condition by removing
    // sub-groups that contain those segments as the whole subgroup will also become "true".
    ActivationCondition updated = condition.RemoveIntersectingSubgroups(removed_segments);
    auto [it, did_insert] = new_conditions.emplace(updated, GlyphSet {});
    it->second.union_set(glyphs_after);
    if (!did_insert) {
      continue;
    }

    auto existing = FindExistingCondition(merger.Context().glyph_groupings.ConditionsAndGlyphs(), updated);
    if (existing.has_value() && !affected_conditions.contains(existing->first)) {
      // Existing isn't in the affected list so we need to account for it's removal, and transfer
      // it's glyphs to new_conditions.
      it->second.union_set(existing->second);
      total_delta -= TRY(CostFor(merger, existing->first, existing->second));
    }
  }

  for (const auto& [condition, glyphs] : new_conditions) {
    double patch_probability_after = 1.0;
    if (!condition.IsFallback()) {
      // TODO(garretrieger): XXXX also include the effect of modified segments in this calc.
      // Start with finding a test case.
      patch_probability_after = TRY(
          condition.Probability(merger.Context().SegmentationInfo().Segments(),
                                *merger.Strategy().ProbabilityCalculator()));
    }

    double patch_size_after =
          TRY(merger.Context().patch_size_cache_for_init_font->GetPatchSize(
              glyphs)) +
          per_request_overhead;
    double cost_after = patch_probability_after * patch_size_after;

    VLOG(1) << "    + (" << patch_probability_after << " * " << patch_size_after
            << ") -> " << cost_after << " [modified patch]";
    total_delta += cost_after;
  }

  VLOG(1) << "    = " << total_delta;

  return std::make_pair(total_delta, glyph_closure_delta);
}

StatusOr<double> CandidateMerge::ComputeBestCaseInitFontCostDelta(
    Merger& merger, uint32_t existing_init_font_size,
    const GlyphSet& moved_glyphs) {

  // TODO(garretrieger): consider reworking this to avoid running a glyph closure, by
  // working only with the explicitly moved glyphs.
  GlyphSet new_glyph_closure, glyph_closure_delta;
  CodepointSet codepoint_closure_delta;
  TRYV(ComputeInitFontGlyphDelta(merger, moved_glyphs, new_glyph_closure,
                                 glyph_closure_delta, codepoint_closure_delta));

  double cost_delta = 0.0;
  double best_case_reduction =
      merger.Strategy().BestCaseSizeReductionFraction();
  double per_request_overhead = merger.Strategy().NetworkOverheadCost();
  // This best case computation is a simplified version that ignores the impact of changing
  // segments on the cost computation and focuses only on the glyphs that are being moved.
  for (const auto& [condition, glyphs] :
       PatchesWithGlyphsOrSegments(merger.Context(), glyph_closure_delta, SegmentSet {})) {
    double patch_probability = 1.0;
    if (!condition.IsFallback()) {
      patch_probability = TRY(
          condition.Probability(merger.Context().SegmentationInfo().Segments(),
                                *merger.Strategy().ProbabilityCalculator()));
    }

    GlyphSet new_glyphs = glyphs;
    new_glyphs.subtract(glyph_closure_delta);

    // Looking up unmodified patch size will have a very high cache hit rate so
    // fine to do for the best case size computation.
    double patch_size = TRY(
        merger.Context().patch_size_cache_for_init_font->GetPatchSize(glyphs));

    if (new_glyphs.empty()) {
      // For full patch removal we can estimate impact on the delta by assuming
      // that all of the data of the patch is moved to the init font and further
      // compressed by the best case reduction fraction.
      cost_delta += best_case_reduction * patch_size -
                    patch_probability * (patch_size + per_request_overhead);
      continue;
    }

    // For a changed patch the change in cost delta is:
    //
    // (best_case_reduction - patch_probability) * reduction in patch_size
    //
    // Since bytes are moved from the patch to the init font with estimated
    // reduction of best_case_reduction. The patch isn't being removed there's
    // no change due to per_request_overhead.
    //
    // For a changed patch the amount of bytes removed can vary from [0, patch
    // size], (Note: we can't compute the actual patch size change because it's
    // expensive).
    //
    // Then to estimate the best case we need to choose a value for reduction in
    // patch_size that creates the smallest delta. This choice depends on
    // whether patch_probability is > or <= best_case_reduction
    if (patch_probability > best_case_reduction) {
      // since (best_case_reduction - patch_probability) is negative, the
      // smallest delta arises when patch size reduction is as large as
      // possible, so equal to patch_size.
      //
      // TODO(garretrieger): XXXXX may want to incorporate a minimum patch size
      // amount since we're saying patch isn't removed.
      cost_delta += (best_case_reduction - patch_probability) * patch_size;
    }

    // Otherwise if patch_probability is <= best_case_reduction then the cost
    // delta will be positive unless patch size reduction is 0, in which chase
    // the change to total cost delta is also 0.
  }

  VLOG(1) << "best case cost_delta for move of glyphs "
          << moved_glyphs.ToString() << " to the initial font = " << cost_delta;

  return cost_delta;
}

template<bool best_case>
StatusOr<double> CandidateMerge::ComputeCostDelta(
      Merger& merger,
      const SegmentSet& merged_segments,
      const Segment& merged_segment,
      std::optional<GlyphSet> exclusive_gids
    ) {

  if (merged_segments.size() <= 1) {
    return 0;
  }

  // These are conditions which will be modified by applying the merge. These are
  // conditions that intersect merged_segments.
  //
  // Map value is the glyphs associated with condition.
  flat_hash_map<ActivationCondition, const GlyphSet*> modified_conditions;

  TRYV(FindModifiedConditions(merger, merged_segments, modified_conditions));

  // new_conditions is modified conditions updated to apply the segment merging.
  // May have less conditions than modified as multiple conditions may merge together
  // as a result of updating their segments.
  //
  // Map value is the set of glyphs and probability associated with the new condition.
  struct Info {
    GlyphSet glyphs;
    double probability = 0.0;
    uint32_t largest_patch_size = 0;
    uint32_t combined_patch_size = 0;
  };
  flat_hash_map<ActivationCondition, Info> new_conditions;
  new_conditions.reserve(modified_conditions.size());

  segment_index_t base = *merged_segments.min();
  const auto& context = merger.Context();
  const auto& patch_size_cache = context.patch_size_cache;
  const auto& segments = context.SegmentationInfo().Segments();
  const auto* calculator = merger.Strategy().ProbabilityCalculator();
  double cost_delta = 0.0;
  const uint32_t per_request_overhead = merger.Strategy().NetworkOverheadCost();
  for (const auto& [condition, glyphs] : modified_conditions) {
    uint32_t patch_size =
        TRY(patch_size_cache->GetPatchSize(*glyphs));
    double p = TRY(condition.Probability(segments, *calculator));
    double d = p * (patch_size + per_request_overhead);
    cost_delta -= d;
    VLOG(1) << "    - (" << p << " * " << (patch_size + per_request_overhead)
            << ") -> " << d << " [removed patch " << condition.ToString()
            << "]";

    ActivationCondition updated =
        condition.ReplaceSegments(base, merged_segments);
    if (updated.IsExclusive()) {
      // Clear is exclusive flag so that de-dup happens properly
      updated = ActivationCondition::clear_exclusive(std::move(updated));
    }
    auto [it, did_insert] = new_conditions.try_emplace(std::move(updated));

    auto& info = it->second;
    if (did_insert) {
      // Because we haven't actuated the merge yet (segments does not reflect
      // it), MergedProbability() is needed to correctly compute the new
      // probability.
      info.probability = TRY(it->first.MergedProbability(
          segments, base, merged_segment, *calculator));
    }

    if (!best_case) {
      info.glyphs.union_set(*glyphs);
    }

    info.largest_patch_size = std::max(info.largest_patch_size, patch_size);
    info.combined_patch_size += patch_size;
  }

  // Add in cost associated within the new conditions patch pairs.
  bool fallback_changed = false;
  double best_case_reduction_fraction = merger.Strategy().BestCaseSizeReductionFraction();
  for (auto& [c, info] : new_conditions) {
    // For modified conditions we assume the associated patch size does not
    // change, only the probability associated with the condition changes.
    double p = info.probability;

    uint32_t size = 0;
    if (best_case) {
      uint32_t extra = info.combined_patch_size - info.largest_patch_size;
      extra = std::max(
        (uint32_t)(extra * best_case_reduction_fraction),
        Merger::BEST_CASE_MERGE_SIZE_DELTA);
      size = info.largest_patch_size + extra;
    } else {
      if (exclusive_gids.has_value()) {
        // When we do not have complete glyph conditions then exclusive_gids
        // provides a hint at which glyphs will end up in the exclusive patch
        // after the merge, incorporate them into the size calculation here.
        // Remove them from all other patches and place them into the exclusive
        // patch.
        info.glyphs.subtract(*exclusive_gids);
        if (c.IsUnitary()) {
          info.glyphs.union_set(*exclusive_gids);
          if (context.glyph_groupings.UnmappedGlyphs().intersects(*exclusive_gids)) {
            fallback_changed = true;
          }
        }
      }
      size = TRY(patch_size_cache->GetPatchSize(info.glyphs));
      if (merger.ShouldRecordMergedSizeReductions()) {
        int32_t extra_raw = info.combined_patch_size - info.largest_patch_size;
        int32_t extra_actual = ((int32_t)size) - info.largest_patch_size;
        if (extra_raw != 0.0)  {
          merger.RecordMergedSizeReduction((double)extra_actual / (double)extra_raw);
        }
      }
    }

    double s = (size + per_request_overhead);
    double d = p * s;
    VLOG(1) << "    + (" << p << " * " << s << ") -> " << d
            << " [new patch " << c.ToString() << "]";
    cost_delta += d;
  }

  if (fallback_changed) {
    GlyphSet new_fallback = context.glyph_groupings.UnmappedGlyphs();
    new_fallback.subtract(*exclusive_gids);

    // Fallback is always needed with 100% probability so delta is just the size difference (new - old)
    double diff = ((double) TRY(patch_size_cache->GetPatchSize(new_fallback)))
      - ((double) TRY(patch_size_cache->GetPatchSize(context.glyph_groupings.UnmappedGlyphs())));
    VLOG(1) << "    + " << diff
            << " [fallback delta]";
    cost_delta += diff;
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

  if (WouldMixFeaturesAndCodepoints(merger.Context().SegmentationInfo(),
                                    base_segment_index, segments_to_merge_)) {
    // With the heuristic merger if it doesn't find a previous merge candidate
    // will try to merge together segments that are composed of codepoints with
    // a segment that adds an optional feature. Since this feature segments are
    // likely rarely used this will inflate the size of the patches for those
    // codepoint segments unnecessarily.
    //
    // In the cost case we don't have proper frequency data for features and
    // likewise don't have co-occurence data with features + codepoints. This
    // makes it difficult to make proper merging decisions that involve
    // mixing codepoints and features.
    //
    // As a result, don't do merges that mix features and codepoints in any
    // cases.
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
  bool segments_to_merge_and_base_are_inert =
      merger.Context().InertSegments().contains(base_segment_index) &&
      segments_to_merge_are_inert;

  const auto& segments = merger.Context().SegmentationInfo().Segments();

  Segment merged_segment = segments[base_segment_index];
  MergeSegments(merger, segments_to_merge, merged_segment);

  if (merger.Strategy().UseCosts() && best_merge_candidate.has_value()) {
    // Before doing a full assessment check a "best case" cost delta.
    // If that doesn't beat the current smallest there's no need to
    // do more indepth analysis.
    double best_case_delta = TRY(ComputeCostDelta<true>(
        merger, segments_to_merge_with_base, merged_segment, std::nullopt));
    if (best_case_delta >= best_merge_candidate->CostDelta()) {
      // We can't possibly beat the current lowest delta.
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
  std::optional<GlyphSet> exclusive_gids;
  if (!merger.Strategy().UseCosts() || merger.Context().GetConditionAnalysisMode() != config::DEP_GRAPH_ONLY) {
    if (!segments_to_merge_are_inert) {
      // When we're not in pure depgraph mode then glyph conditions are incomplete.
      // To help improve accuracy run a closure to find the new set of exclusive
      // glyphs associated with the merge and incorporate that into the cost calculations.
      GlyphSet and_gids, or_gids;
      exclusive_gids = GlyphSet {};
      TRYV(merger.Context().AnalyzeSegment(segments_to_merge_with_base, and_gids,
                                           or_gids, *exclusive_gids));
    } else {
      // When the segments being added to base are all inert then we can assume
      // the merged patch is just a combination of the glyphs from the base and
      // the input segments. Since the segments being added are intert we know
      // that they won't interact with base and will just bring along their own
      // glyphs.
      exclusive_gids = gid_conditions_to_update;
      exclusive_gids->union_set(
          merger.Context().glyph_groupings.ExclusiveGlyphs(base_segment_index));
    }
    new_patch_size =
          TRY(merger.Context().patch_size_cache->GetPatchSize(*exclusive_gids));
  }

  if (!merger.Strategy().UseCosts() &&
      new_patch_size > merger.Strategy().PatchSizeMaxBytes()) {
    return std::nullopt;
  }

  double cost_delta = 0.0;
  if (merger.Strategy().UseCosts()) {
    // Cost delta values are only needed when using cost based merge strategy.
    cost_delta = TRY(ComputeCostDelta<false>(merger, segments_to_merge_with_base,
                                      merged_segment, exclusive_gids));
  }

  if (best_merge_candidate.has_value() &&
      cost_delta >= best_merge_candidate.value().cost_delta_) {
    // Our delta is not smaller, don't bother returning a candidate.
    return std::nullopt;
  }

  CandidateMerge candidate(std::move(merged_segment));
  candidate.base_segment_index_ = base_segment_index;
  candidate.segments_to_merge_ = segments_to_merge;
  candidate.input_segments_are_inert_ = segments_to_merge_and_base_are_inert;
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
  candidate.input_segments_are_inert_ = false;
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
