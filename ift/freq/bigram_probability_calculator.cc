#include "ift/freq/bigram_probability_calculator.h"

#include "common/int_set.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/probability_bound.h"

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

  // TODO(garretrieger): We can use the approach described in
  // https://projecteuclid.org/journals/annals-of-mathematical-statistics/volume-39/issue-6/Bounds-for-the-Probability-of-a-Union-with-Applications/10.1214/aoms/1177698049.full
  // to compute a better upper bound:
  // P(union) <= sum(P(Si)) - max_k=1..n (sum_i!=k(P(Sk n Si)))

  // TODO(garretrieger): XXXX incorporate layout tags
  return ProbabilityBound::BonferroniBound(unigram_sum, bigram_sum);
}

ProbabilityBound BigramProbabilityCalculator::ComputeMergedProbability(
    const std::vector<const Segment*>& segments) const {
  // This assumes that segments are all disjoint, which is enforced in
  // ClosureGlyphSegmenter::CodepointToGlyphSegments().
  double unigram_sum = 0.0;
  double bigram_sum = 0.0;
  for (unsigned i = 0; i < segments.size(); i++) {
    const Segment* s1 = segments[i];
    bigram_sum += s1->ProbabilityBound().bigram_sum_;
    unigram_sum += s1->ProbabilityBound().unigram_sum_;

    // All of the bigram sums within a segment are already captured, we just
    // need to add the combinations between the input segments.
    for (unsigned j = i + 1; j < segments.size(); j++) {
      const Segment* s2 = segments[j];
      for (unsigned cp1 : s1->Definition().codepoints) {
        for (unsigned cp2 : s2->Definition().codepoints) {
          assert(cp1 != cp2);
          bigram_sum += frequencies_.ProbabilityFor(cp1, cp2);
        }
      }
    }
  }

  return ProbabilityBound::BonferroniBound(unigram_sum, bigram_sum);
}

ProbabilityBound BigramProbabilityCalculator::ComputeConjunctiveProbability(
    const std::vector<const Segment*>& segments) const {
  // Here we don't have access to pair probabilities between the segments so we
  // use a bound that relies only on the individual probabilities:
  //
  // sum(P(Si)) - (n - 1) <= P(intersection) <= min(P(Si))
  //
  // For the segments we actually have probability bounds, so use the segment
  // min for the lower bound calc and the segment max for the upper bound calc.
  double sum = 0.0;
  double min_of_maxes = 1.0;
  for (const Segment* s : segments) {
    const auto& bound = s->ProbabilityBound();
    sum += bound.Min();
    if (bound.Max() < min_of_maxes) {
      min_of_maxes = bound.Max();
    }
  }
  double min_prob = sum - (double)segments.size() + 1.0;
  return ProbabilityBound(std::max(0.0, min_prob), min_of_maxes);
}

}  // namespace ift::freq
