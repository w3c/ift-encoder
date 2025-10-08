#include "ift/encoder/merger.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/int_set.h"
#include "ift/encoder/candidate_merge.h"

using absl::Status;
using absl::StatusOr;
using common::GlyphSet;
using common::SegmentSet;

namespace ift::encoder {

StatusOr<std::optional<std::pair<segment_index_t, GlyphSet>>>
Merger::TryNextMerge() {
  if (strategy_.IsNone()) {
    return std::nullopt;
  }

  // TODO(garretrieger): also consider moving the fallback segment into the
  // init font. We should be able to compute an associated cost delta and
  // should proceed if it's negative. Will need to reprocess the segmentation
  // can utilize the existing ReassignInitSubset() method.

  // TODO(garretrieger): special casing for handling multiple script frequency
  // data sets when segmenting for multiple scripts (specifically disjoint ones)
  // we essentially want to consider their frequencies in isolation from the
  // other scripts. For example if greek has a codepoint with 100% probability
  // and cyrillic has a codepoint with 100% probability those would normally be
  // considered a good candidate to merge, but we likely don't want to merge
  // those as most users in practice will be encountering only one of those two
  // scripts at a time. Very roughly I think this can be solved by keeping
  // multiple active segment sets (one per script) and during merging only
  // consider one set at a time. This will prevent merges across scripts. Idea
  // is early stage and definitely needs some more development.

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

Status Merger::MoveSegmentsToInitFont() {
  if (!strategy_.InitFontMergeThreshold().has_value()) {
    return absl::FailedPreconditionError(
        "Cannot be called when there is no merge threshold configured.");
  }

  VLOG(0) << "Checking if there are any segments which should be moved into "
             "the initial font.";

  SubsetDefinition initial_segment =
      Context().SegmentationInfo().InitFontSegment();
  bool change_made;
  do {
    // TODO(garretrieger): as an optimization probably want to avoid rechecking
    //   segments that have previously been assessed for move into init font.
    //   alternatively should be able to modify this to not immediately apply
    //   the move, but scan all options and collect the set of segments to move
    //   then repeat analysis until no changes.
    // TODO(garretrieger): consider reworking this using gids instead of
    //  codepoints. That is specify init font def in terms of gids. That will
    //  more closely match the intention of merging a specific patch into the
    //  init font. Will need to modify segmentation plan to support init font
    //  gid set.
    // TODO(garretrieger): should prune codepoints that are pulled into the init
    //   font from the segments. Will avoid having noop segments (segments
    //   with codepoints but no interactions/patches).
    SegmentSet to_check_individually =
        Context().glyph_groupings.AllDisjunctiveSegments();
    to_check_individually.intersect(candidate_segments_);

    SegmentSet excluded = CutoffSegments();
    to_check_individually.subtract(excluded);

    change_made = false;
    for (segment_index_t s : to_check_individually) {
      SegmentSet candidate_segments{s};
      change_made =
          TRY(CheckAndApplyInitFontMove(candidate_segments, initial_segment));
      if (change_made) {
        break;
      }
    }

    if (change_made) {
      continue;
    }

    for (const auto& [condition, _] :
         Context().glyph_groupings.ConditionsAndGlyphs()) {
      if (condition.conditions().size() <= 1 ||
          condition.TriggeringSegments().intersects(excluded)) {
        // All size 1 conditions are disjunctive and handled in the previous
        // loop. Since this is conjunction, having an excluded segment in it's
        // condition makes the probability near 0.
        continue;
      }

      change_made = TRY(CheckAndApplyInitFontMove(
          condition.TriggeringSegments(), initial_segment));
      if (change_made) {
        break;
      }
    }
  } while (change_made);

  VLOG(0) << "Initial font now has " << initial_segment.codepoints.size()
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
  for (unsigned i = 0; i < context.SegmentationInfo().Segments().size(); i++) {
    if (!context.SegmentationInfo().Segments()[i].Definition().Empty() &&
        inscope_segments.contains(i)) {
      candidate_segments.insert(i);
    }
  }

  return candidate_segments;
}

Status Merger::InitOptimizationCutoff() {
  if (strategy_.UseCosts()) {
    optimization_cutoff_segment_ = TRY(ComputeSegmentCutoff());
    if (optimization_cutoff_segment_ <
        context_->SegmentationInfo().Segments().size()) {
      VLOG(0) << "Cutting off optimization at segment "
              << optimization_cutoff_segment_ << ", P("
              << optimization_cutoff_segment_ << ") = "
              << context_->SegmentationInfo()
                     .Segments()[optimization_cutoff_segment_]
                     .Probability();
    } else {
      VLOG(0) << "No optimization cutoff.";
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

Status Merger::CollectExclusiveCandidateMerges(
    uint32_t base_segment_index,
    std::optional<CandidateMerge>& smallest_candidate_merge) {
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
    // TODO XXXX cutoff if all segments are above the cutoff threshold.
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

    if (next_condition.conditions().size() == 1) {
      // For disjunctive composite patches, also consider merging just the
      // patches together.
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
  return result;
}

StatusOr<bool> Merger::CheckAndApplyInitFontMove(
    const common::SegmentSet& candidate_segments,
    SubsetDefinition& initial_segment) {
  const double threshold = *strategy_.InitFontMergeThreshold();
  const double delta = TRY(CandidateMerge::ComputeCostDelta(
      *this, candidate_segments, std::nullopt, 0));

  if (delta >= threshold * (double)candidate_segments.size()) {
    // Merging doesn't improve cost, skip.
    return false;
  }

  VLOG(0) << "  Moving segments " << candidate_segments.ToString()
          << " into the initial font (cost delta = " << delta << ")";

  for (segment_index_t s : candidate_segments) {
    initial_segment.Union(
        Context().SegmentationInfo().Segments()[s].Definition());
  }

  TRYV(Context().ReassignInitSubset(initial_segment, candidate_segments));
  TRYV(ReassignInitSubset());

  return true;
}

}  // namespace ift::encoder