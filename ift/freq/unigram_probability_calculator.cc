#include "ift/freq/unigram_probability_calculator.h"

#include "hb.h"

namespace ift::freq {

UnigramProbabilityCalculator::UnigramProbabilityCalculator(
    UnicodeFrequencies frequencies)
    : frequencies_(std::move(frequencies)) {}

ProbabilityBound UnigramProbabilityCalculator::ComputeProbability(
    const ift::encoder::SubsetDefinition& definition) const {
  double probability_of_none = 1.0;
  for (uint32_t cp : definition.codepoints) {
    probability_of_none *= (1.0 - frequencies_.ProbabilityFor(cp));
  }

  for (hb_tag_t tag : definition.feature_tags) {
    probability_of_none *= (1.0 - frequencies_.ProbabilityForLayoutTag(tag));
  }

  double probability = 1.0 - probability_of_none;
  return {probability, probability};
}

}  // namespace ift::freq
