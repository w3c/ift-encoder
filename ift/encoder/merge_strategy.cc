#include "ift/encoder/merge_strategy.h"

#include <ostream>

namespace ift::encoder {

void PrintTo(const MergeStrategy& strategy, std::ostream* os) {
  if (strategy.UseCosts()) {
    *os << "CostBased {" << std::endl
        << "  network_overhead = " << strategy.NetworkOverheadCost()
        << std::endl
        << "  min_group_size = " << strategy.MinimumGroupSize() << std::endl
        << "  optimization_cutoff = " << strategy.OptimizationCutoffFraction()
        << std::endl
        << "  best_case_size_reduction_fraction = "
        << strategy.BestCaseSizeReductionFraction() << std::endl;

    for (size_t i = 0; i < strategy.ProbabilityProfiles().size(); ++i) {
      const auto& profile = strategy.ProbabilityProfiles()[i];
      *os << "  profile[" << i << "] {" << std::endl;
      if (profile.init_font_merge_threshold.has_value()) {
        *os << "    init_font_merge_threshold = "
            << *profile.init_font_merge_threshold << std::endl;
      }
      if (profile.init_font_merge_probability_threshold.has_value()) {
        *os << "    init_font_merge_probability_threshold = "
            << *profile.init_font_merge_probability_threshold << std::endl;
      }
      *os << "  }" << std::endl;
    }
    *os << "  use_patch_merges = " << strategy.UsePatchMerges() << std::endl
        << "  pre_closure_group_size = " << strategy.PreClosureGroupSize()
        << std::endl
        << "  pre_closure_probability_threshold = "
        << strategy.PreClosureProbabilityThreshold() << std::endl;
    *os << "}" << std::endl;
  }
}

}  // namespace ift::encoder