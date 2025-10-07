#ifndef IFT_ENCODER_MERGER_
#define IFT_ENCODER_MERGER_

#include <cstdint>

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
  static absl::StatusOr<Merger> New(SegmentationContext& context,
                                    MergeStrategy strategy) {
    Merger merger(context, strategy,
                  ComputeCandidateSegments(context, strategy), UINT32_MAX);
    TRYV(merger.InitOptimizationCutoff());
    return merger;
  }

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

 private:
  Merger(SegmentationContext& context, MergeStrategy strategy,
         common::SegmentSet candidate_segments,
         segment_index_t optimization_cutoff_segment)
      : context_(&context),
        strategy_(strategy),
        candidate_segments_(candidate_segments),
        optimization_cutoff_segment_(optimization_cutoff_segment) {}

  static common::SegmentSet ComputeCandidateSegments(
      SegmentationContext& context, MergeStrategy strategy);
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

  common::SegmentSet CutoffSegments() const;

  void MarkFinished(segment_index_t s) { candidate_segments_.erase(s); }

  absl::StatusOr<bool> CheckAndApplyInitFontMove(
      const common::SegmentSet& candidate_segments,
      SubsetDefinition& initial_segment);

  // Stores the broadeder complete segmentation.
  SegmentationContext* context_;

  // Stores the settings that configure how merging operations are
  // selected and performed.
  MergeStrategy strategy_;

  // The current set of segments under consideration for being merged.
  common::SegmentSet candidate_segments_;

  // Segments greater than this value do not have optimization used when
  // selecting merges. Merging is done via simple selection until minimum group
  // sizes are met.
  segment_index_t optimization_cutoff_segment_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_STATE_
