#ifndef IFT_ENCODER_MERGER_
#define IFT_ENCODER_MERGER_

#include <cstdint>
#include <sstream>

#include "common/int_set.h"
#include "ift/encoder/candidate_merge.h"
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
      common::SegmentSet inscope_segments,
      common::SegmentSet inscope_segments_for_init_move) {
    Merger merger(context, strategy, inscope_segments,
                  inscope_segments_for_init_move, UINT32_MAX);
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
  absl::StatusOr<std::optional<std::pair<segment_index_t, common::GlyphSet>>>
  TryNextMerge();

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
  absl::Status MoveSegmentsToInitFont();

  /*
   * Recompute the state of this merger to respect changes made to the
   * segmentation context to reconfigure the init subset.
   */
  absl::Status ReassignInitSubset();

  /*
   * Merges to_merge segments with base. base is set to merged_segment.
   */
  uint32_t AssignMergedSegment(segment_index_t base,
                               const common::SegmentSet& to_merge,
                               const Segment& merged_segment, bool is_inert);

  uint32_t NumCutoffSegments() const { return CutoffSegments().size(); }

  uint32_t NumInscopeSegments() const { return inscope_segments_.size(); }

  void RecordMergedSizeReduction(double size_reduction) {
    int32_t reduction_percent = 100.0 * size_reduction;
    merged_size_reduction_histogram_[reduction_percent]++;
  }

  bool ShouldRecordMergedSizeReductions() const;

  void LogMergedSizeHistogram() const;

 private:
  Merger(SegmentationContext& context, MergeStrategy strategy,
         common::SegmentSet inscope_segments,
         common::SegmentSet inscope_segments_for_init_move,
         segment_index_t optimization_cutoff_segment)
      : context_(&context),
        strategy_(strategy),
        inscope_segments_(inscope_segments),
        candidate_segments_(
            ComputeCandidateSegments(*context_, strategy_, inscope_segments_)),
        inscope_segments_for_init_move_(inscope_segments_for_init_move),
        optimization_cutoff_segment_(optimization_cutoff_segment) {}

  static common::SegmentSet ComputeCandidateSegments(
      SegmentationContext& context, const MergeStrategy& strategy,
      const common::SegmentSet& inscope_segments);

  absl::Status InitOptimizationCutoff();
  absl::StatusOr<segment_index_t> ComputeSegmentCutoff() const;

  absl::StatusOr<std::optional<common::GlyphSet>> MergeSegmentWithCosts(
      uint32_t base_segment_index);

  absl::Status CollectExclusiveCandidateMerges(
      uint32_t base_segment_index,
      std::optional<CandidateMerge>& smallest_candidate_merge);

  absl::Status CollectCompositeCandidateMerges(
      uint32_t base_segment_index,
      std::optional<CandidateMerge>& smallest_candidate_merge);

  absl::StatusOr<std::optional<common::GlyphSet>> MergeSegmentWithHeuristic(
      uint32_t base_segment_index);

  common::SegmentSet CutoffSegments() const;

  /*
   * Search for a base segment after base_segment_index which can be merged into
   * base_segment_index without exceeding the maximum patch size.
   *
   * Returns the set of glyphs invalidated by the merge if found and the merge
   * suceeded.
   */
  absl::StatusOr<std::optional<common::GlyphSet>> TryMergingABaseSegment(
      segment_index_t base_segment_index);

  absl::StatusOr<std::optional<common::GlyphSet>> TryMergingACompositeCondition(
      segment_index_t base_segment_index);

  absl::StatusOr<std::optional<common::GlyphSet>> TryMerge(
      segment_index_t base_segment_index,
      const common::SegmentSet& to_merge_segments_);

  void MarkFinished(segment_index_t s) { candidate_segments_.erase(s); }

  absl::Status ApplyInitFontMove(const common::GlyphSet& glyphs_to_move,
                                 double delta);

  // For a merge of an inert base patch with any other possible inert segment,
  // this computes the minimum probability the other segment must have for it to
  // be possible to produce a delta lower than lowest_cost_delta (regardless of
  // it's probability or size).
  //
  // This assumes that P(base) >= P(other)
  double BestCaseInertProbabilityThreshold(uint32_t base_patch_size,
                                           double base_probability,
                                           double lowest_cost_delta) const;

  common::SegmentSet InitFontApplyProbabilityThreshold() const;
  common::SegmentSet InitFontSegmentsToCheck(
      const common::SegmentSet& inscope) const;
  absl::btree_map<ActivationCondition, common::GlyphSet>
  InitFontConditionsToCheck(const common::SegmentSet& to_check,
                            bool batch_mode) const;

  // Stores the broadeder complete segmentation.
  SegmentationContext* context_;

  // Stores the settings that configure how merging operations are
  // selected and performed.
  const MergeStrategy strategy_;

  // The current set of segments under consideration for being merged.
  const common::SegmentSet inscope_segments_;
  common::SegmentSet candidate_segments_;

  // This is the set of segments under consideration for being merged into the
  // init font. Typically contains segments that were removed from
  // inscope_segments_ for being shared with other groups.
  const common::SegmentSet inscope_segments_for_init_move_;

  // Segments greater than this value do not have optimization used when
  // selecting merges. Merging is done via simple selection until minimum group
  // sizes are met.
  segment_index_t optimization_cutoff_segment_;

  // Percent reduction of data beyond the single largest input patch.
  absl::btree_map<int32_t, uint32_t> merged_size_reduction_histogram_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_STATE_
