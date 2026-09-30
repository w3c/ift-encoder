#ifndef IFT_FREQ_PROBABILITY_CALCULATOR_H_
#define IFT_FREQ_PROBABILITY_CALCULATOR_H_

#include <vector>

#include "absl/types/span.h"
#include "ift/common/int_set.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/types.h"
#include "ift/freq/probability_bound.h"

namespace ift::freq {

class ProbabilityCalculator {
 public:
  virtual ~ProbabilityCalculator() = default;

  virtual absl::string_view Name() const = 0;

  virtual ift::common::CodepointSet CoveredCodepoints() const = 0;

  virtual ProbabilityBound ComputeProbability(uint32_t codepoint) const = 0;

  virtual ProbabilityBound ComputeProbability(
      absl::Span<const ift::encoder::Segment> segments,
      ift::encoder::segment_index_t segment_index) const = 0;

  virtual ProbabilityBound ComputeMergedProbability(
      absl::Span<const ift::encoder::Segment> segments,
      const ift::common::SegmentSet& segment_indices) const = 0;

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