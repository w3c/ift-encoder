#ifndef IFT_FREQ_UNIGRAM_PROBABILITY_CALCULATOR_H_
#define IFT_FREQ_UNIGRAM_PROBABILITY_CALCULATOR_H_

#include "ift/freq/probability_calculator.h"
#include "ift/freq/unicode_frequencies.h"

namespace ift::freq {

class UnigramProbabilityCalculator : public ProbabilityCalculator {
 public:
  explicit UnigramProbabilityCalculator(UnicodeFrequencies frequencies);

  ProbabilityBound ComputeProbability(
      const ift::encoder::SubsetDefinition& definition) const override;

 private:
  UnicodeFrequencies frequencies_;
};

}  // namespace ift::freq

#endif  // IFT_FREQ_UNIGRAM_PROBABILITY_CALCULATOR_H_
