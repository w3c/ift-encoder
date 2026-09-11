#ifndef IFT_ENCODER_MERGE_STRATEGY_
#define IFT_ENCODER_MERGE_STRATEGY_

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "ift/freq/bigram_probability_calculator.h"
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
  friend void PrintTo(const MergeStrategy& strategy, std::ostream* os);

  struct ProbabilityProfile {
    ProbabilityProfile() = default;

    explicit ProbabilityProfile(
        std::shared_ptr<freq::ProbabilityCalculator> calculator,
        std::optional<double> init_font_merge_threshold = std::nullopt,
        std::optional<double> init_font_merge_probability_threshold =
            std::nullopt)
        : calculator(std::move(calculator)),
          init_font_merge_threshold(init_font_merge_threshold),
          init_font_merge_probability_threshold(
              init_font_merge_probability_threshold) {}

    static absl::StatusOr<ProbabilityProfile> Unigram(
        freq::UnicodeFrequencies&& frequency_data) {
      if (!frequency_data.HasData()) {
        return absl::InvalidArgumentError("Frequency data is missing.");
      }
      return ProbabilityProfile(
          std::make_shared<ift::freq::UnigramProbabilityCalculator>(
              std::move(frequency_data)));
    }

    static absl::StatusOr<ProbabilityProfile> Bigram(
        freq::UnicodeFrequencies&& frequency_data) {
      if (!frequency_data.HasData()) {
        return absl::InvalidArgumentError("Frequency data is missing.");
      }
      return ProbabilityProfile(
          std::make_shared<ift::freq::BigramProbabilityCalculator>(
              std::move(frequency_data)));
    }

    std::shared_ptr<freq::ProbabilityCalculator> calculator;
    std::optional<double> init_font_merge_threshold = std::nullopt;
    std::optional<double> init_font_merge_probability_threshold = std::nullopt;

    absl::StatusOr<const freq::ProbabilityCalculator*> Calculator() const {
      if (!calculator) {
        return absl::InvalidArgumentError(
            "Probability profile is missing a probability calculator.");
      }
      return calculator.get();
    }

    bool operator==(const ProbabilityProfile& other) const {
      return (calculator == nullptr) == (other.calculator == nullptr) &&
             init_font_merge_threshold == other.init_font_merge_threshold &&
             init_font_merge_probability_threshold ==
                 other.init_font_merge_probability_threshold;
    }
  };

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
    // No probability profiles are added here: heuristic (and none) strategies
    // never consult probabilities, every read of ProbabilityProfiles() is
    // gated behind UseCosts().
    return MergeStrategy(false, 0, 0, patch_size_min_bytes,
                         patch_size_max_bytes);
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
  static MergeStrategy CostBased(ProbabilityProfile&& profile,
                                 uint32_t network_overhead_cost = 75,
                                 uint32_t min_group_size = 4) {
    MergeStrategy strategy(true, network_overhead_cost, min_group_size, 0,
                           UINT32_MAX);
    strategy.AddProbabilityProfile(std::move(profile));
    return strategy;
  }

  absl::Span<const ProbabilityProfile> ProbabilityProfiles() const {
    return probability_profiles_;
  }

  absl::StatusOr<const ProbabilityProfile*> GetProbabilityProfile(
      size_t profile_index) const {
    if (profile_index >= probability_profiles_.size()) {
      return absl::InvalidArgumentError("profile_index is out of bounds.");
    }
    return &probability_profiles_[profile_index];
  }

  void AddProbabilityProfile(ProbabilityProfile profile) {
    probability_profiles_.push_back(std::move(profile));
  }

  bool HasInitFontMerge() const {
    for (const auto& profile : probability_profiles_) {
      if (profile.init_font_merge_threshold.has_value()) {
        return true;
      }
    }
    return false;
  }

  bool IsNone() const { return !use_costs_ && patch_size_min_bytes_ == 0; }
  bool UseCosts() const { return use_costs_; }
  bool UsePatchMerges() const { return use_patch_merges_; }

  std::optional<absl::string_view> Name() const {
    if (name_.has_value()) {
      return name_;
    } else {
      return std::nullopt;
    }
  }

  void SetName(std::string name) { name_ = name; }

  uint32_t NetworkOverheadCost() const { return network_overhead_cost_; }
  uint32_t MinimumGroupSize() const { return min_group_size_; }
  uint32_t PatchSizeMinBytes() const { return patch_size_min_bytes_; }
  uint32_t PatchSizeMaxBytes() const { return patch_size_max_bytes_; }

  absl::StatusOr<const freq::ProbabilityCalculator*> ProbabilityCalculator(
      size_t profile_index) const {
    if (profile_index >= probability_profiles_.size()) {
      return absl::InvalidArgumentError("profile_index is out of bounds.");
    }
    const auto& profile = probability_profiles_[profile_index];
    if (!profile.calculator) {
      return absl::InvalidArgumentError(
          "Probability profile is missing calculator.");
    }
    return profile.calculator.get();
  }

  void SetMinimumGroupSize(uint32_t value) { min_group_size_ = value; }

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

  // For best case size reduction computations this sets the assumed smallest
  // possible reduction in data (post compression) added to a base patch.
  //
  // See the comment in segmenter_config.proto for more details.
  double BestCaseSizeReductionFraction() const {
    return best_case_size_reduction_fraction_;
  }

  void SetBestCaseSizeReductionFraction(double value) {
    best_case_size_reduction_fraction_ = std::max(0.0, std::min(1.0, value));
  }

  uint32_t PreClosureGroupSize() const { return pre_closure_group_size_; }

  double PreClosureProbabilityThreshold() const {
    return pre_closure_probability_threshold_;
  }

  void SetPreClosureGroupSize(uint32_t value) {
    pre_closure_group_size_ = value;
  }

  void SetPreClosureProbabilityThreshold(double value) {
    pre_closure_probability_threshold_ = value;
  }

  void SetUsePatchMerges(bool value) { use_patch_merges_ = value; }

  bool operator==(const MergeStrategy& other) const {
    return use_costs_ == other.use_costs_ &&
           network_overhead_cost_ == other.network_overhead_cost_ &&
           min_group_size_ == other.min_group_size_ &&
           patch_size_min_bytes_ == other.patch_size_min_bytes_ &&
           patch_size_max_bytes_ == other.patch_size_max_bytes_ &&
           optimization_cutoff_fraction_ ==
               other.optimization_cutoff_fraction_ &&
           best_case_size_reduction_fraction_ ==
               other.best_case_size_reduction_fraction_ &&
           use_patch_merges_ == other.use_patch_merges_ &&
           pre_closure_group_size_ == other.pre_closure_group_size_ &&
           pre_closure_probability_threshold_ ==
               other.pre_closure_probability_threshold_ &&
           probability_profiles_ == other.probability_profiles_;
  }

 private:
  MergeStrategy(bool use_costs, uint32_t network_overhead_cost,
                uint32_t min_group_size, uint32_t patch_size_min_bytes,
                uint32_t patch_size_max_bytes)
      : use_costs_(use_costs),
        network_overhead_cost_(network_overhead_cost),
        min_group_size_(min_group_size),
        patch_size_min_bytes_(patch_size_min_bytes),
        patch_size_max_bytes_(patch_size_max_bytes) {}

  std::optional<std::string> name_ = std::nullopt;
  bool use_costs_;
  uint32_t network_overhead_cost_;
  uint32_t min_group_size_;
  uint32_t patch_size_min_bytes_;
  uint32_t patch_size_max_bytes_;
  double optimization_cutoff_fraction_ = 0.001;
  double best_case_size_reduction_fraction_ = 0.5;
  bool use_patch_merges_ = false;

  uint32_t pre_closure_group_size_ = 1;
  double pre_closure_probability_threshold_ = 1.0;

  std::vector<ProbabilityProfile> probability_profiles_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_MERGE_STRATEGY_
