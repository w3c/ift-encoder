#include "ift/encoder/merge_strategy.h"

#include <ostream>

namespace ift::encoder {

void PrintTo(const MergeStrategy& strategy, std::ostream* os) {
  if (strategy.UseCosts()) {
    *os << "CostBased {" << std::endl
        << "  network_overhead = " << strategy.NetworkOverheadCost()
        << std::endl
        << "  min_group_size = " << strategy.MinimumGroupSize() << std::endl
        << "  optimization_cutoff = " << strategy.OptimizationCutoffFraction() << std::endl
        << "  best_case_size_reduction_fraction = " << strategy.BestCaseSizeReductionFraction() << std::endl;

    if (strategy.InitFontMergeThreshold().has_value()) {
      *os << "  init_font_merge_threshold = " << *strategy.InitFontMergeThreshold() << std::endl;
    }
    if (strategy.InitFontMergeProbabilityThreshold().has_value()) {
      *os << "  init_font_merge_probability_threshold = " << *strategy.InitFontMergeProbabilityThreshold() << std::endl;
    }
    *os << "  use_patch_merges = " << strategy.UsePatchMerges() << std::endl
        << "  pre_closure_group_size = " << strategy.PreClosureGroupSize() << std::endl
        << "  pre_closure_probability_threshold = " << strategy.PreClosureProbabilityThreshold() << std::endl;
    *os << std::endl;

    if (strategy.init_font_merge_threshold_.has_value()) {
      *os << "  init_font_merge_threshold = "
          << *strategy.init_font_merge_threshold_ << std::endl;
    }
    *os << "}" << std::endl;
  }
}

}  // namespace ift::encoder