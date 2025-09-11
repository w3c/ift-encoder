#include "ift/freq/probability_bound.h"

#include <ostream>

namespace ift::freq {

void PrintTo(const ProbabilityBound& bound, std::ostream* os) {
  *os << "ProbabilityBound(min=" << bound.min_ << ", max=" << bound.max_ << ")";
}

}  // namespace ift::freq
