#ifndef IFT_FREQ_MOCK_PROBABILITY_CALCULATOR_H_
#define IFT_FREQ_MOCK_PROBABILITY_CALCULATOR_H_

#include <vector>

#include "ift/encoder/segment.h"
#include "ift/freq/probability_calculator.h"

namespace ift::freq {

class MockProbabilityCalculator : public ProbabilityCalculator {
 public:
  MockProbabilityCalculator(std::vector<ift::encoder::Segment> segments)
      : segments_(segments) {}

  ProbabilityBound ComputeProbability(
      const ift::encoder::SubsetDefinition& definition) const override {
    for (const auto& segment : segments_) {
      if (segment.Definition() == definition) {
        return {segment.Probability(), segment.Probability()};
      }
    }
    return {0.0, 0.0};
  }

 private:
  std::vector<ift::encoder::Segment> segments_;
};

}  // namespace ift::freq

#endif  // IFT_FREQ_MOCK_PROBABILITY_CALCULATOR_H_