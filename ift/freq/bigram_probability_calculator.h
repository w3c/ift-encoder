#ifndef IFT_FREQ_BIGRAM_PROBABILITY_CALCULATOR_H_
#define IFT_FREQ_BIGRAM_PROBABILITY_CALCULATOR_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "absl/container/btree_set.h"
#include "ift/common/int_set.h"
#include "ift/freq/lru_cache.h"
#include "ift/freq/probability_bound.h"
#include "ift/freq/probability_calculator.h"
#include "ift/freq/segment_probability_cache.h"
#include "ift/freq/unicode_frequencies.h"

namespace ift::freq {

constexpr size_t BIGRAM_PROBABILITY_CACHE_SIZE = 300000;

// The BigramProbabilityCalculator uses unigram and bigram codepoint frequency
// data to compute probability bounds for codepoint sets. Unlike the unigram
// calculator this one does not assume independence between codepoints.
// As a result it will return a range of probability instead of a single value
// since we only have unigram and bigram frequency data which is not sufficient
// to compute the true probability.
class BigramProbabilityCalculator : public ProbabilityCalculator {
 public:
  explicit BigramProbabilityCalculator(
      UnicodeFrequencies frequencies,
      size_t max_cache_size = BIGRAM_PROBABILITY_CACHE_SIZE);

  absl::string_view Name() const override {
    return frequencies_.Name();
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

 private:
  struct BigramSegmentCacheEntry {
    ProbabilityBound codepoints_bound;
    ProbabilityBound bound;
    std::vector<uint32_t> cps;
    std::vector<double> unigram_probs;
    std::vector<double> partial_totals;
    double unigram_total = 0.0;
    double bigram_total = 0.0;
    double max_single_bound = 0.0;
    double max_pair_bound = 0.0;
    bool saturated = false;
  };

  ProbabilityBound BigramProbabilityBound(
      const ift::common::CodepointSet& codepoints,
      double current_best_lower) const;

  ProbabilityBound ApplyFeatureTags(
      const absl::btree_set<hb_tag_t>& feature_tags,
      ProbabilityBound codepoints_bound) const;

  ProbabilityBound ApplyLowerBoundAndFeatureTags(
    const absl::btree_set<hb_tag_t>& feature_tags,
    double best_lower,
    ProbabilityBound codepoints_bound) const;

  BigramSegmentCacheEntry ComputeSegmentEntry(
      const ift::encoder::SubsetDefinition& definition) const;

  const BigramSegmentCacheEntry& GetOrComputeSegmentEntry(
      absl::Span<const ift::encoder::Segment> segments,
      ift::encoder::segment_index_t segment_index) const;

  UnicodeFrequencies frequencies_;
  mutable LruCache<ift::common::CodepointSet, std::optional<ProbabilityBound>>
      cache_;
  mutable SegmentProbabilityCache<BigramSegmentCacheEntry> segment_cache_;
};

}  // namespace ift::freq

#endif  // IFT_FREQ_BIGRAM_PROBABILITY_CALCULATOR_H_
