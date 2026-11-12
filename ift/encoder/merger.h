#ifndef IFT_ENCODER_MERGER_H_
#define IFT_ENCODER_MERGER_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "ift/common/int_set.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/all_pairs_merger.h"
#include "ift/encoder/candidate_merge.h"
#include "ift/encoder/invalidation_set.h"
#include "ift/encoder/merge_strategy.h"
#include "ift/encoder/segmentation_context.h"
#include "ift/encoder/types.h"

namespace ift::encoder {

// Finds and merge segments/patches from a in progress segmentation.
//
// A merger instance is scoped to operate on a subset of the segments
// in a complete segmentation. The in progress segmentation is supplied
// via a provided segmentation context.
class Merger {
 public:
  static absl::StatusOr<Merger> New(
      SegmentationContext& context, MergeStrategy strategy,
      ift::common::SegmentSet inscope_segments,
      ift::common::SegmentSet inscope_segments_for_init_move) {
    Merger merger(context, strategy, inscope_segments,
                  inscope_segments_for_init_move, UINT32_MAX);
    TRYV(merger.ResetSegmentProbabilities());
    TRYV(merger.InitOptimizationCutoff());
    return merger;
  }

  // This is the estimated smallest possible increase in a patch size as a
  // result of a merge (ie. assuming the added glyph(s) are redundant with the
  // base and cost 0 to encode). This is roughly the number of bytes that would
  // be added by including a single extra gid into the patch header.
  static constexpr unsigned BEST_CASE_MERGE_SIZE_DELTA = 6;

  const MergeStrategy& Strategy() const { return strategy_; }

  const SegmentationContext& Context() const { return *context_; }

  SegmentationContext& Context() { return *context_; }

  /*
   * Searches for a merge to perform and executes it if found. Does not trigger
   * closure re-analysis of the merged segments.
   *
   * If a merge was performed returns the segment and glyphs which were modified
   * to allow groupings to be updated.
   *
   * If nullopt is returned then there are no more available merges to perform.
   */
  absl::StatusOr<std::optional<InvalidationSet>> TryNextMerge();

  /*
   * This method analyzes the segments and checks to see if any should be
   * moved into the initial font.
   *
   * The common example where this is useful is for segments that have 100%
   * probability. Since these are always needed, the most efficient thing to
   * do is to move them into the initial font so they are already loaded
   * without needing to be part of a patch.
   *
   * The approach is fairly straightforward: iterate through all of the
   * conditions/patches and compute a cost delta for moving that patch
   * into the init font. Move only those cases whose delta is below a
   * configurable threshold.
   */
  absl::Status MoveSegmentsToInitFont(size_t profile_index = 0);

  /*
   * Recompute the state of this merger to respect changes made to the
   * segmentation context to reconfigure the init subset.
   */
  absl::Status ReassignInitSubset();

  /*
   * Merges to_merge segments with base. base is set to merged_segment.
   */
  uint32_t AssignMergedSegment(segment_index_t base,
                               const ift::common::SegmentSet& to_merge,
                               const Segment& merged_segment, bool is_inert);

  uint32_t NumCutoffSegments() const { return CutoffSegments().size(); }

  uint32_t NumInscopeSegments() const { return inscope_segments_.size(); }

  const ift::common::SegmentSet& InscopeSegments() const {
    return inscope_segments_;
  }

  const ift::common::SegmentSet& CandidateSegments() const {
    return candidate_segments_;
  }

  segment_index_t OptimizationCutoffSegment() const {
    return optimization_cutoff_segment_;
  }

  // For a merge of an inert base patch with any other possible inert segment,
  // this computes the minimum probability the other segment must have for it to
  // be possible to produce a delta lower than lowest_cost_delta (regardless of
  // it's probability or size).
  //
  // This assumes that P(base) >= P(other)
  double BestCaseInertProbabilityThreshold(uint32_t base_patch_size,
                                           double base_probability,
                                           double lowest_cost_delta) const;

  void RecordMergedSizeReduction(double size_reduction) {
    int32_t reduction_percent = 100.0 * size_reduction;
    merged_size_reduction_histogram_[reduction_percent]++;
  }

  bool ShouldRecordMergedSizeReductions() const;

  void LogMergedSizeHistogram() const;

  // The total cost of a segmentation is defined as the sum of the costs
  // evaluated independently against each of the strategies probability
  // calculators (see docs/multi_script_merging.md):
  //
  //   cost(S) = sum_m sum_i P_m(c_i) * (size(p_i) + k)
  //
  // Patch sizes and glyph sets are identical for all calculators, only the
  // probabilities differ. As a result the summed cost of a patch can be
  // computed using the aggregate probability, sum_m P_m(c_i), of the
  // associated activation condition:
  //
  //   sum_m P_m(c_i) * (size(p_i) + k) = (sum_m P_m(c_i)) * (size(p_i) + k)
  //
  // The same applies to cost deltas since the cost function is linear over the
  // set of calculators.
  //
  // Note: unlike a regular probability an aggregate probability is in the
  // range [0, number of probability profiles].

  // Computes the aggregate probability for the segment at segment_index, using
  // cached segment probabilities when available.
  absl::StatusOr<double> AggregateProbability(
      segment_index_t segment_index) const {
    if (cached_agg_prob_.at(segment_index) >= 0.0) {
      return cached_agg_prob_.at(segment_index);
    }
    return ComputeAndCacheAggregateProbability(segment_index);
  }

  // Computes the lower-bound aggregate probability (sum of ProbabilityBound::Min
  // across profiles) for the segment at segment_index, using cached segment
  // probabilities when available.
  absl::StatusOr<double> MinAggregateProbability(
      segment_index_t segment_index) const {
    if (cached_min_agg_prob_.at(segment_index) >= 0.0) {
      return cached_min_agg_prob_.at(segment_index);
    }
    TRYV(ComputeAndCacheAggregateProbability(segment_index));
    return cached_min_agg_prob_.at(segment_index);
  }

  // Returns the cached patch size in bytes of the exclusive glyph set for
  // segment_index (or 0 if segment_index has no exclusive glyphs).
  absl::StatusOr<uint32_t> ExclusivePatchSize(
      segment_index_t segment_index) const {
    if (!exclusive_sizes_dirty_ &&
        cached_patch_size_cache_ == context_->patch_size_cache.get() &&
        cached_exclusive_size_.at(segment_index) != UINT32_MAX) {
      return cached_exclusive_size_.at(segment_index);
    }
    return ComputeAndCacheExclusivePatchSize(segment_index);
  }

  // Returns the base patch overhead in bytes (0 in IFT mode; the cached empty
  // subset WOFF2 size in UNICODE_RANGE mode).
  absl::StatusOr<uint32_t> BasePatchOverhead() const {
    if (context_->GetTargetMode() != config::UNICODE_RANGE) {
      return 0;
    }
    if (!exclusive_sizes_dirty_ &&
        cached_patch_size_cache_ == context_->patch_size_cache.get() &&
        cached_base_overhead_.has_value()) {
      return *cached_base_overhead_;
    }
    return ComputeAndCacheBasePatchOverhead();
  }

  void InvalidateExclusivePatchSizes() { exclusive_sizes_dirty_ = true; }

  // Computes the aggregate probability of condition being activated.
  absl::StatusOr<double> AggregateProbability(
      const ActivationCondition& condition) const;

  // Computes the aggregate probability of the union of merged_segments.
  absl::StatusOr<double> AggregateMergedProbability(
      const ift::common::SegmentSet& merged_segments) const;

  // Same as AggregateProbability(condition), but computes the probability of
  // condition as it would be if merged_segment_index has been replaced by
  // the union of merged_segments.
  absl::StatusOr<double> AggregateMergedProbability(
      const ActivationCondition& condition,
      segment_index_t merged_segment_index,
      const ift::common::SegmentSet& merged_segments) const;

  // The largest value an aggregate probability can take, that is the number of
  // probability profiles in the strategy.
  double MaxAggregateProbability() const {
    return (double)strategy_.ProbabilityProfiles().size();
  }

 private:
  Merger(SegmentationContext& context, MergeStrategy strategy,
         ift::common::SegmentSet inscope_segments,
         ift::common::SegmentSet inscope_segments_for_init_move,
         segment_index_t optimization_cutoff_segment)
      : context_(&context),
        strategy_(strategy),
        inscope_segments_(inscope_segments),
        candidate_segments_(
            ComputeCandidateSegments(*context_, strategy_, inscope_segments_)),
        inscope_segments_for_init_move_(inscope_segments_for_init_move),
        optimization_cutoff_segment_(optimization_cutoff_segment) {}

  static ift::common::SegmentSet ComputeCandidateSegments(
      SegmentationContext& context, const MergeStrategy& strategy,
      const ift::common::SegmentSet& inscope_segments);

  static ift::common::GlyphSet ComputeCandidatePatchMergeGlyphs(
      SegmentationContext& context, const MergeStrategy& strategy,
      const ift::common::SegmentSet& inscope_segments);

  absl::StatusOr<std::optional<InvalidationSet>> TryNextPatchMerge();

  absl::Status ResetSegmentProbabilities() const;
  absl::StatusOr<double> ComputeAndCacheAggregateProbability(
      segment_index_t segment_index) const;
  absl::StatusOr<uint32_t> ComputeAndCacheExclusivePatchSize(
      segment_index_t segment_index) const;
  absl::StatusOr<uint32_t> ComputeAndCacheBasePatchOverhead() const;
  void EnsurePatchSizeCacheValid() const;
  absl::Status InitOptimizationCutoff();
  absl::StatusOr<segment_index_t> ComputeSegmentCutoff() const;

  absl::StatusOr<std::optional<InvalidationSet>> MergeSegmentWithCosts(
      uint32_t base_segment_index);

  absl::StatusOr<std::optional<InvalidationSet>> PatchMergeWithCosts(
      const ActivationCondition& base,
      const std::vector<std::pair<ActivationCondition, double>>&
          ordered_candidates);

  absl::Status CollectExclusiveCandidateMerges(
      uint32_t base_segment_index,
      std::optional<CandidateMerge>& smallest_candidate_merge);

  absl::Status CollectCompositeCandidateMerges(
      uint32_t base_segment_index,
      std::optional<CandidateMerge>& smallest_candidate_merge);

  absl::StatusOr<std::optional<InvalidationSet>> MergeSegmentWithHeuristic(
      uint32_t base_segment_index);

  ift::common::SegmentSet CutoffSegments() const;

  /*
   * Search for a base segment after base_segment_index which can be merged into
   * base_segment_index without exceeding the maximum patch size.
   *
   * Returns the set of glyphs invalidated by the merge if found and the merge
   * suceeded.
   */
  absl::StatusOr<std::optional<InvalidationSet>> TryMergingABaseSegment(
      segment_index_t base_segment_index);

  struct InertProbe {
    ift::common::SegmentSet batch;
    ift::common::GlyphSet glyphs;
    ift::common::SegmentSet::const_iterator next_it;
    size_t added_count = 0;
    bool hit_non_inert = false;
  };

  bool CanMergeAsBaseSegment(segment_index_t base_segment_index,
                             segment_index_t candidate) const;

  InertProbe CollectInertProbe(
      segment_index_t base_segment_index,
      const ift::common::SegmentSet& accepted_batch,
      const ift::common::GlyphSet& accepted_glyphs,
      ift::common::SegmentSet::const_iterator start_it,
      size_t max_to_add) const;

  absl::StatusOr<ift::common::SegmentSet> FindInertBatchToMerge(
      segment_index_t base_segment_index,
      const ift::common::GlyphSet& base_glyphs, uint32_t base_patch_size,
      ift::common::SegmentSet::const_iterator& next_segment_it);

  absl::StatusOr<std::optional<InvalidationSet>> TryMergingACompositeCondition(
      segment_index_t base_segment_index);

  absl::StatusOr<std::optional<InvalidationSet>> TryMerge(
      segment_index_t base_segment_index,
      const ift::common::SegmentSet& to_merge_segments_);

  void MarkFinished(segment_index_t s) {
    candidate_segments_.erase(s);
    all_pairs_merger_.MarkFinished(s);
  }

  absl::Status ApplyInitFontMove(const ift::common::GlyphSet& glyphs_to_move,
                                 double delta);

  absl::StatusOr<ift::common::SegmentSet> InitFontApplyProbabilityThreshold(
      size_t profile_index = 0) const;
  ift::common::SegmentSet InitFontSegmentsToCheck(
      const ift::common::SegmentSet& inscope) const;
  absl::btree_map<ActivationCondition, ift::common::GlyphSet>
  InitFontConditionsToCheck(const ift::common::SegmentSet& to_check,
                            bool batch_mode) const;

  // Stores the broadeder complete segmentation.
  SegmentationContext* context_;

  // Stores the settings that configure how merging operations are
  // selected and performed.
  const MergeStrategy strategy_;

  // The current set of segments under consideration for being merged.
  const ift::common::SegmentSet inscope_segments_;
  ift::common::SegmentSet candidate_segments_;
  std::optional<ift::common::GlyphSet> candidate_patch_merge_glyphs_;

  // This is the set of segments under consideration for being merged into the
  // init font. Typically contains segments that were removed from
  // inscope_segments_ for being shared with other groups.
  const ift::common::SegmentSet inscope_segments_for_init_move_;

  // Segments greater than this value do not have optimization used when
  // selecting merges. Merging is done via simple selection until minimum group
  // sizes are met.
  segment_index_t optimization_cutoff_segment_;

  // Percent reduction of data beyond the single largest input patch.
  absl::btree_map<int32_t, uint32_t> merged_size_reduction_histogram_;

  // Per-segment cached aggregate probability, lower-bound aggregate
  // probability, and exclusive patch size in bytes.
  mutable std::vector<double> cached_agg_prob_;
  mutable std::vector<double> cached_min_agg_prob_;
  mutable std::vector<uint32_t> cached_exclusive_size_;
  mutable std::optional<uint32_t> cached_base_overhead_;
  mutable const PatchSizeCache* cached_patch_size_cache_ = nullptr;
  mutable bool exclusive_sizes_dirty_ = true;

  AllPairsMerger all_pairs_merger_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_MERGER_H_
