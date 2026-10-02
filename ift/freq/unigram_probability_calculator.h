#ifndef IFT_FREQ_UNIGRAM_PROBABILITY_CALCULATOR_H_
#define IFT_FREQ_UNIGRAM_PROBABILITY_CALCULATOR_H_

#include "ift/freq/bigram_probability_calculator.h"
#include "ift/freq/lru_cache.h"
#include "ift/freq/probability_calculator.h"
#include "ift/freq/segment_probability_cache.h"
#include "ift/freq/unicode_frequencies.h"

namespace ift::freq {

constexpr size_t UNIGRAM_PROBABILITY_CACHE_SIZE = 300000;

// The UnigramProbabilityCalculator calculates segment probabilites of occurence
// using unigram's (ie. one probability per codepoint). Because no additional
// probability data is present (such as co-occurrence probabilities) the
// calculations assume that these unigram probabilities are fully independent.
class UnigramProbabilityCalculator : public ProbabilityCalculator {
 public:
  explicit UnigramProbabilityCalculator(
      UnicodeFrequencies frequencies,
      size_t max_cache_size = UNIGRAM_PROBABILITY_CACHE_SIZE);

  absl::string_view Name() const override {
    return frequencies_.Name();
  }

  ift::common::CodepointSet CoveredCodepoints() const override {
    return frequencies_.CoveredCodepoints();
  }

  ProbabilityBound ComputeProbability(uint32_t codepoint) const override {
    double p = frequencies_.ProbabilityFor(codepoint);
    return {p, p};
  }

  ProbabilityBound ComputeProbability(
      absl::Span<const ift::encoder::Segment> segments,
      ift::encoder::segment_index_t segment_index) const override;

  ProbabilityBound ComputeMergedProbability(
      absl::Span<const ift::encoder::Segment> segments,
      const ift::common::SegmentSet& segment_indices) const override;

  ProbabilityBound ComputeConjunctiveProbability(
      const std::vector<ProbabilityBound>& bounds) const override;

  void InvalidateSegmentProbabilities(
      const ift::common::SegmentSet& segments) const override {
    segment_cache_.Invalidate(segments);
  }

  void ResetSegmentProbabilities(size_t num_segments) const override {
    segment_cache_.Reset(num_segments);
  }

  std::unique_ptr<BigramProbabilityCalculator> ToBigramCalculator() && {
    return std::make_unique<BigramProbabilityCalculator>(std::move(frequencies_));
  }

 private:
  ProbabilityBound ComputeProbability(
      const ift::encoder::SubsetDefinition& definition) const;

  UnicodeFrequencies frequencies_;
  mutable LruCache<ift::common::CodepointSet, std::optional<double>> cache_;
  mutable SegmentProbabilityCache<ProbabilityBound> segment_cache_;
};

}  // namespace ift::freq

#endif  // IFT_FREQ_UNIGRAM_PROBABILITY_CALCULATOR_H_
