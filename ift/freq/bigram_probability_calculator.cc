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

ProbabilityBound BigramProbabilityCalculator::BigramProbabilityBound(
    const CodepointSet& codepoints, double best_lower) const {
  double unigram_total = 0.0;
  double bigram_total = 0.0;
  double max_partial_bigram_total = 0.0;
  double max_single_bound = 0.0;
  double max_pair_bound = 0.0;

  for (auto it1 = codepoints.begin(); it1 != codepoints.end(); it1++) {
    if (max_single_bound >= 1.0) {
      // Bounds can't be lower than [1, 1] stop checking.
      return ProbabilityBound(1.0, 1.0);
    }

    double partial_total = 0.0;
    unsigned cp1 = *it1;
    double P1 = frequencies_.ProbabilityFor(cp1);
    unigram_total += P1;
    max_single_bound = std::max(P1, max_single_bound);
    for (auto it2 = codepoints.begin(); it2 != codepoints.end(); it2++) {
      unsigned cp2 = *it2;
      if (cp1 == cp2) {
        continue;
      }
      double P12 = frequencies_.ProbabilityFor(cp1, cp2);
      partial_total += P12;
      if (cp1 < cp2) {
        max_pair_bound = std::max(P1 + frequencies_.ProbabilityFor(cp2) - P12,
                                  max_pair_bound);
        if (max_pair_bound >= 1.0) {
          // Bounds can't be lower than [1, 1] stop checking.
          return ProbabilityBound(1.0, 1.0);
        }
        bigram_total += P12;
      }
    }
    max_partial_bigram_total =
        std::max(partial_total, max_partial_bigram_total);
  }

  // The bounds calculations are based on the Kounias bounds:
  // https://projecteuclid.org/journals/annals-of-mathematical-statistics/volume-39/issue-6/Bounds-for-the-Probability-of-a-Union-with-Applications/10.1214/aoms/1177698049.full

  // == Lower Bound ==
  // A lower bound is given by the greater of three values:
  // - Either the largest individual codepoint frequency.
  // - max(Pi + Pj - Pij)
  // - Or: sum(Pi) - sum(Pj<k)
  double lower =
      std::max(std::max(std::max(unigram_total - bigram_total, max_pair_bound),
                        max_single_bound),
               best_lower);

  // == Upper Bound ==
  // An upper bound is given by
  // sum(Pi) - max_j=1..n [ sum_j!=k(Pjk) ]
  double upper =
      std::max(std::min(unigram_total - max_partial_bigram_total, 1.0), lower);

  return ProbabilityBound(lower, upper);
}

ProbabilityBound BigramProbabilityCalculator::ComputeProbability(
    const SubsetDefinition& definition) const {
  if (definition.Empty()) {
    return {1, 1};
  }
  // TODO(garretrieger): XXXX incorporate layout tags
  return BigramProbabilityBound(definition.codepoints, 0.0);
}

ProbabilityBound BigramProbabilityCalculator::ComputeMergedProbability(
    const std::vector<const Segment*>& segments) const {
  // This assumes that segments are all disjoint, which is enforced in
  // ClosureGlyphSegmenter::CodepointToGlyphSegments().
  double best_lower = 0.0;
  for (const auto* s : segments) {
    best_lower = std::max(best_lower, s->ProbabilityBound().Min());
    if (best_lower >= 1.0) {
      // Since this is a union the bound must be [1, 1]
      return ProbabilityBound(1.0, 1.0);
    }
  }

  SubsetDefinition union_def;
  for (const auto* s : segments) {
    union_def.Union(s->Definition());
  }

  // TODO(garretrieger): we can potentially cache information in the segment
  //                     probability bound from the previous prob calculations
  //                     that could be used during this merge to accelerate
  //                     the computation. For example the unigram and bigram
  //                     sums. Example code follows, would need to be updated
  //                     to also handle the pair probabilities, and the upper
  //                     bound calculations.
  /*
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
  */
  return BigramProbabilityBound(union_def.codepoints, best_lower);
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
