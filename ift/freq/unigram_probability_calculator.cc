#include "ift/freq/unigram_probability_calculator.h"

#include "hb.h"
#include "ift/encoder/segment.h"

using ift::encoder::Segment;
using ift::encoder::SubsetDefinition;

namespace ift::freq {

UnigramProbabilityCalculator::UnigramProbabilityCalculator(
    UnicodeFrequencies frequencies)
    : frequencies_(std::move(frequencies)) {}

ProbabilityBound UnigramProbabilityCalculator::ComputeProbability(
    const SubsetDefinition& definition) const {
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

ProbabilityBound UnigramProbabilityCalculator::ComputeMergedProbability(
    const std::vector<const Segment*>& segments) const {
  // Note: this assumes that all segments are disjoint. Which we enforce for
  // the inputs to cost based merging.
  double probability_of_none = 1.0;
  for (const auto* s : segments) {
    probability_of_none *= (1.0 - s->Probability());
  }
  double p = 1.0 - probability_of_none;
  return {p, p};
}

ProbabilityBound UnigramProbabilityCalculator::ComputeConjunctiveProbability(
    const std::vector<const Segment*>& segments) const {
  double probability = 1.0;
  for (const auto* segment : segments) {
    probability *= segment->Probability();
  }
  return {probability, probability};
}

}  // namespace ift::freq
