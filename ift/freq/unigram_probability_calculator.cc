#include "ift/freq/unigram_probability_calculator.h"

namespace ift::freq {

UnigramProbabilityCalculator::UnigramProbabilityCalculator(
    const UnicodeFrequencies& frequencies)
    : frequencies_(frequencies) {}

ProbabilityBound UnigramProbabilityCalculator::ComputeProbability(
    const ift::encoder::SubsetDefinition& definition) const {
  double probability_of_none = 1.0;
  for (uint32_t cp : definition.codepoints) {
    probability_of_none *= (1.0 - frequencies_.ProbabilityFor(cp));
  }
  double probability = 1.0 - probability_of_none;
  return {probability, probability};
}

}  // namespace ift::freq
