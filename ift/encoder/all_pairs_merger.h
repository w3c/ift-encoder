#ifndef IFT_ENCODER_ALL_PAIRS_MERGER_H_
#define IFT_ENCODER_ALL_PAIRS_MERGER_H_

#include <cstdint>
#include <optional>
#include <queue>
#include <string>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/flags/declare.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "ift/common/int_set.h"
#include "ift/encoder/candidate_merge.h"
#include "ift/encoder/invalidation_set.h"
#include "ift/encoder/types.h"

namespace ift::encoder {

class Merger;

enum class AllPairsNormalizationMode {
  kNone,
  kSegment,
  kByte,
};

bool AbslParseFlag(absl::string_view text, AllPairsNormalizationMode* mode,
                   std::string* error);
std::string AbslUnparseFlag(AllPairsNormalizationMode mode);

// Implements Phase 1 all-pairs cost-reducing segment merging for Merger using a
// lazy two-stage (best-case lower bound -> exact) priority queue.
class AllPairsMerger {
 public:
  struct ProbOrderedSegment {
    double probability;
    segment_index_t segment_index;

    bool operator<(const ProbOrderedSegment& other) const {
      if (probability != other.probability) {
        return probability > other.probability;
      }
      return segment_index < other.segment_index;
    }

    bool operator==(const ProbOrderedSegment& other) const {
      return probability == other.probability &&
             segment_index == other.segment_index;
    }
  };

  absl::StatusOr<std::optional<InvalidationSet>> TryNextAllPairsMerge(
      Merger& merger);

  void AssignMergedSegment(const Merger& merger, segment_index_t base,
                           const ift::common::SegmentSet& to_merge);

  void MarkFinished(segment_index_t s) {
    if (!initialized_) {
      return;
    }
    RemoveActiveSegment(s);
    segment_epoch_.at(s) = UINT32_MAX;
    dirty_segments_.erase(s);
    dirty_bases_.erase(s);
  }

  const absl::btree_set<ProbOrderedSegment>& ActiveSegmentsByProb() const {
    return active_segments_by_prob_;
  }

 private:
  struct QueuedCandidate {
    double cost_delta;
    double norm_divisor = 1.0;
    bool is_exact;
    // Canonical set of all segments participating in the merge (size >= 2).
    // The surviving base segment is always *segments.min().
    ift::common::SegmentSet segments;
    uint32_t created_epoch;
    std::optional<CandidateMerge> evaluated_merge;

    bool HigherPriorityThan(const QueuedCandidate& other) const {
      if (cost_delta != other.cost_delta) {
        return cost_delta < other.cost_delta;
      }
      if (is_exact != other.is_exact) {
        return is_exact && !other.is_exact;
      }
      return segments < other.segments;
    }

    struct LowerPriority {
      bool operator()(const QueuedCandidate& a,
                      const QueuedCandidate& b) const {
        return b.HigherPriorityThan(a);
      }
    };
  };

  absl::Status InitPhase1(Merger& merger);
  absl::Status RefreshDirtySegments(Merger& merger);
  bool IsCandidateStale(const QueuedCandidate& candidate) const;
  bool IsActiveProbSegment(segment_index_t s) const {
    return active_segment_prob_.at(s).has_value();
  }
  absl::Status UpdateActiveSegment(Merger& merger, segment_index_t s);
  void RemoveActiveSegment(segment_index_t s);
  absl::StatusOr<double> NormalizationDivisor(
      Merger& merger, const ift::common::SegmentSet& merged_segments) const;
  absl::Status QueueBestCaseCandidate(
      Merger& merger, const ift::common::SegmentSet& merged_segments);
  absl::Status QueueLowerRankedExclusivePairs(
      Merger& merger,
      absl::btree_set<ProbOrderedSegment>::const_iterator u_it);
  absl::Status QueueCompositeCandidates(
      Merger& merger,
      const std::optional<ift::common::SegmentSet>& filter_segments =
          std::nullopt);
  absl::Status QueueAllCandidates(
      Merger& merger,
      const std::optional<ift::common::SegmentSet>& dirty_segments =
          std::nullopt);

  bool initialized_ = false;
  uint32_t current_epoch_ = 0;
  std::vector<uint32_t> segment_epoch_;
  ift::common::SegmentSet dirty_segments_;
  ift::common::SegmentSet dirty_bases_;
  ift::common::GlyphSet dirty_glyphs_;
  absl::btree_set<ProbOrderedSegment> active_segments_by_prob_;
  std::vector<std::optional<double>> active_segment_prob_;
  std::priority_queue<QueuedCandidate, std::vector<QueuedCandidate>,
                      QueuedCandidate::LowerPriority>
      queue_;
};

}  // namespace ift::encoder

ABSL_DECLARE_FLAG(ift::encoder::AllPairsNormalizationMode,
                  experimental_all_pairs_normalization);

#endif  // IFT_ENCODER_ALL_PAIRS_MERGER_H_
