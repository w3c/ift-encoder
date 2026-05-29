#ifndef IFT_FREQ_BIGRAM_PROBABILITY_CALCULATOR_H_
#define IFT_FREQ_BIGRAM_PROBABILITY_CALCULATOR_H_

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "ift/common/int_set.h"
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
      const std::vector<ProbabilityBound>& bounds) const override;

  void ResetCache() const override {
    if (cache_hit_ || cache_miss_) {
      VLOG(1) << "bigram prob cache hit % = "
              << ((double)cache_hit_ / (double)(cache_hit_ + cache_miss_)) *
                     100.0;
    }
    cache_hit_ = 0;
    cache_miss_ = 0;
    cache_.clear();
  }

 private:
  ProbabilityBound BigramProbabilityBound(
      const ift::common::CodepointSet& codepoints,
      double current_best_lower) const;

  ProbabilityBound ComputeProbabilityInternal(
      const ift::encoder::SubsetDefinition& definition,
      double best_lower) const;

  UnicodeFrequencies frequencies_;
  mutable absl::flat_hash_map<ift::common::CodepointSet, ProbabilityBound>
      cache_;
  mutable uint64_t cache_hit_ = 0;
  mutable uint64_t cache_miss_ = 0;
};

}  // namespace ift::freq

#endif  // IFT_FREQ_BIGRAM_PROBABILITY_CALCULATOR_H_
