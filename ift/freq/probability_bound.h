#ifndef IFT_FREQ_PROBABILITY_BOUND_H_
#define IFT_FREQ_PROBABILITY_BOUND_H_

#include <ostream>

#include "absl/strings/str_cat.h"

namespace ift::freq {

class BigramProbabilityCalculator;

struct ProbabilityBound {
  static ProbabilityBound Zero() { return ProbabilityBound{0.0, 0.0}; }

  // TODO(garretrieger): XXXXX automatically clamp the min, max values?
  ProbabilityBound(double min, double max) : min_(min), max_(max) {}

  double Min() const { return min_; }
  double Max() const { return max_; }

  bool operator==(const ProbabilityBound& other) const {
    return min_ == other.min_ && max_ == other.max_;
  }

  std::string ToString() const {
    return absl::StrCat("[", min_, ", ", max_, "]");
  }

  friend void PrintTo(const ProbabilityBound& point, std::ostream* os);
  friend class BigramProbabilityCalculator;

 private:
  double min_;
  double max_;
};

}  // namespace ift::freq

#endif  // IFT_FREQ_PROBABILITY_BOUND_H_
