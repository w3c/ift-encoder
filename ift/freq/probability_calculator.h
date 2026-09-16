#ifndef IFT_FREQ_PROBABILITY_CALCULATOR_H_
#define IFT_FREQ_PROBABILITY_CALCULATOR_H_

#include <vector>

#include "absl/types/span.h"
#include "ift/common/int_set.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"
#include "ift/freq/probability_bound.h"

namespace ift::freq {

class ProbabilityCalculator {
 public:
  virtual ~ProbabilityCalculator() = default;

  // Compute and returns the probability bounds on a page
  // intersecting the given subset definition.
  // If segment_index is provided, the calculator may cache the result by
  // segment index.
  virtual ProbabilityBound ComputeProbability(
      const ift::encoder::SubsetDefinition& definition) const = 0;

  virtual ProbabilityBound ComputeProbability(
      absl::Span<const ift::encoder::Segment> segments,
      ift::encoder::segment_index_t segment_index) const = 0;

  // Compute and returns the probability bounds on a page
  // intersecting the segment that result from merging the input segments
  //
  // May use previously computed probability information in segments
  // to speed up the computation.
  virtual ProbabilityBound ComputeMergedProbability(
      const std::vector<const ift::encoder::Segment*>& segments) const = 0;

  virtual ProbabilityBound ComputeMergedProbability(
      absl::Span<const ift::encoder::Segment> segments,
      const ift::common::SegmentSet& segment_indices) const {
    std::vector<const ift::encoder::Segment*> segment_ptrs;
    segment_ptrs.reserve(segment_indices.size());
    for (ift::encoder::segment_index_t s : segment_indices) {
      segment_ptrs.push_back(&segments[s]);
    }
    return ComputeMergedProbability(segment_ptrs);
  }

  // Compute and return the probability bounds on a page intersecting
  // all of the input segments.
  //
  // May use previously computed probability information in the segments
  // to speed up the computation.
  virtual ProbabilityBound ComputeConjunctiveProbability(
      const std::vector<ProbabilityBound>& bounds) const = 0;

  // Invalidates cached segment probabilities for the specified segments.
  virtual void InvalidateSegmentProbabilities(
      const ift::common::SegmentSet& segments) const {}

  // Clears all cached segment probabilities and sizes the cache for
  // num_segments.
  virtual void ResetSegmentProbabilities(size_t num_segments) const {}
};

}  // namespace ift::freq

#endif  // IFT_FREQ_PROBABILITY_CALCULATOR_H_