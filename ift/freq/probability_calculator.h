#ifndef IFT_FREQ_PROBABILITY_CALCULATOR_H_
#define IFT_FREQ_PROBABILITY_CALCULATOR_H_

#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"

namespace ift::freq {

struct ProbabilityBound {
  static ProbabilityBound Zero() { return ProbabilityBound{0.0, 0.0}; }

  ProbabilityBound(double min, double max) : min_(min), max_(max) {}

  double Min() const { return min_; }
  double Max() const { return max_; }

  bool operator==(const ProbabilityBound& other) const {
    return min_ == other.min_ && max_ == other.max_;
  }

  friend void PrintTo(const ProbabilityBound& point, std::ostream* os);

 private:
  double min_;
  double max_;
  // TODO XXXX add fields to cache unigram and bigram sums
};

class ProbabilityCalculator {
 public:
  virtual ~ProbabilityCalculator() = default;

  // Compute and returns the probability bounds on a page
  // intersecting the given subset definition.
  virtual ProbabilityBound ComputeProbability(
      const ift::encoder::SubsetDefinition& definition) const = 0;

  // Compute and returns the probability bounds on a page
  // intersecting the segment that result from merging the input segments
  //
  // May use previously computed probability information in segments
  // to speed up the computation.
  virtual ProbabilityBound ComputeMergedProbability(
      const std::vector<const ift::encoder::Segment*>& segments) const = 0;

  // Compute and return the probability bounds on a page intersecting
  // all of the input segments.
  //
  // May use previously computed probability information in the segments
  // to speed up the computation.
  virtual ProbabilityBound ComputeConjunctiveProbability(
      const std::vector<const ift::encoder::Segment*>& segments) const = 0;
};

}  // namespace ift::freq

#endif  // IFT_FREQ_PROBABILITY_CALCULATOR_H_