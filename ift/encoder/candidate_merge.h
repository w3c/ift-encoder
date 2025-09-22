#ifndef IFT_ENCODER_CANDIDATE_MERGE_H_
#define IFT_ENCODER_CANDIDATE_MERGE_H_

#include <cstdint>
#include <optional>

#include "absl/status/statusor.h"
#include "common/int_set.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/segmentation_context.h"
#include "ift/freq/probability_bound.h"

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

  // Inert probability threshold computation cache
  double base_size = 0.0;
  double base_probability = 0.0;
  double network_overhead = 0.0;

  static CandidateMerge BaselineCandidate(uint32_t base_segment_index,
                                          double cost_delta, double base_size,
                                          double base_probability,
                                          double network_overhead) {
    return CandidateMerge{
        .base_segment_index = base_segment_index,
        .segments_to_merge = {base_segment_index},
        .merged_segment = Segment({}, freq::ProbabilityBound::Zero()),
        .new_segment_is_inert = true,
        .new_patch_size = 0,
        .cost_delta = cost_delta,
        .invalidated_glyphs = {},
        .base_size = base_size,
        .base_probability = base_probability,
        .network_overhead = network_overhead};
  }

  // This is the estimated smallest possible increase in a patch size as a
  // result of a merge (ie. assuming the added glyph(s) are redundant with the
  // base and cost 0 to encode). This is roughly the number of bytes that would
  // be added by including a single extra gid into the patch header.
  static constexpr unsigned BEST_CASE_MERGE_SIZE_DELTA = 6;

  bool operator==(const CandidateMerge& other) const {
    // base segment and segments to merge uniquely identify a candidate
    // merge operation.
    return base_segment_index == other.base_segment_index &&
           segments_to_merge == other.segments_to_merge;
  }

  bool operator<(const CandidateMerge& other) const {
    if (cost_delta != other.cost_delta) {
      return cost_delta < other.cost_delta;
    }
    if (base_segment_index != other.base_segment_index) {
      return base_segment_index < other.base_segment_index;
    }
    return segments_to_merge < other.segments_to_merge;
  }

  // Given some candidate merge this computes the minimum probability an inert
  // segment must have for it to be possible to have a lower cost delta than
  // this one. Used to prefilter merges and avoid expensive cost delta
  // calculations.
  double InertProbabilityThreshold(uint32_t patch_size, double merged_probability) const {
    // The threshold calculation here was worked out by hand by considering the
    // equation:
    //
    // minimum cost delta > best case merged size * merge probability
    //                      - total base size * base probability
    //                      - total patch size * patch probability
    //
    // The threshold is then found by solving for patch probability in the above
    // inequality.
    //
    // Note: because the to be merged patch is inert we need to only consider
    // the contributions of the base patch and the to be merged patch.

    // For the best case merged size we assume complete overlap between the two
    // merged patches so that the new size is just the larger of the two patches
    // to be merged, plus the byte cost of adding at least one more gid into the
    // patch header.
    double best_case_merged_size = std::max(base_size, (double)patch_size) +
                                   network_overhead +
                                   BEST_CASE_MERGE_SIZE_DELTA;
    double total_base_size = base_size + network_overhead;
    double total_patch_size = patch_size + network_overhead;


    double numerator = merged_probability * best_case_merged_size - base_probability * total_base_size - cost_delta;
    double min_p = std::min(std::max(numerator / total_patch_size, 0.0), 1.0);
    return min_p;
  }

  // Applies this merge operation to the given SegmentationContext.
  common::GlyphSet Apply(SegmentationContext& context);

  static absl::StatusOr<std::optional<CandidateMerge>> AssessMerge(
      SegmentationContext& context, segment_index_t base_segment_index,
      const common::SegmentSet& segments_to_merge_,
      const std::optional<CandidateMerge>& best_merge_candidate);

  // Computes the estimated size of the patch for a segment and returns true if
  // it is below the minimum.
  static absl::StatusOr<bool> IsPatchTooSmall(
      SegmentationContext& context, segment_index_t base_segment_index,
      const common::GlyphSet& glyphs);
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_CANDIDATE_MERGE_H_