#ifndef IFT_ENCODER_MERGE_STRATEGY_
#define IFT_ENCODER_MERGE_STRATEGY_

#include <algorithm>
#include <cstdint>
#include <memory>

#include "common/try.h"
#include "ift/freq/bigram_probability_calculator.h"
#include "ift/freq/noop_probability_calculator.h"
#include "ift/freq/probability_calculator.h"
#include "ift/freq/unicode_frequencies.h"
#include "ift/freq/unigram_probability_calculator.h"

namespace ift::encoder {

// Used to configure how segment merging is performed by the closure glyph
// segmenter.
//
// Configures both the specific algorithm used to select merges and the
// parameters to that algorithm.
class MergeStrategy {
 public:
  // No merging will be performed, just produced the glyph segmentation based on
  // the provided input segments.
  static MergeStrategy None() { return Heuristic(0, UINT32_MAX); }

  // A heuristic based merged will be performed that attempts to ensure patch
  // sizes are within the specified bounds.
  //
  // A heuristic is used to identify candidate segments for merge that are
  // expected to improve the overall segmentation. The heuristic prioritizes
  // first merging segments that interact with each other, then segments that
  // are close together in the input ordering. Merges are performed in priority
  // order until patches associated with each segment are within the specified
  // min/max limit.
  //
  // This will often be less optimal then the cost based strategy, but is faster
  // as far less merge candidates need to be evaluated.
  static MergeStrategy Heuristic(uint32_t patch_size_min_bytes,
                                 uint32_t patch_size_max_bytes = UINT32_MAX) {
    MergeStrategy strategy(false, 0, 0, patch_size_min_bytes,
                           patch_size_max_bytes);
    strategy.probability_calculator_ =
        std::make_unique<ift::freq::NoopProbabilityCalculator>();
    return strategy;
  }

  // Merging will be performed such that it attempts to minimize the total
  // estimated cost of the segmentation. Where cost is defined as the expected
  // number of bytes to be loaded on average. Requires segments to have
  // probabilities assigned to them. Also that the probability calculations
  // assume input segments are disjoint.
  //
  // Network overhead cost is a fixed number of bytes that is added to every
  // patch size. Setting it higher will encourage more aggressive merging, while
  // setting it lower will encourage less aggressive merging.
  static absl::StatusOr<MergeStrategy> CostBased(
      freq::UnicodeFrequencies frequency_data,
      uint32_t network_overhead_cost = 75, uint32_t min_group_size = 4) {
    if (!frequency_data.HasData()) {
      return absl::InvalidArgumentError(
          "If cost based merging is enabled unicode frequency data must be "
          "provided.");
    }

    MergeStrategy strategy(true, network_overhead_cost, min_group_size, 0,
                           UINT32_MAX);
    strategy.probability_calculator_ =
        std::make_unique<ift::freq::UnigramProbabilityCalculator>(
            std::move(frequency_data));
    return strategy;
  }

  // Merging will be performed such that it attempts to minimize the total
  // estimated cost of the segmentation. Works the same as CostBased() with
  // the following changes:
  // - When analyzing probabilities of segments being encountered the
  // calculations
  //   will include both individual codepoint and pair codepoint probabilities.
  // - Notably this means we don't need to assume independent codepoint
  // probabilities like
  //   "CostBased()" does.
  // - As a result this is more accurate, but more computationally costly.
  static absl::StatusOr<MergeStrategy> BigramCostBased(
      freq::UnicodeFrequencies frequency_data,
      uint32_t network_overhead_cost = 75, uint32_t min_group_size = 4) {
    if (!frequency_data.HasData()) {
      return absl::InvalidArgumentError(
          "If cost based merging is enabled unicode frequency data must be "
          "provided.");
    }

    MergeStrategy strategy(true, network_overhead_cost, min_group_size, 0,
                           UINT32_MAX);
    strategy.probability_calculator_ =
        std::make_unique<ift::freq::BigramProbabilityCalculator>(
            std::move(frequency_data));
    return strategy;
  }

  static MergeStrategy CostBased(
      std::unique_ptr<freq::ProbabilityCalculator> probability_calculator,
      uint32_t network_overhead_cost, uint32_t min_group_size) {
    MergeStrategy strategy(true, network_overhead_cost, min_group_size, 0,
                           UINT32_MAX);
    strategy.probability_calculator_ = std::move(probability_calculator);
    return strategy;
  }

  bool IsNone() const { return !use_costs_ && patch_size_min_bytes_ == 0; }
  bool UseCosts() const { return use_costs_; }
  uint32_t NetworkOverheadCost() const { return network_overhead_cost_; }
  uint32_t MinimumGroupSize() const { return min_group_size_; }
  uint32_t PatchSizeMinBytes() const { return patch_size_min_bytes_; }
  uint32_t PatchSizeMaxBytes() const { return patch_size_max_bytes_; }
  const freq::ProbabilityCalculator* ProbabilityCalculator() const {
    return probability_calculator_.get();
  }

  // Configures the threshold for when to stop optimizing segments.
  //
  // For the set of segments which account for less than this fraction of the
  // total cost don't do expensive optimized merging, just merge adjacent
  // segments.
  double OptimizationCutoffFraction() const {
    return optimization_cutoff_fraction_;
  }
  void SetOptimizationCutoffFraction(double value) {
    optimization_cutoff_fraction_ = value;
  }

  // Configures the brotli quality used when calculating patch sizes.
  // Defaults to 8.
  //
  // Higher qualities will result in more accurate patch
  // size calculations but can significantly increase calculation times.
  //
  // Inversely, lower qualities will result in less accurate patch size
  // calculations, but can speed up calculation times.
  void SetBrotliQuality(uint32_t value) {
    brotli_quality_ = std::max(std::min(value, 11u), 1u);
  }

  uint32_t BrotliQuality() const { return brotli_quality_; }

 private:
  MergeStrategy(bool use_costs, uint32_t network_overhead_cost,
                uint32_t min_group_size, uint32_t patch_size_min_bytes,
                uint32_t patch_size_max_bytes)
      : use_costs_(use_costs),
        network_overhead_cost_(network_overhead_cost),
        min_group_size_(min_group_size),
        patch_size_min_bytes_(patch_size_min_bytes),
        patch_size_max_bytes_(patch_size_max_bytes),
        probability_calculator_(nullptr) {}

  bool use_costs_;
  uint32_t network_overhead_cost_;
  uint32_t min_group_size_;
  uint32_t patch_size_min_bytes_;
  uint32_t patch_size_max_bytes_;
  // 9 and above are quite slow given the number of compressions that need to be
  // performed.
  uint32_t brotli_quality_ = 8;
  double optimization_cutoff_fraction_ = 0.001;

  std::unique_ptr<freq::ProbabilityCalculator> probability_calculator_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_MERGE_STRATEGY_
