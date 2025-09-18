#ifndef IFT_FREQ_PROBABILITY_BOUND_H_
#define IFT_FREQ_PROBABILITY_BOUND_H_

#include <ostream>

#include "absl/strings/str_cat.h"

namespace ift::freq {

class BigramProbabilityCalculator;

struct ProbabilityBound {
  static ProbabilityBound Zero() { return ProbabilityBound{0.0, 0.0}; }

  static ProbabilityBound BonferroniBound(double unigram_sum,
                                          double bigram_sum) {
    // See:
    // https://en.wikipedia.org/wiki/Boole%27s_inequality#Bonferroni_inequalities
    ProbabilityBound bound = Zero();
    // K = 2: P >= ..
    bound.min_ = std::max(std::min(unigram_sum - bigram_sum, 1.0), 0.0),
    // K = 1: P <= ...
        bound.max_ = std::max(std::min(unigram_sum, 1.0), 0.0);
    bound.unigram_sum_ = unigram_sum;
    bound.bigram_sum_ = bigram_sum;
    return bound;
  }

  // TODO(garretrieger): XXXXX automatically clamp the min, max values?
  ProbabilityBound(double min, double max)
      : min_(min), max_(max), unigram_sum_(0), bigram_sum_(0) {}

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

  double unigram_sum_;
  double bigram_sum_;
};

}  // namespace ift::freq

#endif  // IFT_FREQ_PROBABILITY_BOUND_H_
