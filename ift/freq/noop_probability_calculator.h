#ifndef IFT_FREQ_NOOP_PROBABILITY_CALCULATOR_H_
#define IFT_FREQ_NOOP_PROBABILITY_CALCULATOR_H_

#include "ift/freq/probability_calculator.h"

namespace ift::freq {

class NoopProbabilityCalculator : public ProbabilityCalculator {
 public:
  ~NoopProbabilityCalculator() override = default;

  ProbabilityBound ComputeProbability(
      const ift::encoder::SubsetDefinition& definition) const override {
    return {0.0, 0.0};
  }
};

}  // namespace ift::freq

#endif  // IFT_FREQ_NOOP_PROBABILITY_CALCULATOR_H_
