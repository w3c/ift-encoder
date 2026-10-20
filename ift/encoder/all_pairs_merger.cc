#include "ift/encoder/all_pairs_merger.h"

#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>

#include "absl/container/btree_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/candidate_merge.h"
#include "ift/encoder/invalidation_set.h"
#include "ift/encoder/merger.h"
#include "ift/encoder/types.h"

using absl::btree_set;
using absl::Status;
using absl::StatusOr;
using ift::common::SegmentSet;

namespace ift::encoder {

void AllPairsMerger::AssignMergedSegment(segment_index_t base,
                                         const SegmentSet& to_merge) {
  if (!initialized_) {
    return;
  }
  for (segment_index_t s : to_merge) {
    RemoveActiveSegment(s);
    segment_epoch_.at(s) = UINT32_MAX;
  }
  // base's glyph groupings are not finalized until after Apply() (and
  // ReprocessChanged() for non-inert merges) completes, so remove it for now;
  // the next TryNextAllPairsMerge() call will re-insert it via
  // UpdateActiveSegment().
  RemoveActiveSegment(base);
  segment_epoch_.at(base) = ++current_epoch_;
}

void AllPairsMerger::RemoveActiveSegment(segment_index_t s) {
  auto& prob = active_segment_prob_.at(s);
  if (prob.has_value()) {
    active_segments_by_prob_.erase(ProbOrderedSegment{*prob, s});
    prob = std::nullopt;
  }
}

Status AllPairsMerger::UpdateActiveSegment(Merger& merger, segment_index_t s) {
  RemoveActiveSegment(s);
  if (!merger.CandidateSegments().contains(s) ||
      s >= merger.OptimizationCutoffSegment()) {
    return absl::OkStatus();
  }
  uint32_t size = TRY(merger.ExclusivePatchSize(s));
  if (size == 0) {
    return absl::OkStatus();
  }
  double prob = TRY(merger.AggregateProbability(s));
  active_segments_by_prob_.insert(ProbOrderedSegment{prob, s});
  active_segment_prob_.at(s) = prob;
  return absl::OkStatus();
}

Status AllPairsMerger::QueueBestCaseCandidate(
    Merger& merger, const SegmentSet& merged_segments) {
  if (merged_segments.size() <= 1) {
    return absl::OkStatus();
  }
  segment_index_t base = *merged_segments.min();
  if (CandidateMerge::WouldMixFeaturesAndCodepoints(
          merger.Context().SegmentationInfo(), base, merged_segments)) {
    return absl::OkStatus();
  }

  auto best_case_delta = CandidateMerge::ComputeCostDelta<true>(
      merger, merged_segments, std::nullopt, 0.0);
  if (absl::IsCancelled(best_case_delta.status())) {
    return absl::OkStatus();
  }
  double delta = TRY(best_case_delta);
  if (delta < 0.0) {
    queue_.push(QueuedCandidate{
        .cost_delta = delta,
        .is_exact = false,
        .segments = merged_segments,
        .created_epoch = current_epoch_,
        .evaluated_merge = std::nullopt,
    });
  }
  return absl::OkStatus();
}

Status AllPairsMerger::QueueLowerRankedExclusivePairs(
    Merger& merger, btree_set<ProbOrderedSegment>::const_iterator u_it) {
  segment_index_t u = u_it->segment_index;
  double u_prob = u_it->probability;
  uint32_t u_size = TRY(merger.ExclusivePatchSize(u));
  double inert_threshold =
      merger.BestCaseInertProbabilityThreshold(u_size, u_prob, 0.0);

  for (auto v_it = std::next(u_it); v_it != active_segments_by_prob_.end();
       ++v_it) {
    segment_index_t v = v_it->segment_index;
    if (merger.Context().InertSegments().contains(v) &&
        v_it->probability <= inert_threshold) {
      // Iteration is in descending probability order, so all subsequent inert
      // segments also fail the threshold.
      // TODO(garretrieger): should we keep looking at non inert options beyond this
      // point? Once all pairs is done, test the impact of changing this.
      break;
    }
    TRYV(QueueBestCaseCandidate(merger, SegmentSet{u, v}));
  }

  return absl::OkStatus();
}

Status AllPairsMerger::QueueCompositeCandidates(
    Merger& merger, const std::optional<SegmentSet>& filter_segments) {
  if (merger.Context().GetTargetMode() == config::UNICODE_RANGE) {
    return absl::OkStatus();
  }

  ActivationCondition last_exclusive =
      ActivationCondition::exclusive_segment(UINT32_MAX, 0);

  btree_set<SegmentSet> unique_merge_sets;
  for (auto it = merger.Context().glyph_groupings.OrderedConditions().lower_bound(
           last_exclusive);
       it != merger.Context().glyph_groupings.OrderedConditions().end(); ++it) {
    const auto& next_condition = *it;
    if (next_condition.IsFallback() || next_condition.IsExclusive()) {
      continue;
    }

    SegmentSet triggering_segments = next_condition.TriggeringSegments();
    if (triggering_segments.size() <= 1) {
      continue;
    }

    if (filter_segments.has_value() &&
        !triggering_segments.intersects(*filter_segments)) {
      continue;
    }

    std::optional<unsigned> min = triggering_segments.min();
    if (min.has_value() && *min >= merger.OptimizationCutoffSegment()) {
      continue;
    }

    if (!triggering_segments.intersects(merger.CandidateSegments()) ||
        !triggering_segments.is_subset_of(merger.InscopeSegments())) {
      continue;
    }

    if (triggering_segments.size() == 2) {
      segment_index_t u = *triggering_segments.min();
      segment_index_t v = *triggering_segments.max();
      if (IsActiveProbSegment(u) && IsActiveProbSegment(v)) {
        // Pair of active exclusive segments is already handled by exclusive
        // pair queuing.
        continue;
      }
    }

    unique_merge_sets.insert(std::move(triggering_segments));
  }

  for (const auto& segments : unique_merge_sets) {
    TRYV(QueueBestCaseCandidate(merger, segments));
  }

  return absl::OkStatus();
}

Status AllPairsMerger::InitPhase1(Merger& merger) {
  size_t num_segments = merger.Context().SegmentationInfo().Segments().size();
  current_epoch_ = 0;
  segment_epoch_.assign(num_segments, UINT32_MAX);
  for (segment_index_t s : merger.CandidateSegments()) {
    segment_epoch_.at(s) = 0;
  }
  active_segments_by_prob_.clear();
  active_segment_prob_.assign(num_segments, std::nullopt);
  queue_ = {};

  for (segment_index_t s : merger.CandidateSegments()) {
    if (s >= merger.OptimizationCutoffSegment()) {
      break;
    }
    TRYV(UpdateActiveSegment(merger, s));
  }

  for (auto u_it = active_segments_by_prob_.begin();
       u_it != active_segments_by_prob_.end(); ++u_it) {
    TRYV(QueueLowerRankedExclusivePairs(merger, u_it));
  }

  TRYV(QueueCompositeCandidates(merger));
  initialized_ = true;
  return absl::OkStatus();
}

bool AllPairsMerger::IsCandidateStale(const QueuedCandidate& candidate) const {
  for (segment_index_t s : candidate.segments) {
    if (segment_epoch_.at(s) > candidate.created_epoch) {
      return true;
    }
  }
  return false;
}

StatusOr<std::optional<InvalidationSet>> AllPairsMerger::TryNextAllPairsMerge(
    Merger& merger) {
  if (phase_1_complete_) {
    return std::nullopt;
  }

  if (!initialized_) {
    TRYV(InitPhase1(merger));
  }

  while (!queue_.empty()) {
    QueuedCandidate top =
        std::move(const_cast<QueuedCandidate&>(queue_.top()));
    queue_.pop();

    if (IsCandidateStale(top)) {
      continue;
    }

    if (!top.is_exact) {
      segment_index_t base = *top.segments.min();
      auto baseline = CandidateMerge::BaselineCandidate(base, 0.0);
      auto candidate_merge = TRY(CandidateMerge::AssessSegmentMerge(
          merger, base, top.segments, baseline));
      if (candidate_merge.has_value() && candidate_merge->CostDelta() < 0.0) {
        queue_.push(QueuedCandidate{
            .cost_delta = candidate_merge->CostDelta(),
            .is_exact = true,
            .segments = std::move(top.segments),
            .created_epoch = current_epoch_,
            .evaluated_merge = std::move(*candidate_merge),
        });
      }
      continue;
    }

    return TRY(top.evaluated_merge->Apply(merger));
  }

  phase_1_complete_ = true;
  return std::nullopt;
}

}  // namespace ift::encoder
