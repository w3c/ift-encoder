#ifndef IFT_ENCODER_MERGE_STRATEGY_
#define IFT_ENCODER_MERGE_STRATEGY_

#include <algorithm>
#include <cstdint>

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
    return MergeStrategy(false, 0, patch_size_min_bytes, patch_size_max_bytes);
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
  static MergeStrategy CostBased(uint32_t network_overhead_cost = 75) {
    return MergeStrategy(true, network_overhead_cost, 0, UINT32_MAX);
  }

  bool IsNone() const { return !use_costs_ && patch_size_min_bytes_ == 0; }
  bool UseCosts() const { return use_costs_; }
  uint32_t NetworkOverheadCost() const { return network_overhead_cost_; }
  uint32_t PatchSizeMinBytes() const { return patch_size_min_bytes_; }
  uint32_t PatchSizeMaxBytes() const { return patch_size_max_bytes_; }

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
                uint32_t patch_size_min_bytes, uint32_t patch_size_max_bytes)
      : use_costs_(use_costs),
        network_overhead_cost_(network_overhead_cost),
        patch_size_min_bytes_(patch_size_min_bytes),
        patch_size_max_bytes_(patch_size_max_bytes) {}

  bool use_costs_;
  uint32_t network_overhead_cost_;
  uint32_t patch_size_min_bytes_;
  uint32_t patch_size_max_bytes_;
  uint32_t brotli_quality_ = 8;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_MERGE_STRATEGY_