#include "ift/encoder/merger.h"

#include <optional>

#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/int_set.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/candidate_merge.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"

using absl::btree_map;
using absl::Status;
using absl::StatusOr;
using common::GlyphSet;
using common::SegmentSet;

ABSL_FLAG(bool, record_merged_size_reductions, false,
          "When enabled the merger will record the percent size reductions of "
          "each assessed merge.");

namespace ift::encoder {

bool Merger::ShouldRecordMergedSizeReductions() const {
  return absl::GetFlag(FLAGS_record_merged_size_reductions);
}

StatusOr<std::optional<std::pair<segment_index_t, GlyphSet>>>
Merger::TryNextMerge() {
  if (strategy_.IsNone()) {
    return std::nullopt;
  }

  // TODO(garretrieger): there's also the problem of overlapping scripts (eg.
  // CJK) that will need special casing. Very broad strokes idea is to assess
  // cost for each script individually and use the sum of the individual costs
  // as the overall cost.

  while (true) {
    auto it = candidate_segments_.cbegin();
    if (it == candidate_segments_.cend()) {
      break;
    }

    segment_index_t base_segment_index = *it;

    std::optional<GlyphSet> modified_gids;
    if (strategy_.UseCosts()) {
      modified_gids = TRY(MergeSegmentWithCosts(base_segment_index));
    } else {
      modified_gids = TRY(MergeSegmentWithHeuristic(base_segment_index));
    }

    if (modified_gids.has_value()) {
      return std::pair(base_segment_index, *modified_gids);
    }

    MarkFinished(base_segment_index);
  }

  return std::nullopt;
}

SegmentSet Merger::InitFontSegmentsToCheck(const SegmentSet& inscope) const {
  SegmentSet to_check = inscope;

  SegmentSet excluded = CutoffSegments();
  // Shared segments aren't subject to optimization cutoff. So only exclude
  // those in inscope_segments_ (which is all of the non-shared segments)
  excluded.intersect(inscope_segments_);
  to_check.subtract(excluded);

  return to_check;
}

SegmentSet Merger::InitFontApplyProbabilityThreshold() const {
  SegmentSet below_threshold;
  if (strategy_.InitFontMergeProbabilityThreshold().has_value()) {
    for (segment_index_t s : inscope_segments_for_init_move_) {
      const auto& seg = Context().SegmentationInfo().Segments().at(s);
      if (seg.Probability() < strategy_.InitFontMergeProbabilityThreshold()) {
        below_threshold.insert(s);
      }
    }
  }

  SegmentSet inscope = inscope_segments_for_init_move_;
  inscope.subtract(below_threshold);

  VLOG(0) << inscope.size() << " inscope segments, " << below_threshold.size()
          << " skipped for being below the probability threshold.";
  return inscope;
}

btree_map<ActivationCondition, GlyphSet> Merger::InitFontConditionsToCheck(
    const SegmentSet& to_check, bool batch_mode) const {
  // We only want to check conditions that use at least one segment which is
  // inscope for moving to the init font.
  btree_map<ActivationCondition, GlyphSet> conditions;
  for (segment_index_t s : to_check) {
    for (const auto& c :
         Context().glyph_groupings.TriggeringSegmentToConditions(s)) {
      if (conditions.contains(c)) {
        continue;
      }

      if (batch_mode) {
        SegmentSet triggering_segments = c.TriggeringSegments();
        if (triggering_segments.size() != 1 ||
            !Context().InertSegments().contains(*triggering_segments.begin())) {
          // Non-inert conditions are skipped during the batch processing.
          continue;
        }
      }

      GlyphSet glyphs = Context().glyph_groupings.ConditionsAndGlyphs().at(c);
      conditions.insert(std::make_pair(c, glyphs));
    }
  }
  return conditions;
}

Status Merger::MoveSegmentsToInitFont() {
  if (!strategy_.InitFontMergeThreshold().has_value()) {
    return absl::FailedPreconditionError(
        "Cannot be called when there is no merge threshold configured.");
  }

  VLOG(0) << "Checking if there are any segments which should be moved into "
             "the initial font.";

  SegmentSet inscope = InitFontApplyProbabilityThreshold();

  // Init move processing works in two phases:
  //
  // First is batch mode. In batch mode only inert segments are checked
  // for move. Any segments that are below the threshold are moved to the
  // init font in a single operation. Because inert segments are not
  // expected to interact we don't need to reform the closure analysis
  // after each individual move to get an accurate cost delta.
  //
  // Once batch processing has no more moves left, the processing switches
  // to non-batch processing where all candidate conditions are checked
  // and moved one at a time.

  bool batch_mode = true;
  VLOG(0) << " batch checking inert segments for move to init font.";
  do {
    SegmentSet to_check = InitFontSegmentsToCheck(inscope);

    uint32_t init_font_size =
        TRY(Context().patch_size_cache_for_init_font->GetPatchSize(
            Context().SegmentationInfo().InitFontGlyphs()));

    double total_delta = 0.0;
    double lowest_delta = *strategy_.InitFontMergeThreshold();
    std::optional<GlyphSet> glyphs_for_lowest = std::nullopt;

    btree_map<ActivationCondition, GlyphSet> conditions =
        InitFontConditionsToCheck(to_check, batch_mode);

    for (const auto& [condition, glyphs] : conditions) {
      auto [best_case_delta, _] = TRY(CandidateMerge::ComputeInitFontCostDelta(
          *this, init_font_size, true, glyphs));
      if (best_case_delta >= lowest_delta) {
        // Filter by best case first which is much faster to compute.
        continue;
      }

      auto [delta, all_glyphs] = TRY(CandidateMerge::ComputeInitFontCostDelta(
          *this, init_font_size, false, glyphs));
      if (delta >= lowest_delta) {
        continue;
      }

      if (batch_mode) {
        // In batch mode we accept any merges under the threshold instead of
        // finding the lowest.
        if (!glyphs_for_lowest.has_value()) {
          glyphs_for_lowest = GlyphSet{};
        }
        total_delta += delta;
        glyphs_for_lowest->union_set(all_glyphs);
      } else {
        lowest_delta = delta;
        total_delta = delta;
        glyphs_for_lowest = all_glyphs;
      }
    }

    if (!glyphs_for_lowest.has_value()) {
      if (batch_mode) {
        // Batch mode processing done, move on to non-batch processing.
        batch_mode = false;
        VLOG(0) << " switching to checking individually.";
        continue;
      } else {
        // No more moves to make.
        break;
      }
    }

    TRYV(ApplyInitFontMove(*glyphs_for_lowest, total_delta));
  } while (true);

  VLOG(0) << "Initial font now has "
          << Context().SegmentationInfo().InitFontSegment().codepoints.size()
          << " codepoints.";
  return absl::OkStatus();
}

Status Merger::ReassignInitSubset() {
  candidate_segments_ =
      ComputeCandidateSegments(Context(), strategy_, inscope_segments_);
  TRYV(InitOptimizationCutoff());
  return absl::OkStatus();
}

uint32_t Merger::AssignMergedSegment(segment_index_t base,
                                     const common::SegmentSet& to_merge,
                                     const Segment& merged_segment,
                                     bool is_inert) {
  candidate_segments_.subtract(to_merge);
  candidate_segments_.insert(base);
  return Context().AssignMergedSegment(base, to_merge, merged_segment,
                                       is_inert);
}

SegmentSet Merger::ComputeCandidateSegments(
    SegmentationContext& context, const MergeStrategy& strategy,
    const common::SegmentSet& inscope_segments) {
  SegmentSet candidate_segments;

  if (inscope_segments.size() < context.SegmentationInfo().Segments().size()) {
    for (segment_index_t s : inscope_segments) {
      if (s < context.SegmentationInfo().Segments().size() &&
          !context.SegmentationInfo().Segments()[s].Definition().Empty()) {
        candidate_segments.insert(s);
      }
    }
  } else {
    for (segment_index_t s = 0;
         s < context.SegmentationInfo().Segments().size(); s++) {
      if (inscope_segments.contains(s) &&
          !context.SegmentationInfo().Segments()[s].Definition().Empty()) {
        candidate_segments.insert(s);
      }
    }
  }

  return candidate_segments;
}

Status Merger::InitOptimizationCutoff() {
  if (strategy_.UseCosts()) {
    optimization_cutoff_segment_ = TRY(ComputeSegmentCutoff());
    if (optimization_cutoff_segment_ <
        context_->SegmentationInfo().Segments().size()) {
      VLOG(1) << "Cutting off optimization at segment "
              << optimization_cutoff_segment_ << ", P("
              << optimization_cutoff_segment_ << ") = "
              << context_->SegmentationInfo()
                     .Segments()[optimization_cutoff_segment_]
                     .Probability();
    } else {
      VLOG(1) << "No optimization cutoff.";
    }
  }
  return absl::OkStatus();
}

StatusOr<segment_index_t> Merger::ComputeSegmentCutoff() const {
  // For this computation to keep things simple we consider only exclusive
  // segments.
  //
  // Since this is just meant to compute a rough cutoff point below which
  // probabilites are too small to have any real impact on the final costs,
  // considering only exclusive segments is good enough for this calculation and
  // significantly simplifies things.

  // First compute the total cost for all active segments
  double total_cost = 0.0;
  double overhead = strategy_.NetworkOverheadCost();
  for (segment_index_t s : candidate_segments_) {
    auto segment_glyphs = context_->glyph_groupings.ExclusiveGlyphs(s);
    if (segment_glyphs.empty()) {
      continue;
    }

    double size = TRY(context_->patch_size_cache->GetPatchSize(segment_glyphs));
    double probability =
        context_->SegmentationInfo().Segments()[s].Probability();
    total_cost += probability * (size + overhead);
  }

  double cutoff_tail_cost = total_cost * strategy_.OptimizationCutoffFraction();
  segment_index_t previous_segment_index = UINT32_MAX;
  for (auto it = candidate_segments_.rbegin(); it != candidate_segments_.rend();
       it++) {
    segment_index_t s = *it;
    auto segment_glyphs = context_->glyph_groupings.ExclusiveGlyphs(s);
    if (segment_glyphs.empty()) {
      continue;
    }

    double size = TRY(context_->patch_size_cache->GetPatchSize(segment_glyphs));
    double probability =
        context_->SegmentationInfo().Segments()[s].Probability();
    cutoff_tail_cost -= probability * (size + overhead);
    if (cutoff_tail_cost < 0.0) {
      // This segment puts us above the cutoff, so set the cutoff as the
      // previous segment.
      return previous_segment_index;
    }

    previous_segment_index = s;
  }

  return previous_segment_index;
}

StatusOr<std::optional<GlyphSet>> Merger::MergeSegmentWithCosts(
    uint32_t base_segment_index) {
  // TODO(garretrieger): what we are trying to solve here is effectively
  // a partitioning problem (finding the partitioning with lowest cost) which is
  // NP.
  //
  // To make this tractable we use a simplistic greedy approach were we
  // iteratively select two (or more) segments to merge that lower the overall
  // cost. Currently this selects candidates from two sources:
  // 1. Start with the highest probability segment, evaluate the cost delta for
  //    merging it with every other segment. Once no more merges are found,
  //    move on to the next highest frequency.
  // 2. Consider merging the groups of segments that are known to interact as
  //    these might give slightly better results due to reduction of
  //    conditional patches.
  //
  // This approach can likely be improved:
  // - Consider all possible pairs instead of just pairs with the highest freq
  //   item.
  // - This could be made tractable by caching the pair wise cost deltas and
  //   invaldidating specific ones as needed on each merge.
  // - After forming an initial greedy based partition try to fine tune by
  //   randomly moving codepoints between the segments to see if further cost
  //   reductions can be realized. Can use a computaton budget to set a bound
  //   on how much time is spent here.
  //
  // Lastly, currently lacking a good set of frequency data for all unicode
  // codepoints this approach has not yet been thoroughly tested. Next steps
  // would be to gather some frequency data, test this approach as is, and then
  // refine it potentially using some of the proposals noted above.

  auto base_segment_glyphs =
      context_->glyph_groupings.ExclusiveGlyphs(base_segment_index);
  if (base_segment_glyphs.empty()) {
    // This base segment has no exclusive glyphs, there's no need to to compute
    // merges.
    return std::nullopt;
  }

  std::optional<CandidateMerge> smallest_candidate_merge;
  const auto& base_segment =
      context_->SegmentationInfo().Segments()[base_segment_index];
  bool min_group_size_met =
      base_segment.MeetsMinimumGroupSize(strategy_.MinimumGroupSize());
  if (min_group_size_met) {
    // If min group size is met, then we will no longer consider merge's that
    // have a positive cost delta so start with an existing smallest candidate
    // set to cost delta 0 which will filter out positive cost delta candidates.
    unsigned base_size =
        TRY(context_->patch_size_cache->GetPatchSize(base_segment_glyphs));
    smallest_candidate_merge = CandidateMerge::BaselineCandidate(
        base_segment_index, 0.0, base_size, base_segment.Probability(),
        strategy_.NetworkOverheadCost());
  }

  // TODO(garretrieger): On each iteration we should consider all merge pairs
  //  rather than limiting ourselves just to pairs involving a single
  //  base_segment_index. This will take some care to keep it performant
  //  however. We'd likely need a priority queue to cache deltas with a
  //  way of invalidating any pairs that are changed by each merge operation.
  TRYV(CollectExclusiveCandidateMerges(base_segment_index,
                                       smallest_candidate_merge));
  TRYV(CollectCompositeCandidateMerges(base_segment_index,
                                       smallest_candidate_merge));
  if (!smallest_candidate_merge.has_value()) {
    return std::nullopt;
  }

  if (smallest_candidate_merge->SegmentsToMerge() ==
      SegmentSet{base_segment_index}) {
    // nothing smaller than the baseline was found.
    return std::nullopt;
  }

  // Enforce a negative cost delta only if this segments has met the minimum
  // grouping size.
  if (min_group_size_met && smallest_candidate_merge->CostDelta() >= 0.0) {
    return std::nullopt;
  }

  return smallest_candidate_merge->Apply(*this);
}

double Merger::BestCaseInertProbabilityThreshold(
    uint32_t base_patch_size, double base_probability,
    double lowest_cost_delta) const {
  // The following assumptions are made:
  // - P(base) >= P(other)
  // - the best case merged size is max(base_size, other_size) + k
  //
  // Then if we start with the formula for the  cost delta of an inert merge:
  //
  // P(merged) * merged_size - P(base) * base_size - P(other) * other_size
  //
  // (here all sizes include the network overhead delta).
  //
  // And consider what valid values of P(merged), and other_size will produce
  // the lowest total delta we find that this happens when:
  // - P(merged) = P(base)
  // - other_size = base_size
  // - merged_size = base_size + k
  //
  // From that we find that the smallest possible delta is:
  //
  // min(cost delta) = P(base) * k - P(other) * base_size
  //
  // From which we find that:
  //
  // P(other) > (k * P(base) - lowest_cost_delta) / base_size
  base_patch_size += Strategy().NetworkOverheadCost();
  return std::min(1.0, std::max(0.0, (((double)BEST_CASE_MERGE_SIZE_DELTA) *
                                          base_probability -
                                      lowest_cost_delta) /
                                         ((double)base_patch_size)));
}

Status Merger::CollectExclusiveCandidateMerges(
    uint32_t base_segment_index,
    std::optional<CandidateMerge>& smallest_candidate_merge) {
  auto base_glyphs =
      context_->glyph_groupings.ExclusiveGlyphs(base_segment_index);
  uint32_t base_size =
      TRY(Context().patch_size_cache->GetPatchSize(base_glyphs));
  double base_probability = Context()
                                .SegmentationInfo()
                                .Segments()
                                .at(base_segment_index)
                                .Probability();

  double inert_threshold = -1.0;
  if (smallest_candidate_merge.has_value()) {
    inert_threshold = BestCaseInertProbabilityThreshold(
        base_size, base_probability, smallest_candidate_merge->CostDelta());
  }

  for (auto it = candidate_segments_.lower_bound(base_segment_index);
       it != candidate_segments_.end(); it++) {
    if (*it == base_segment_index) {
      continue;
    }

    segment_index_t segment_index = *it;

    if (segment_index >= optimization_cutoff_segment_ &&
        smallest_candidate_merge.has_value()) {
      // We are at the optimization cutoff, so we won't evaluate any further
      // candidates beyond what is need to select at least one. Since a
      // candidate already exists, we can stop here.
      return absl::OkStatus();
    }

    if (context_->InertSegments().contains(segment_index) &&
        context_->SegmentationInfo()
                .Segments()
                .at(segment_index)
                .Probability() <= inert_threshold) {
      // Since we iteration is in probability order from highest to lowest, once
      // one segment fails the threshold then we know all further ones will as
      // well.
      break;
    }

    auto segment_glyphs =
        context_->glyph_groupings.ExclusiveGlyphs(segment_index);
    if (segment_glyphs.empty()) {
      // This segment has no exclusive glyphs, so no need to consider it for a
      // merge.
      continue;
    }

    SegmentSet triggering_segments{segment_index};
    auto candidate_merge = TRY(CandidateMerge::AssessSegmentMerge(
        *this, base_segment_index, triggering_segments,
        smallest_candidate_merge));
    if (candidate_merge.has_value()) {
      smallest_candidate_merge = *candidate_merge;
      inert_threshold = BestCaseInertProbabilityThreshold(
          base_size, base_probability, smallest_candidate_merge->CostDelta());
    }
  }

  return absl::OkStatus();
}

Status Merger::CollectCompositeCandidateMerges(
    uint32_t base_segment_index,
    std::optional<CandidateMerge>& smallest_candidate_merge) {
  if (base_segment_index >= optimization_cutoff_segment_) {
    // We are at the optimization cutoff, so we won't evaluate any composite
    // candidates
    return absl::OkStatus();
  }

  // Composite conditions are always ordered after exclusive in the conditions
  // list. So start iteration from the last possible exclusive condition.
  ActivationCondition last_exclusive =
      ActivationCondition::exclusive_segment(UINT32_MAX, 0);

  for (auto it = context_->glyph_groupings.ConditionsAndGlyphs().lower_bound(
           last_exclusive);
       it != context_->glyph_groupings.ConditionsAndGlyphs().end(); it++) {
    const auto& next_condition = it->first;
    if (next_condition.IsFallback() || next_condition.IsExclusive()) {
      // Merging the fallback will cause all segments to be merged into one,
      // which is undesirable so don't consider the fallback. Also skip
      // any non composite conditions.
      continue;
    }

    SegmentSet triggering_segments = next_condition.TriggeringSegments();

    std::optional<unsigned> min = triggering_segments.min();
    if (min.has_value() && min >= optimization_cutoff_segment_) {
      // Don't consider merges where all triggering segment are cutoff
      // the probability of these is too low to significantly impact overall
      // cost
      continue;
    }

    if (!triggering_segments.intersects(candidate_segments_) ||
        !triggering_segments.is_subset_of(inscope_segments_)) {
      // At least one active segment must be present, otherwise this is a
      // condition that's already been considered and rejected. Additionally,
      // all triggering segments must be inscope otherwise this merge crosses
      // merge group boundaries.
      continue;
    }

    auto candidate_merge = TRY(CandidateMerge::AssessSegmentMerge(
        *this, base_segment_index, triggering_segments,
        smallest_candidate_merge));
    if (candidate_merge.has_value()) {
      smallest_candidate_merge = *candidate_merge;
    }

    if (strategy_.UsePatchMerges() && next_condition.conditions().size() == 1) {
      // For disjunctive composite patches, also consider merging just the
      // patches together (if enabled).
      auto candidate_merge = TRY(CandidateMerge::AssessPatchMerge(
          *this, base_segment_index, triggering_segments,
          smallest_candidate_merge));
      if (candidate_merge.has_value()) {
        smallest_candidate_merge = *candidate_merge;
      }
    }
  }
  return absl::OkStatus();
}

StatusOr<std::optional<GlyphSet>> Merger::MergeSegmentWithHeuristic(
    uint32_t base_segment_index) {
  auto base_segment_glyphs =
      context_->glyph_groupings.ExclusiveGlyphs(base_segment_index);
  if (base_segment_glyphs.empty() ||
      !TRY(CandidateMerge::IsPatchTooSmall(*this, base_segment_index,
                                           base_segment_glyphs))) {
    // Patch is big enough, no merge is needed.
    return std::nullopt;
  }

  auto modified_gids = TRY(TryMergingACompositeCondition(base_segment_index));
  if (modified_gids.has_value()) {
    // Return to the parent method so it can reanalyze and reform groups
    return *modified_gids;
  }

  modified_gids = TRY(TryMergingABaseSegment(base_segment_index));
  if (modified_gids.has_value()) {
    // Return to the parent method so it can reanalyze and reform groups
    return *modified_gids;
  }

  VLOG(0) << "Unable to get segment " << base_segment_index
          << " above minimum size. Continuing to next segment.";
  return std::nullopt;
}

StatusOr<std::optional<GlyphSet>> Merger::TryMergingABaseSegment(
    segment_index_t base_segment_index) {
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

  auto next_segment_it = candidate_segments_.lower_bound(base_segment_index);
  if (next_segment_it != candidate_segments_.end() &&
      *next_segment_it == base_segment_index) {
    next_segment_it++;
  }

  while (next_segment_it != candidate_segments_.end()) {
    SegmentSet triggering_segments{*next_segment_it};

    auto modified_gids = TRY(TryMerge(base_segment_index, triggering_segments));
    if (!modified_gids.has_value()) {
      next_segment_it++;
      continue;
    }

    VLOG(0) << "  Merging segments from base patch into segment "
            << base_segment_index << ": " << triggering_segments.ToString();
    return modified_gids;
  }

  return std::nullopt;
}

StatusOr<std::optional<GlyphSet>> Merger::TryMergingACompositeCondition(
    segment_index_t base_segment_index) {
  auto candidate_conditions =
      context_->glyph_groupings.TriggeringSegmentToConditions(
          base_segment_index);
  ActivationCondition base_condition =
      ActivationCondition::exclusive_segment(base_segment_index, UINT32_MAX);

  for (ActivationCondition next_condition : candidate_conditions) {
    if (next_condition.IsFallback()) {
      // Merging the fallback will cause all segments to be merged into one,
      // which is undesirable so don't consider the fallback.
      continue;
    }

    if (next_condition < base_condition) {
      // all conditions before base_condition are already processed, so we only
      // want to search after base_condition.
      continue;
    }

    SegmentSet triggering_segments = next_condition.TriggeringSegments();
    if (!triggering_segments.contains(base_segment_index) ||
        !triggering_segments.is_subset_of(inscope_segments_)) {
      continue;
    }

    auto modified_gids = TRY(TryMerge(base_segment_index, triggering_segments));
    if (!modified_gids.has_value()) {
      continue;
    }

    VLOG(0) << "  Merging segments from composite patch into segment "
            << base_segment_index << ": " << next_condition.ToString();
    return modified_gids;
  }

  return std::nullopt;
}

StatusOr<std::optional<GlyphSet>> Merger::TryMerge(
    segment_index_t base_segment_index, const SegmentSet& to_merge_segments_) {
  // TODO(garretrieger): extensions/improvements that could be made:
  // - Can we reduce # of closures for the additional conditions checks?
  //   - is the full analysis needed to get the or set?
  // - Use merging and/or duplication to ensure minimum patch size.
  //   - composite patches (NOT STARTED)
  // - Multi segment combination testing with GSUB dep analysis to guide.

  // Attempt to merge to_merge_semgents into base_segment_index. If maximum
  // patch size would be exceeded does not merge and returns nullopt.
  //
  // Otherwise the segment definitions are merged and any affected downstream
  // info (glyph conditions and glyph groupings) are invalidated. The set of
  // invalidated glyph ids is returned.
  auto maybe_candidate_merge = TRY(CandidateMerge::AssessSegmentMerge(
      *this, base_segment_index, to_merge_segments_, std::nullopt));
  if (!maybe_candidate_merge.has_value()) {
    return std::nullopt;
  }
  auto& candidate_merge = maybe_candidate_merge.value();
  return candidate_merge.Apply(*this);
}

SegmentSet Merger::CutoffSegments() const {
  common::SegmentSet result;

  unsigned num_segments = context_->SegmentationInfo().Segments().size();
  segment_index_t start = optimization_cutoff_segment_;
  if (!num_segments || start > num_segments - 1) {
    return result;
  }

  result.insert_range(start, num_segments - 1);
  result.intersect(inscope_segments_);
  return result;
}

Status Merger::ApplyInitFontMove(const GlyphSet& glyphs_to_move, double delta) {
  VLOG(0) << "  Moving " << glyphs_to_move.size()
          << " glyphs into the initial font (cost delta = " << delta << ")";

  SubsetDefinition initial_segment =
      Context().SegmentationInfo().InitFontSegmentWithoutDefaults();
  initial_segment.gids.union_set(glyphs_to_move);

  TRYV(Context().ReassignInitSubset(initial_segment));
  TRYV(ReassignInitSubset());

  return absl::OkStatus();
}

void Merger::LogMergedSizeHistogram() const {
  if (!ShouldRecordMergedSizeReductions()) {
    return;
  }

  std::stringstream histogram_string;
  histogram_string << "reduction_percent, count" << std::endl;
  for (const auto [percent, count] : merged_size_reduction_histogram_) {
    histogram_string << percent << ", " << count << std::endl;
  }
  VLOG(0) << "Merged Size Reduction Histogram for "
          << strategy_.Name().value_or("unamed") << std::endl
          << histogram_string.str();
}

}  // namespace ift::encoder