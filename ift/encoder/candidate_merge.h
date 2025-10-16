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

  // This is the estimated smallest possible increase in a patch size as a
  // result of a merge (ie. assuming the added glyph(s) are redundant with the
  // base and cost 0 to encode). This is roughly the number of bytes that would
  // be added by including a single extra gid into the patch header.
  static constexpr unsigned BEST_CASE_MERGE_SIZE_DELTA = 6;

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

  // Given some candidate merge this computes the minimum probability an inert
  // segment must have for it to be possible to have a lower cost delta than
  // this one. Used to prefilter merges and avoid expensive cost delta
  // calculations.
  double InertProbabilityThreshold(uint32_t patch_size,
                                   double merged_probability) const {
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
    double best_case_merged_size = std::max(base_size_, (double)patch_size) +
                                   network_overhead_ +
                                   BEST_CASE_MERGE_SIZE_DELTA;
    double total_base_size = base_size_ + network_overhead_;
    double total_patch_size =
        patch_size > 0 ? patch_size + network_overhead_ : 0;

    double numerator = merged_probability * best_case_merged_size -
                       base_probability_ * total_base_size - cost_delta_;
    double min_p = std::min(std::max(numerator / total_patch_size, 0.0), 1.0);
    return min_p;
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
  static absl::StatusOr<double> ComputeCostDelta(
      const Merger& context, const common::SegmentSet& merged_segments,
      const Segment& merged_segment, uint32_t new_patch_size);

  // Computes the predicted change to the toal cost if moved_glyphs are
  // moved from patches into the initial font.
  static absl::StatusOr<double> ComputeInitFontCostDelta(
      Merger& merger, uint32_t existing_init_font_size, bool best_case,
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