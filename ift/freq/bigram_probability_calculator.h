#ifndef IFT_FREQ_BIGRAM_PROBABILITY_CALCULATOR_H_
#define IFT_FREQ_BIGRAM_PROBABILITY_CALCULATOR_H_

#include "common/int_set.h"
#include "ift/freq/probability_bound.h"
#include "ift/freq/probability_calculator.h"
#include "ift/freq/unicode_frequencies.h"

namespace ift::freq {

// The BigramProbabilityCalculator uses unigram and bigram codepoint frequency
// data to compute probability bounds for codepoint sets. Unlike the unigram
// calculator this one does not assume independence between codepoints.
// As a result it will return a range of probability instead of a single value
// since we only have unigram and bigram frequency data which is not sufficient
// to compute the true probability.
class BigramProbabilityCalculator : public ProbabilityCalculator {
 public:
  explicit BigramProbabilityCalculator(UnicodeFrequencies frequencies);

  ProbabilityBound ComputeProbability(
      const ift::encoder::SubsetDefinition& definition) const override;

  ProbabilityBound ComputeMergedProbability(
      const std::vector<const ift::encoder::Segment*>& segments) const override;

  ProbabilityBound ComputeConjunctiveProbability(
      const std::vector<const ift::encoder::Segment*>& segments) const override;

 private:
  ProbabilityBound BigramProbabilityBound(
      const common::CodepointSet& codepoints, double current_best_lower) const;

  UnicodeFrequencies frequencies_;
};

}  // namespace ift::freq

#endif  // IFT_FREQ_BIGRAM_PROBABILITY_CALCULATOR_H_
