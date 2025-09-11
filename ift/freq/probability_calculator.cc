#include "ift/freq/probability_calculator.h"

namespace ift::freq {

void PrintTo(const ProbabilityBound& point, std::ostream* os) {
  *os << "[" << point.Min() << ", " << point.Max() << "]";
}

}  // namespace ift::freq