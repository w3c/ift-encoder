#ifndef IFT_FREQ_UNIGRAM_PROBABILITY_CALCULATOR_H_
#define IFT_FREQ_UNIGRAM_PROBABILITY_CALCULATOR_H_

#include "ift/freq/probability_calculator.h"
#include "ift/freq/unicode_frequencies.h"

namespace ift::freq {

// The UnigramProbabilityCalculator calculates segment probabilites of occurence
// using unigram's (ie. one probability per codepoint). Because no additional
// probability data is present (such as co-occurrence probabilities) the
// calculations assume that these unigram probabilities are fully independent.
class UnigramProbabilityCalculator : public ProbabilityCalculator {
 public:
  explicit UnigramProbabilityCalculator(UnicodeFrequencies frequencies);

  ProbabilityBound ComputeProbability(
      const ift::encoder::SubsetDefinition& definition) const override;

  ProbabilityBound ComputeConjunctiveProbability(
      const std::vector<const ift::encoder::Segment*>& segments) const override;

 private:
  UnicodeFrequencies frequencies_;
};

}  // namespace ift::freq

#endif  // IFT_FREQ_UNIGRAM_PROBABILITY_CALCULATOR_H_
