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
        << std::endl;

    if (strategy.init_font_merge_threshold_.has_value()) {
      *os << "  init_font_merge_threshold = "
          << *strategy.init_font_merge_threshold_ << std::endl;
    }
    *os << "}" << std::endl;
  }
}

}  // namespace ift::encoder