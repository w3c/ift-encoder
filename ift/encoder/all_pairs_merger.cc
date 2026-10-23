#include "ift/encoder/all_pairs_merger.h"

#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/candidate_merge.h"
#include "ift/encoder/invalidation_set.h"
#include "ift/encoder/merger.h"
#include "ift/encoder/types.h"

ABSL_FLAG(ift::encoder::AllPairsNormalizationMode,
          experimental_all_pairs_normalization,
          ift::encoder::AllPairsNormalizationMode::kNone,
          "Controls how cost deltas are normalized when ranking candidate "
          "merges in all-pairs mode: 'none' (no normalization), 'segment' "
          "(divide by merged_segments.size() - 1), or 'byte' (divide by "
          "pre-merge patch bytes including network overhead).");

using absl::btree_set;
using absl::Status;
using absl::StatusOr;
using ift::common::GlyphSet;
using ift::common::SegmentSet;

namespace ift::encoder {

bool AbslParseFlag(absl::string_view text, AllPairsNormalizationMode* mode,
                   std::string* error) {
  if (text == "none") {
    *mode = AllPairsNormalizationMode::kNone;
    return true;
  }
  if (text == "segment" || text == "segments") {
    *mode = AllPairsNormalizationMode::kSegment;
    return true;
  }
  if (text == "byte" || text == "bytes") {
    *mode = AllPairsNormalizationMode::kByte;
    return true;
  }
  *error = "unknown normalization mode; expected 'none', 'segment', or 'byte'";
  return false;
}

std::string AbslUnparseFlag(AllPairsNormalizationMode mode) {
  switch (mode) {
    case AllPairsNormalizationMode::kNone:
      return "none";
    case AllPairsNormalizationMode::kSegment:
      return "segment";
    case AllPairsNormalizationMode::kByte:
      return "byte";
  }
  return "none";
}

void AllPairsMerger::AssignMergedSegment(const Merger& merger,
                                         segment_index_t base,
                                         const SegmentSet& to_merge) {
  if (!initialized_) {
    return;
  }

  SegmentSet merged_segments = to_merge;
  merged_segments.insert(base);
  const auto& groupings = merger.Context().glyph_groupings;
  const auto& pre_comb_map = groupings.ConditionsAndGlyphsPreCombination();

  for (segment_index_t s : merged_segments) {
    // precombination representative affected glyphs are recorded this will
    // let us locate any conditions involved with those glyphs post-merge.
    for (const auto& c :
         groupings.TriggeringSegmentToConditionsPreCombination(s)) {
      if (c.IsFallback()) {
        continue;
      }
      if (auto rep = pre_comb_map.at(c).min(); rep.has_value()) {
        dirty_glyphs_.insert(*rep);
      }
    }

    // Also record any segments involved in conditions directly impacted by the merge.
    for (const auto& c :
         groupings.TriggeringSegmentToConditions(s)) {
      if (c.IsFallback()) {
        continue;
      }
      if (!c.IsExclusive()) {
        dirty_segments_.union_set(c.TriggeringSegments());
      }
    }
  }

  for (segment_index_t s : to_merge) {
    RemoveActiveSegment(s);
    segment_epoch_.at(s) = UINT32_MAX;
    dirty_segments_.erase(s);
    dirty_bases_.erase(s);
  }
  // base's glyph groupings are not finalized until after Apply() (and
  // ReprocessChanged() for non-inert merges) completes, so remove it for now;
  // the next TryNextAllPairsMerge() call will re-insert it via
  // RefreshDirtySegments().
  RemoveActiveSegment(base);
  segment_epoch_.at(base) = UINT32_MAX;

  dirty_segments_.insert(base);
  dirty_bases_.insert(base);
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
    // TODO(garretrieger): active_segments_by_prob is currently restricted to
    // just segments with exclusive patches which in turn means we
    // only consider exclusive segment pairs for merging, should we
    // consider all segment pairs? or keep this as is relying on composite
    // condition candidates.
    return absl::OkStatus();
  }

  double prob = TRY(merger.AggregateProbability(s));
  active_segments_by_prob_.insert(ProbOrderedSegment{prob, s});
  active_segment_prob_.at(s) = prob;
  return absl::OkStatus();
}

StatusOr<double> AllPairsMerger::NormalizationDivisor(
    Merger& merger, const SegmentSet& merged_segments) const {
  switch (absl::GetFlag(FLAGS_experimental_all_pairs_normalization)) {
    case AllPairsNormalizationMode::kNone:
      return 1.0;
    case AllPairsNormalizationMode::kSegment:
      return static_cast<double>(merged_segments.size() - 1);
    case AllPairsNormalizationMode::kByte: {
      const uint32_t overhead = merger.Strategy().NetworkOverheadCost();
      const auto& context = merger.Context();
      if (merged_segments.size() == 2) {
        segment_index_t u = *merged_segments.min();
        segment_index_t v = *merged_segments.max();
        if (context.InertSegments().contains(u) &&
            context.InertSegments().contains(v)) {
          uint32_t size_u = TRY(merger.ExclusivePatchSize(u));
          uint32_t size_v = TRY(merger.ExclusivePatchSize(v));
          if (size_u > 0 && size_v > 0) {
            return static_cast<double>(size_u + overhead) +
                   static_cast<double>(size_v + overhead);
          }
        }
      }

      const bool unicode_range_mode =
          (context.GetTargetMode() == config::UNICODE_RANGE);
      const auto& groupings = context.glyph_groupings;
      const auto& conditions_and_glyphs = groupings.ConditionsAndGlyphs();
      absl::flat_hash_set<const ActivationCondition*> seen_conditions;
      uint64_t total_bytes = 0;
      for (segment_index_t s : merged_segments) {
        for (const auto& c : groupings.TriggeringSegmentToConditions(s)) {
          if (c.IsFallback() || (unicode_range_mode && !c.IsExclusive())) {
            continue;
          }
          auto it = conditions_and_glyphs.find(c);
          if (it == conditions_and_glyphs.end()) {
            return absl::InternalError(
                "Condition which should be present wasn't found.");
          }
          if (!seen_conditions.insert(&it->first).second) {
            continue;
          }
          uint32_t patch_size =
              TRY(context.patch_size_cache->GetPatchSize(it->second));
          total_bytes += static_cast<uint64_t>(patch_size + overhead);
        }
      }
      return total_bytes > 0 ? static_cast<double>(total_bytes) : 1.0;
    }
  }
  return 1.0;
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
    double norm_divisor = TRY(NormalizationDivisor(merger, merged_segments));
    queue_.push(QueuedCandidate{
        .cost_delta = delta / norm_divisor,
        .norm_divisor = norm_divisor,
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

    if (!triggering_segments.is_subset_of(merger.CandidateSegments()) ||
        !triggering_segments.is_subset_of(merger.InscopeSegments())) {
      continue;
    }

    if (triggering_segments.size() == 2) {
      segment_index_t u = *triggering_segments.min();
      segment_index_t v = *triggering_segments.max();
      if (IsActiveProbSegment(u) && IsActiveProbSegment(v)) {
        // Pairs of active segments are already considered by QueueLowerRankedExclusivePairs()
        // so don't need to check them again here.
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

Status AllPairsMerger::QueueAllCandidates(
    Merger& merger, const std::optional<SegmentSet>& dirty_segments) {
  if (!dirty_segments.has_value()) {
    for (auto u_it = active_segments_by_prob_.begin();
         u_it != active_segments_by_prob_.end(); ++u_it) {
      TRYV(QueueLowerRankedExclusivePairs(merger, u_it));
    }
  } else {
    for (segment_index_t d : *dirty_segments) {
      if (!IsActiveProbSegment(d)) {
        continue;
      }
      double d_prob = *active_segment_prob_.at(d);
      auto d_it = active_segments_by_prob_.find(ProbOrderedSegment{d_prob, d});
      if (d_it == active_segments_by_prob_.end()) {
        return absl::InternalError(
            "Active probability segment missing from ordered set.");
      }

      bool d_is_inert = merger.Context().InertSegments().contains(d);
      for (auto u_it = active_segments_by_prob_.begin(); u_it != d_it; ++u_it) {
        segment_index_t u = u_it->segment_index;
        if (dirty_segments->contains(u)) {
          // Pair {u, d} is already queued when processing u's lower-ranked
          // partners.
          continue;
        }
        if (d_is_inert) {
          uint32_t u_size = TRY(merger.ExclusivePatchSize(u));
          double inert_threshold = merger.BestCaseInertProbabilityThreshold(
              u_size, u_it->probability, 0.0);
          if (d_prob <= inert_threshold) {
            continue;
          }
        }
        TRYV(QueueBestCaseCandidate(merger, SegmentSet{u, d}));
      }

      TRYV(QueueLowerRankedExclusivePairs(merger, d_it));
    }
  }

  return QueueCompositeCandidates(merger, dirty_segments);
}

Status AllPairsMerger::InitPhase1(Merger& merger) {
  size_t num_segments = merger.Context().SegmentationInfo().Segments().size();
  current_epoch_ = 0;
  segment_epoch_.assign(num_segments, UINT32_MAX);
  for (segment_index_t s : merger.CandidateSegments()) {
    segment_epoch_.at(s) = 0;
  }
  dirty_segments_.clear();
  dirty_bases_.clear();
  dirty_glyphs_.clear();
  active_segments_by_prob_.clear();
  active_segment_prob_.assign(num_segments, std::nullopt);
  queue_ = {};

  for (segment_index_t s : merger.CandidateSegments()) {
    if (s >= merger.OptimizationCutoffSegment()) {
      break;
    }
    TRYV(UpdateActiveSegment(merger, s));
  }

  TRYV(QueueAllCandidates(merger));
  initialized_ = true;
  return absl::OkStatus();
}

Status AllPairsMerger::RefreshDirtySegments(Merger& merger) {
  const auto& groupings = merger.Context().glyph_groupings;
  for (glyph_id_t gid : dirty_glyphs_) {
    if (auto cond = groupings.GlyphToCondition(gid);
        cond.has_value() && !cond->IsFallback()) {
      dirty_segments_.union_set(cond->TriggeringSegments());
    }
  }
  dirty_glyphs_.clear();

  dirty_bases_.intersect(merger.CandidateSegments());
  for (segment_index_t base : dirty_bases_) {
    dirty_segments_.insert(base);
    for (const auto& c : groupings.TriggeringSegmentToConditions(base)) {
      if (!c.IsFallback() && !c.IsExclusive()) {
        dirty_segments_.union_set(c.TriggeringSegments());
      }
    }
    TRYV(UpdateActiveSegment(merger, base));
  }
  dirty_bases_.clear();

  dirty_segments_.intersect(merger.CandidateSegments());
  if (dirty_segments_.empty()) {
    return absl::OkStatus();
  }

  ++current_epoch_;
  for (segment_index_t s : dirty_segments_) {
    segment_epoch_.at(s) = current_epoch_;
    if (IsActiveProbSegment(s) && groupings.ExclusiveGlyphs(s).empty()) {
      RemoveActiveSegment(s);
    }
  }

  TRYV(QueueAllCandidates(merger, dirty_segments_));
  dirty_segments_.clear();
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
  if (!initialized_) {
    TRYV(InitPhase1(merger));
  } else {
    TRYV(RefreshDirtySegments(merger));
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
            .cost_delta = candidate_merge->CostDelta() / top.norm_divisor,
            .norm_divisor = top.norm_divisor,
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

  return std::nullopt;
}

}  // namespace ift::encoder
