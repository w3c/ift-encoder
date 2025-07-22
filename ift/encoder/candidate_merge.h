#ifndef IFT_ENCODER_CANDIDATE_MERGE_H_
#define IFT_ENCODER_CANDIDATE_MERGE_H_

#include <cstdint>
#include <optional>

#include "absl/status/statusor.h"
#include "common/int_set.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/segmentation_context.h"

namespace ift::encoder {

struct CandidateMerge {
  // The segment into which other segments will be merged.
  segment_index_t base_segment_index;

  // The set of segments to be merged into the base_segment_index.
  common::SegmentSet segments_to_merge;

  // The result of merge the above segments.
  Segment merged_segment;

  // If true the merge segment will be inert, that is it won't interact
  // with the closure.
  bool new_segment_is_inert;

  // Estimated size of the patch after merging.
  uint32_t new_patch_size;

  // The estimated change overall cost of the segmentation if this merge
  // were to be appiled.
  double cost_delta;

  // The set of glyphs that would be invalidated (need reprocessing) if this
  // merge is applied.
  common::GlyphSet invalidated_glyphs;

  bool operator<(const CandidateMerge& other) const {
    if (cost_delta != other.cost_delta) {
      return cost_delta < other.cost_delta;
    }
    if (base_segment_index != other.base_segment_index) {
      return base_segment_index < other.base_segment_index;
    }
    return segments_to_merge < other.segments_to_merge;
  }

  // Applies this merge operation to the given SegmentationContext.
  std::optional<common::GlyphSet> Apply(SegmentationContext& context);

  static absl::StatusOr<std::optional<CandidateMerge>> AssessMerge(
      SegmentationContext& context, segment_index_t base_segment_index,
      const common::SegmentSet& segments_to_merge_);

  // Computes the estimated size of the patch for a segment and returns true if
  // it is below the minimum.
  static absl::StatusOr<bool> IsPatchTooSmall(
      SegmentationContext& context, segment_index_t base_segment_index,
      const common::GlyphSet& glyphs);
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_CANDIDATE_MERGE_H_