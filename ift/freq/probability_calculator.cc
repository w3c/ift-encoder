#include "ift/freq/probability_calculator.h"

namespace ift::freq {

void PrintTo(const ProbabilityBound& point, std::ostream* os) {
  *os << "[" << point.min << ", " << point.max << "]";
}

}  // namespace ift::freq