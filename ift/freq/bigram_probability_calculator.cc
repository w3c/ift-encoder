#include "ift/freq/bigram_probability_calculator.h"

#include "common/int_set.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/probability_calculator.h"

using common::CodepointSet;
using ift::encoder::Segment;
using ift::encoder::SubsetDefinition;

namespace ift::freq {

BigramProbabilityCalculator::BigramProbabilityCalculator(
    UnicodeFrequencies frequencies)
    : frequencies_(std::move(frequencies)) {}

double BigramProbabilityCalculator::UnigramProbabilitySum(
    const common::CodepointSet& codepoints) const {
  double total = 0.0;
  for (unsigned cp : codepoints) {
    total += frequencies_.ProbabilityFor(cp);
  }
  return total;
}

double BigramProbabilityCalculator::BigramProbabilitySum(
    const common::CodepointSet& codepoints) const {
  // This sums up the probabilities for all unique codepoint pairs from
  // codepoints.
  // TODO(garretrieger): this sum is expensive (O(n^2)) and since we'll be
  // computing it repeatedly
  //   for growing segments we can likely cache previous sums and build those up
  //   with the new entries instead of computing from scratch every time. This
  //   might be doable by capturing the unigram and bigram sums in Segment's and
  //   then changing the probability calculator call signature to take a base
  //   segment if it exists.
  double total = 0.0;
  auto it1 = codepoints.begin();
  for (; it1 != codepoints.end(); it1++) {
    auto it2 = it1;
    it2++;
    for (; it2 != codepoints.end(); it2++) {
      total += frequencies_.ProbabilityFor(*it1, *it2);
    }
  }
  return total;
}

ProbabilityBound BigramProbabilityCalculator::ComputeProbability(
    const SubsetDefinition& definition) const {
  if (definition.Empty()) {
    return {1, 1};
  }

  // Utilitizes the Bonferroni inequalities to compute probability bounds
  // based on codepoint unigram and bigram frequencies.
  // See:
  // https://en.wikipedia.org/wiki/Boole%27s_inequality#Bonferroni_inequalities
  double unigram_sum = UnigramProbabilitySum(definition.codepoints);
  double bigram_sum = BigramProbabilitySum(definition.codepoints);

  ProbabilityBound bounds{
      // K = 2: P >= ..
      std::max(std::min(unigram_sum - bigram_sum, 1.0), 0.0),
      // K = 1: P <= ...
      std::max(std::min(unigram_sum, 1.0), 0.0)};

  // TODO(garretrieger): XXXX layout tags

  return bounds;
}

ProbabilityBound BigramProbabilityCalculator::ComputeMergedProbability(
    const std::vector<const Segment*>& segments) const {
  // TODO XXX implement this using the cached uni/bigram sums once available in
  // Segment.
  SubsetDefinition merged;
  for (const auto* s : segments) {
    merged.Union(s->Definition());
  }
  return ComputeProbability(merged);
}

ProbabilityBound BigramProbabilityCalculator::ComputeConjunctiveProbability(
    const std::vector<const Segment*>& segments) const {
  // TODO XXXX
  return {0, 0};
}

}  // namespace ift::freq
