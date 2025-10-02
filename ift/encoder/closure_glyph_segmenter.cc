#include "ift/encoder/closure_glyph_segmenter.h"

#include <algorithm>
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
#include "common/woff2.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/candidate_merge.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/merge_strategy.h"
#include "ift/encoder/patch_size_cache.h"
#include "ift/encoder/segmentation_context.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"
#include "ift/freq/probability_calculator.h"
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
using common::Woff2;
using ift::GlyphKeyedDiff;
using ift::freq::ProbabilityCalculator;

namespace ift::encoder {

// An indepth description of how this segmentation implementation works can
// be found in ../../docs/closure_glyph_segmentation.md.

Status CheckForDisjointCodepoints(
    const std::vector<SubsetDefinition>& subset_definitions) {
  CodepointSet union_of_codepoints;
  for (const auto& def : subset_definitions) {
    CodepointSet intersection = def.codepoints;
    intersection.intersect(union_of_codepoints);
    if (!intersection.empty()) {
      return absl::InvalidArgumentError(
          "Input subset definitions must have disjoint codepoint sets when "
          "using cost-based merging.");
    }
    union_of_codepoints.union_set(def.codepoints);
  }
  return absl::OkStatus();
}

// TODO(garretrieger): extensions/improvements that could be made:
// - Can we reduce # of closures for the additional conditions checks?
//   - is the full analysis needed to get the or set?
// - Use merging and/or duplication to ensure minimum patch size.
//   - composite patches (NOT STARTED)
// - Multi segment combination testing with GSUB dep analysis to guide.

// Attempt to merge to_merge_semgents into base_segment_index. If maximum
// patch size would be exceeded does not merge and returns nullopt.
//
// Otherwise the segment definitions are merged and any affected downstream info
// (glyph conditions and glyph groupings) are invalidated. The set of
// invalidated glyph ids is returned.
StatusOr<std::optional<GlyphSet>> TryMerge(
    SegmentationContext& context, segment_index_t base_segment_index,
    const SegmentSet& to_merge_segments_) {
  auto maybe_candidate_merge = TRY(CandidateMerge::AssessSegmentMerge(
      context, base_segment_index, to_merge_segments_, std::nullopt));
  if (!maybe_candidate_merge.has_value()) {
    return std::nullopt;
  }
  auto& candidate_merge = maybe_candidate_merge.value();
  return candidate_merge.Apply(context);
}

/*
 * Search for a composite condition which can be merged into base_segment_index.
 *
 * Returns the set of glyphs invalidated by the merge if found and the merge
 * suceeded.
 */
StatusOr<std::optional<GlyphSet>> TryMergingACompositeCondition(
    SegmentationContext& context, segment_index_t base_segment_index) {
  auto candidate_conditions =
      context.glyph_groupings.TriggeringSegmentToConditions(base_segment_index);
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
StatusOr<std::optional<GlyphSet>> TryMergingABaseSegment(
    SegmentationContext& context, segment_index_t base_segment_index) {
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

  auto next_segment_it =
      context.ActiveSegments().lower_bound(base_segment_index);
  if (next_segment_it != context.ActiveSegments().end() &&
      *next_segment_it == base_segment_index) {
    next_segment_it++;
  }

  while (next_segment_it != context.ActiveSegments().end()) {
    SegmentSet triggering_segments{*next_segment_it};

    auto modified_gids =
        TRY(TryMerge(context, base_segment_index, triggering_segments));
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

/*
 * Attempts to merge base_segment_index.
 *
 * If a merge was performed returns the segment which was modified to allow
 * groupings to be updated.
 *
 * This uses a hueristic approach for locating candidate segments to merge.
 */

StatusOr<std::optional<GlyphSet>> MergeSegmentWithHeuristic(
    SegmentationContext& context, uint32_t base_segment_index) {
  auto base_segment_glyphs =
      context.glyph_groupings.ExclusiveGlyphs(base_segment_index);
  if (base_segment_glyphs.empty() ||
      !TRY(CandidateMerge::IsPatchTooSmall(context, base_segment_index,
                                           base_segment_glyphs))) {
    // Patch is big enough, no merge is needed.
    return std::nullopt;
  }

  auto modified_gids =
      TRY(TryMergingACompositeCondition(context, base_segment_index));
  if (modified_gids.has_value()) {
    // Return to the parent method so it can reanalyze and reform groups
    return *modified_gids;
  }

  modified_gids = TRY(TryMergingABaseSegment(context, base_segment_index));
  if (modified_gids.has_value()) {
    // Return to the parent method so it can reanalyze and reform groups
    return *modified_gids;
  }

  VLOG(0) << "Unable to get segment " << base_segment_index
          << " above minimum size. Continuing to next segment.";
  return std::nullopt;
}

Status CollectCompositeCandidateMerges(
    SegmentationContext& context, uint32_t base_segment_index,
    std::optional<CandidateMerge>& smallest_candidate_merge) {
  if (base_segment_index >= context.OptimizationCutoffSegment()) {
    // We are at the optimization cutoff, so we won't evaluate any composite
    // candidates
    return absl::OkStatus();
  }

  // Composite conditions are always ordered after exclusive in the conditions
  // list. So start iteration from the last possible exclusive condition.
  ActivationCondition last_exclusive =
      ActivationCondition::exclusive_segment(UINT32_MAX, 0);

  for (auto it = context.glyph_groupings.ConditionsAndGlyphs().lower_bound(
           last_exclusive);
       it != context.glyph_groupings.ConditionsAndGlyphs().end(); it++) {
    const auto& next_condition = it->first;
    if (next_condition.IsFallback() || next_condition.IsExclusive()) {
      // Merging the fallback will cause all segments to be merged into one,
      // which is undesirable so don't consider the fallback. Also skip
      // any non composite conditions.
      continue;
    }

    SegmentSet triggering_segments = next_condition.TriggeringSegments();
    if (!triggering_segments.intersects(context.ActiveSegments())) {
      // At least one active segment must be present, otherwise we can assume
      // the composites probability is too low to contribute significantly to
      // cost optimization.
      continue;
    }

    auto candidate_merge = TRY(CandidateMerge::AssessSegmentMerge(
        context, base_segment_index, triggering_segments,
        smallest_candidate_merge));
    if (candidate_merge.has_value()) {
      smallest_candidate_merge = *candidate_merge;
    }

    if (next_condition.conditions().size() == 1) {
      // For disjunctive composite patches, also consider merging just the
      // patches together.
      auto candidate_merge = TRY(CandidateMerge::AssessPatchMerge(
          context, base_segment_index, triggering_segments,
          smallest_candidate_merge));
      if (candidate_merge.has_value()) {
        smallest_candidate_merge = *candidate_merge;
      }
    }
  }
  return absl::OkStatus();
}

Status CollectExclusiveCandidateMerges(
    SegmentationContext& context, uint32_t base_segment_index,
    std::optional<CandidateMerge>& smallest_candidate_merge) {
  for (auto it = context.ActiveSegments().lower_bound(base_segment_index);
       it != context.ActiveSegments().end(); it++) {
    if (*it == base_segment_index) {
      continue;
    }

    segment_index_t segment_index = *it;

    if (segment_index >= context.OptimizationCutoffSegment() &&
        smallest_candidate_merge.has_value()) {
      // We are at the optimization cutoff, so we won't evaluate any further
      // candidates beyond what is need to select at least one. Since a
      // candidate already exists, we can stop here.
      return absl::OkStatus();
    }

    auto segment_glyphs =
        context.glyph_groupings.ExclusiveGlyphs(segment_index);
    if (segment_glyphs.empty()) {
      // This segment has no exclusive glyphs, so no need to consider it for a
      // merge.
      continue;
    }

    SegmentSet triggering_segments{segment_index};
    auto candidate_merge = TRY(CandidateMerge::AssessSegmentMerge(
        context, base_segment_index, triggering_segments,
        smallest_candidate_merge));
    if (candidate_merge.has_value()) {
      smallest_candidate_merge = *candidate_merge;
    }
  }
  return absl::OkStatus();
}

/*
 * Checks the cost of all possible merges with start_segment and perform
 * the merge that has the lowest negative cost delta.
 */
StatusOr<std::optional<GlyphSet>> MergeSegmentWithCosts(
    SegmentationContext& context, uint32_t base_segment_index) {
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
  // Additional areas for improvement:
  // - Our input data has per segment (or codepoint) probability data, but does
  //   not at the moment contain co-occurrence probabilities, so when assessing
  //   segment probabilities we must either work with lower, upper probability
  //   bounds, or make the assumption that probabilities are independent (which
  //   is almost certainly not true). All three approaches result in a cost
  //   function which is not fully accurate.
  // - This approach could be modified to utilize code point pair probabilities
  //   to produce more accurate bounds via Boole's Inequality
  //   (https://en.wikipedia.org/wiki/Boole%27s_inequality)
  //
  // Lastly, currently lacking a good set of frequency data for all unicode
  // codepoints this approach has not yet been thoroughly tested. Next steps
  // would be to gather some frequency data, test this approach as is, and then
  // refine it potentially using some of the proposals noted above.

  auto base_segment_glyphs =
      context.glyph_groupings.ExclusiveGlyphs(base_segment_index);
  if (base_segment_glyphs.empty()) {
    // This base segment has no exclusive glyphs, there's no need to to compute
    // merges.
    return std::nullopt;
  }

  std::optional<CandidateMerge> smallest_candidate_merge;
  const auto& base_segment =
      context.SegmentationInfo().Segments()[base_segment_index];
  bool min_group_size_met = base_segment.MeetsMinimumGroupSize(
      context.GetMergeStrategy().MinimumGroupSize());
  if (min_group_size_met) {
    // If min group size is met, then we will no longer consider merge's that
    // have a positive cost delta so start with an existing smallest candidate
    // set to cost delta 0 which will filter out positive cost delta candidates.
    unsigned base_size =
        TRY(context.patch_size_cache->GetPatchSize(base_segment_glyphs));
    smallest_candidate_merge = CandidateMerge::BaselineCandidate(
        base_segment_index, 0.0, base_size, base_segment.Probability(),
        context.GetMergeStrategy().NetworkOverheadCost());
  }

  // TODO(garretrieger): On each iteration we should consider all merge pairs
  //  rather than limiting ourselves just to pairs involving a single
  //  base_segment_index. This will take some care to keep it performant
  //  however. We'd likely need a priority queue to cache deltas with a
  //  way of invalidating any pairs that are changed by each merge operation.
  TRYV(CollectExclusiveCandidateMerges(context, base_segment_index,
                                       smallest_candidate_merge));
  TRYV(CollectCompositeCandidateMerges(context, base_segment_index,
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

  return smallest_candidate_merge->Apply(context);
}

/*
 * Searches segments starting from start_segment and attempts to merge following
 * the configured strategy.
 *
 * If a merge was performed returns the segment and glyphs which were modified
 * to allow groupings to be updated.
 */
StatusOr<std::optional<std::pair<segment_index_t, GlyphSet>>>
MergeNextBaseSegment(SegmentationContext& context) {
  if (context.GetMergeStrategy().IsNone()) {
    return std::nullopt;
  }

  // TODO(garretrieger): also consider moving the fallback segment into the
  // init font. We should be able to compute an associated cost delta and
  // should proceed if it's negative. Will need to reprocess the segmentation
  // can utilize the existing ReassignInitSubset() method.

  // TODO(garretrieger): merges are currently only done by merging two
  // or more segment subset definitions together. However, there's a
  // more granular type of merge possible where two patches are merged:
  // the new patch has glyphs from both patches and it's conditions
  // are a union of the two patches. However, the participating segment
  // definitions aren't merged.
  //
  // To illustrate where this is useful consider this case:
  //
  // P(s1) = 100%
  // P(s2) = 100%
  // P(s3) =   1%
  // P(s4) =   1%
  //
  // With patches
  // s1 -> p0
  // s3 -> p1
  // s4 -> p2
  // (s2 OR s3 OR s4) -> p3
  //
  // Ideally we want to merge p3 and p0 since both have 100% probability
  // but we don't want to also pull in s3 and s4 with their associated
  // patches as those are low probability. If we limit ourselves to
  // only merging segment definitions then it's not possible to merge
  // p3 and p0 without also merging in p1 and p2.
  //
  // However, if we take the more granular approach the mapping can be
  // modifed to:
  //
  // P(s1) = 100%
  // P(s2) = 100%
  // P(s3) =   1%
  // P(s4) =   1%
  //
  // With patches
  // (s1 or s2 or s3 or s4) -> p0 + p3
  // s3 -> p1
  // s4 -> p2
  //
  // Here's a rough plan for how this capability could be added into the
  // existing code:
  // - Introduce a second type of merge that is considered called a "glyph
  // union".
  // - In the glyph groupings datastructure we keep a union find structure that
  //   stores groupings of glyph ids.
  // - When producing the or_glyphs groups if the glyph being categorized is
  // part
  //   of a group in the union find then expand the condidtion set to include
  //   all conditions on all glyphs in the group.
  // - In the above example we'd put the glyphs from p0 and p3 into a union
  // - Then the conditions s1 -> p0, (s2 OR s3 OR s4) -> p3 will match the union
  //   and both condition sets will be expanded out to the superset (s1 or s2 or
  //   s3 or s4) creating a single combined patch.
  // - Cost delta computation will need to be updated to be able to assess this
  // case.
  // - There's a small complication that glyph sets might change (eg. s1 get's
  // expanded
  //   so p0 gets bigger). All new glyphs in p0 will need to be considered to be
  //   in the union. This can be handled by doing grouping in two phases, first
  //   form the unmodified groupings, then expand them using the union find.

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
    auto it = context.ActiveSegments().cbegin();
    if (it == context.ActiveSegments().cend()) {
      break;
    }

    segment_index_t base_segment_index = *it;

    std::optional<GlyphSet> modified_gids;
    if (context.GetMergeStrategy().UseCosts()) {
      modified_gids = TRY(MergeSegmentWithCosts(context, base_segment_index));
    } else {
      modified_gids =
          TRY(MergeSegmentWithHeuristic(context, base_segment_index));
    }

    if (modified_gids.has_value()) {
      return std::pair(base_segment_index, *modified_gids);
    }

    context.MarkFinished(base_segment_index);
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
      face, context.SegmentationInfo().InitFontSegment(),
      context.SegmentationInfo().Segments(), MergeStrategy::None());

  // Compute the glyph groupings/conditions from scratch to compare against the
  // incrementall produced ones.
  for (segment_index_t segment_index = 0;
       segment_index < context.SegmentationInfo().Segments().size();
       segment_index++) {
    TRY(non_incremental_context.ReprocessSegment(segment_index));
  }

  // Transfer over information on combined patches
  for (const GlyphSet& group :
       TRY(context.glyph_groupings.CombinedPatches().NonIdentityGroups())) {
    TRYV(non_incremental_context.glyph_groupings.CombinePatches(group, {}));
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

  if (non_incremental_context.glyph_groupings != context.glyph_groupings) {
    return absl::FailedPreconditionError("glyph groups aren't correct.");
  }

  return absl::OkStatus();
}

static StatusOr<std::vector<Segment>> ToSegments(
    const std::vector<SubsetDefinition>& subset_definitions,
    const MergeStrategy& merge_strategy) {
  auto calculator = merge_strategy.ProbabilityCalculator();
  std::vector<Segment> segments;
  for (const auto& def : subset_definitions) {
    auto probability = calculator->ComputeProbability(def);
    segments.emplace_back(def, probability);
  }
  if (merge_strategy.UseCosts()) {
    // Cost based merging has probability data available for segments, use that
    // to sort from highest to lowest. Later processing relies on this ordering.
    std::sort(segments.begin(), segments.end(),
              [](const Segment& a, const Segment& b) {
                if (a.Probability() != b.Probability()) {
                  return a.Probability() > b.Probability();
                }
                if (a.Definition().codepoints != b.Definition().codepoints) {
                  return a.Definition().codepoints < b.Definition().codepoints;
                }
                return a.Definition().feature_tags <
                       b.Definition().feature_tags;
              });
  }
  return segments;
}

static StatusOr<bool> CheckAndApplyInitFontMove(
    const SegmentSet& candidate_segments, SegmentationContext& context,
    SubsetDefinition& initial_segment) {
  const double threshold = *context.GetMergeStrategy().InitFontMergeThreshold();
  const double delta = TRY(CandidateMerge::ComputeCostDelta(
      context, candidate_segments, std::nullopt, 0));

  if (delta >= threshold * (double)candidate_segments.size()) {
    // Merging doesn't improve cost, skip.
    return false;
  }

  VLOG(0) << "  Moving segments " << candidate_segments.ToString()
          << " into the initial font (cost delta = " << delta << ")";

  for (segment_index_t s : candidate_segments) {
    initial_segment.Union(
        context.SegmentationInfo().Segments()[s].Definition());
  }

  TRYV(context.ReassignInitSubset(initial_segment, candidate_segments));
  return true;
}

// This method analyzes the segments and checks to see if any should be
// moved into the initial font.
//
// The common example where this is useful is for segments that have 100%
// probability. Since these are always needed, the most efficient thing to
// do is to move them into the initial font so they are already loaded
// without needing to be part of a patch.
//
// The approach is fairly straightforward: iterate through all of the
// conditions/patches and compute a cost delta for moving that patch
// into the init font. Move only those cases whose delta is below a
// configurable threshold.
Status ClosureGlyphSegmenter::MoveSegmentsToInitFont(
    SegmentationContext& context) const {
  if (!context.GetMergeStrategy().InitFontMergeThreshold().has_value()) {
    return absl::FailedPreconditionError(
        "Cannot be called when there is no merge threshold configured.");
  }

  VLOG(0) << "Checking if there are any segments which should be moved into "
             "the initial font.";

  SubsetDefinition initial_segment =
      context.SegmentationInfo().InitFontSegment();
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
        context.glyph_groupings.AllDisjunctiveSegments();
    SegmentSet excluded = context.CutoffSegments();
    to_check_individually.subtract(excluded);

    change_made = false;
    for (segment_index_t s : to_check_individually) {
      SegmentSet candidate_segments{s};
      change_made = TRY(CheckAndApplyInitFontMove(candidate_segments, context,
                                                  initial_segment));
      if (change_made) {
        break;
      }
    }

    if (change_made) {
      continue;
    }

    for (const auto& [condition, _] :
         context.glyph_groupings.ConditionsAndGlyphs()) {
      if (condition.conditions().size() <= 1 ||
          condition.TriggeringSegments().intersects(excluded)) {
        // All size 1 conditions are disjunctive and handled in the previous
        // loop. Since this is conjunction, having an excluded segment in it's
        // condition makes the probability near 0.
        continue;
      }

      change_made = TRY(CheckAndApplyInitFontMove(
          condition.TriggeringSegments(), context, initial_segment));
      if (change_made) {
        break;
      }
    }
  } while (change_made);

  VLOG(0) << "Initial font now has " << initial_segment.codepoints.size()
          << " codepoints.";
  return absl::OkStatus();
}

StatusOr<GlyphSegmentation> ClosureGlyphSegmenter::CodepointToGlyphSegments(
    hb_face_t* face, SubsetDefinition initial_segment,
    const std::vector<SubsetDefinition>& subset_definitions,
    std::optional<MergeStrategy> strategy) const {
  MergeStrategy merge_strategy = MergeStrategy::None();
  if (strategy.has_value()) {
    merge_strategy = std::move(*strategy);
  }

  if (merge_strategy.UseCosts()) {
    TRYV(CheckForDisjointCodepoints(subset_definitions));
  }

  std::vector<Segment> segments =
      TRY(ToSegments(subset_definitions, merge_strategy));
  SegmentationContext context = TRY(InitializeSegmentationContext(
      face, initial_segment, std::move(segments), std::move(merge_strategy)));

  if (context.GetMergeStrategy().IsNone()) {
    // No merging will be needed so we're done.
    return context.ToGlyphSegmentation();
  }

  // ### First phase of merging is to check for any patches which should be
  // moved to the initial font
  //     (eg. cases where the probability of a patch is ~1.0).
  if (context.GetMergeStrategy().UseCosts() &&
      context.GetMergeStrategy().InitFontMergeThreshold().has_value()) {
    TRYV(MoveSegmentsToInitFont(context));
  }

  // ### Iteratively merge segments and incrementally reprocess affected data.
  segment_index_t last_merged_segment_index = 0;
  while (true) {
    auto merged = TRY(MergeNextBaseSegment(context));

    if (!merged.has_value()) {
      // Nothing was merged so we're done.
      TRYV(ValidateIncrementalGroupings(face, context));
      return context.ToGlyphSegmentation();
    }

    const auto& [merged_segment_index, modified_gids] = *merged;
    last_merged_segment_index = merged_segment_index;

    GlyphSet analysis_modified_gids;
    if (!context.InertSegments().contains(last_merged_segment_index)) {
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

StatusOr<SegmentationContext>
ClosureGlyphSegmenter::InitializeSegmentationContext(
    hb_face_t* face, SubsetDefinition initial_segment,
    std::vector<Segment> segments, MergeStrategy merge_strategy) const {
  uint32_t glyph_count = hb_face_get_glyph_count(face);
  if (!glyph_count) {
    return absl::InvalidArgumentError("Provided font has no glyphs.");
  }

  // The IFT compiler has a set of defaults always included in the initial font
  // add them here so we correctly factor them into the generated segmentation.
  AddInitSubsetDefaults(initial_segment);

  // No merging is done during init.
  SegmentationContext context(face, initial_segment, segments,
                              std::move(merge_strategy));

  // ### Generate the initial conditions and groupings by processing all
  // segments and glyphs. ###
  VLOG(0) << "Forming initial segmentation plan.";
  for (segment_index_t segment_index = 0;
       segment_index < context.SegmentationInfo().Segments().size();
       segment_index++) {
    TRY(context.ReprocessSegment(segment_index));
  }
  context.glyph_closure_cache.LogClosureCount("Inital segment analysis");

  GlyphSet all_glyphs;
  all_glyphs.insert_range(0, glyph_count - 1);
  TRYV(context.GroupGlyphs(all_glyphs));
  context.glyph_closure_cache.LogClosureCount("Condition grouping");

  TRYV(context.InitOptimizationCutoff());

  return context;
}

StatusOr<SegmentationCost> ClosureGlyphSegmenter::TotalCost(
    hb_face_t* original_face, const GlyphSegmentation& segmentation,
    const ProbabilityCalculator& probability_calculator) const {
  SubsetDefinition non_ift;
  non_ift.Union(segmentation.InitialFontSegment());

  std::vector<Segment> segments;
  for (const auto& def : segmentation.Segments()) {
    non_ift.Union(def);

    auto P = probability_calculator.ComputeProbability(def);
    Segment s(def, P);
    segments.push_back(std::move(s));
  }

  double init_font_size = TRY(CandidateMerge::Woff2SizeOf(
      original_face, segmentation.InitialFontSegment(), 11));
  double non_ift_font_size =
      TRY(CandidateMerge::Woff2SizeOf(original_face, non_ift, 11));

  // TODO(garretrieger): for the total cost we need to also add in the table
  // keyed patch costs
  //                     may want to use the IFT compiler to produce the
  //                     complete encoding then compute table keyed costs from
  //                     that (in conjunction) with probability calculations.
  double total_cost = init_font_size;

  // Use highest quality so we get the true cost.
  PatchSizeCacheImpl patch_sizer(original_face, 11);
  for (const auto& c : segmentation.Conditions()) {
    double Pc = TRY(c.Probability(segments, probability_calculator));
    const GlyphSet& gids = segmentation.GidSegments().at(c.activated());
    double patch_size = (double)TRY(patch_sizer.GetPatchSize(gids));
    total_cost += Pc * (patch_size + 75);
  }

  double ideal_cost = 0.0;
  double incremental_size =
      non_ift_font_size / (double)non_ift.codepoints.size();
  for (unsigned cp : non_ift.codepoints) {
    double Pcp = probability_calculator.ComputeProbability({cp}).Min();
    ideal_cost += Pcp * incremental_size;
  }

  return SegmentationCost{
      .total_cost = total_cost,
      .cost_for_non_segmented = non_ift_font_size,
      .ideal_cost = ideal_cost,
  };
}

}  // namespace ift::encoder
