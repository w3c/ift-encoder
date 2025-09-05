#ifndef IFT_FREQ_PROBABILITY_CALCULATOR_H_
#define IFT_FREQ_PROBABILITY_CALCULATOR_H_

#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"

namespace ift::freq {

struct ProbabilityBound {
  double min;
  double max;
};

class ProbabilityCalculator {
 public:
  virtual ~ProbabilityCalculator() = default;

  virtual ProbabilityBound ComputeProbability(
      const ift::encoder::SubsetDefinition& definition) const = 0;

  virtual ProbabilityBound ComputeConjunctiveProbability(
      const std::vector<const ift::encoder::Segment*>& segments) const = 0;
};

}  // namespace ift::freq

#endif  // IFT_FREQ_PROBABILITY_CALCULATOR_H_