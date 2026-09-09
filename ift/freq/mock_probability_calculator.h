#ifndef IFT_FREQ_MOCK_PROBABILITY_CALCULATOR_H_
#define IFT_FREQ_MOCK_PROBABILITY_CALCULATOR_H_

#include <vector>

#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/probability_calculator.h"

namespace ift::freq {

class MockProbabilityCalculator : public ProbabilityCalculator {
 public:
  MockProbabilityCalculator(std::vector<std::pair<ift::encoder::Segment, double>> segments)
      : segments_(segments) {}

  ProbabilityBound ComputeProbability(
      const ift::encoder::SubsetDefinition& definition) const override {
    for (const auto& [segment, prob] : segments_) {
      if (segment.Definition() == definition) {
        return {prob, prob};
      }
    }
    return {0.0, 0.0};
  }

  ProbabilityBound ComputeMergedProbability(
      const std::vector<const ift::encoder::Segment*>& segments)
      const override {
    ift::encoder::SubsetDefinition merged;
    for (const auto* s : segments) {
      merged.Union(s->Definition());
    }
    return ComputeProbability(merged);
  }

  ProbabilityBound ComputeConjunctiveProbability(
      const std::vector<ProbabilityBound>& bounds) const override {
    double probability = 1.0;
    for (const auto& bound : bounds) {
      probability *= bound.Value();
    }
    return {probability, probability};
  }

 private:
  std::vector<std::pair<ift::encoder::Segment, double>> segments_;
};

}  // namespace ift::freq

#endif  // IFT_FREQ_MOCK_PROBABILITY_CALCULATOR_H_