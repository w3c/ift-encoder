#ifndef IFT_ENCODER_CANDIDATE_MERGE_H_
#define IFT_ENCODER_CANDIDATE_MERGE_H_

#include <cstdint>
#include <optional>

#include "absl/status/statusor.h"
#include "common/int_set.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/types.h"
#include "ift/freq/probability_bound.h"

namespace ift::encoder {

class Merger;
class CandidateMergeTest;

struct CandidateMerge {
  friend class CandidateMergeTest;

 private:
  // The segment into which other segments will be merged.
  segment_index_t base_segment_index_;

  // The set of segments to be merged into the base_segment_index.
  common::SegmentSet segments_to_merge_;

  // The result of merge the above segments. If it's not present then
  // that implies this merge is only a merge of the base segment patch
  // and the disjunctive patch with the condition segments_to_merge_.
  std::optional<Segment> merged_segment_;

  // If true the all of the segments participating in this merge are inert.
  bool input_segments_are_inert_;

  // Estimated size of the patch after merging.
  uint32_t new_patch_size_;

  // The estimated change overall cost of the segmentation if this merge
  // were to be appiled.
  double cost_delta_;

  // The set of glyphs that would be invalidated (need reprocessing) if this
  // merge is applied.
  common::GlyphSet invalidated_glyphs_;

  // Inert probability threshold computation cache
  double base_size_ = 0.0;
  double base_probability_ = 0.0;
  double network_overhead_ = 0.0;

  CandidateMerge(Segment merged_segment) : merged_segment_(merged_segment) {}
  CandidateMerge() : merged_segment_(std::nullopt) {}

 public:
  static CandidateMerge BaselineCandidate(uint32_t base_segment_index,
                                          double cost_delta, double base_size,
                                          double base_probability,
                                          double network_overhead) {
    CandidateMerge merge(Segment({}, freq::ProbabilityBound::Zero()));
    merge.base_segment_index_ = base_segment_index;
    merge.segments_to_merge_ = {base_segment_index};
    merge.input_segments_are_inert_ = true;
    merge.new_patch_size_ = 0;
    merge.cost_delta_ = cost_delta;
    merge.invalidated_glyphs_ = {};
    merge.base_size_ = base_size;
    merge.base_probability_ = base_probability;
    merge.network_overhead_ = network_overhead;
    return merge;
  }

  const common::SegmentSet& SegmentsToMerge() const {
    return segments_to_merge_;
  }

  double CostDelta() const { return cost_delta_; }


  bool operator==(const CandidateMerge& other) const {
    // base segment and segments to merge uniquely identify a candidate
    // merge operation.
    return base_segment_index_ == other.base_segment_index_ &&
           segments_to_merge_ == other.segments_to_merge_ &&
           merged_segment_.has_value() == other.merged_segment_.has_value();
  }

  bool operator<(const CandidateMerge& other) const {
    if (cost_delta_ != other.cost_delta_) {
      return cost_delta_ < other.cost_delta_;
    }
    if (merged_segment_.has_value() != other.merged_segment_.has_value()) {
      // Preference segment merges over direct patch merging.
      return merged_segment_.has_value();
    }
    if (base_segment_index_ != other.base_segment_index_) {
      return base_segment_index_ < other.base_segment_index_;
    }
    return segments_to_merge_ < other.segments_to_merge_;
  }

  // Applies this merge operation to the given SegmentationContext.
  absl::StatusOr<common::GlyphSet> Apply(Merger& context);

 private:
  absl::Status ApplyPatchMerge(Merger& context);

 public:
  // Assess the results of merge base_segment_index with segments_to_merge
  // to produce a new combined segment.
  //
  // If the merge is not better than best_merge_candidate or not possible
  // then nullopt will be returned.
  //
  // Returns a candidate merge object which stores information on the merge.
  static absl::StatusOr<std::optional<CandidateMerge>> AssessSegmentMerge(
      Merger& context, segment_index_t base_segment_index,
      const common::SegmentSet& segments_to_merge_,
      const std::optional<CandidateMerge>& best_merge_candidate);

  // Assess the resutl of merging together exactly two patches:
  // 1. The exclusive patch for base_segment_index.
  // 2. The patch associated with the disjunctive segments_to_merge_ condition.
  //
  // If the merge is not better than best_merge_candidate or not possible
  // then nullopt will be returned.
  //
  // Returns a candidate merge object which stores information on the merge.
  static absl::StatusOr<std::optional<CandidateMerge>> AssessPatchMerge(
      Merger& context, segment_index_t base_segment_index,
      const common::SegmentSet& segments_to_merge_,
      const std::optional<CandidateMerge>& best_merge_candidate);

  // Computes the estimated size of the patch for a segment and returns true if
  // it is below the minimum.
  static absl::StatusOr<bool> IsPatchTooSmall(
      Merger& context, segment_index_t base_segment_index,
      const common::GlyphSet& glyphs);

  // Computes the predicted change to the total cost if merged_segments
  // are joined together into a new segment, merged_segment.
  //
  // If new_patch_size is not provided then this computes a "best case" delta
  // where the new patch size is choosen to produce the best achievable delta.
  static absl::StatusOr<double> ComputeCostDelta(
      Merger& merger, const common::SegmentSet& merged_segments,
      const Segment& merged_segment, std::optional<uint32_t> new_patch_size);

  // Computes the predicted change to the toal cost if moved_glyphs are
  // moved from patches into the initial font.
  //
  // Returns the cost delta, and the full set of glyphs that will be moved
  // (including those added by closure).
  static absl::StatusOr<std::pair<double, common::GlyphSet>>
  ComputeInitFontCostDelta(Merger& merger, uint32_t existing_init_font_size,
                           bool best_case,
                           const common::GlyphSet& moved_glyphs);

  static absl::StatusOr<double> ComputePatchMergeCostDelta(
      const Merger& context, segment_index_t base_segment,
      const common::GlyphSet& base_glyphs,
      const common::SegmentSet& target_segments,
      const common::GlyphSet& target_glyphs,
      const common::GlyphSet& merged_glyphs);

  static absl::StatusOr<uint32_t> Woff2SizeOf(hb_face_t* original_face,
                                              const SubsetDefinition& def,
                                              int quality);
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_CANDIDATE_MERGE_H_