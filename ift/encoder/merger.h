#ifndef IFT_ENCODER_MERGER_
#define IFT_ENCODER_MERGER_

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
  Merger(SegmentationContext& context, MergeStrategy strategy,
         common::SegmentSet candidate_segments,
         segment_index_t optimization_cutoff_segment)
      : context_(&context),
        strategy_(strategy),
        candidate_segments_(candidate_segments),
        optimization_cutoff_segment_(optimization_cutoff_segment) {}

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

 private:
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

  void MarkFinished(segment_index_t s) { candidate_segments_.erase(s); }

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
