#include "ift/freq/unicode_frequencies.h"

#include <algorithm>
#include <utility>

namespace ift::freq {

void UnicodeFrequencies::Add(uint32_t cp1, uint32_t cp2, uint64_t count) {
  if (cp1 > cp2) {
    std::swap(cp1, cp2);
  }
  uint64_t& freq = frequencies_[{cp1, cp2}];
  freq += count;
  if (freq > max_count_) {
    max_count_ = freq;
  }
}

double UnicodeFrequencies::ProbabilityFor(uint32_t cp) const {
  return ProbabilityFor(cp, cp);
}

double UnicodeFrequencies::ProbabilityFor(uint32_t cp1, uint32_t cp2) const {
  if (max_count_ == 0) {
    return 0.0;
  }
  if (cp1 > cp2) {
    std::swap(cp1, cp2);
  }
  auto it = frequencies_.find({cp1, cp2});
  if (it == frequencies_.end()) {
    return 1.0 / max_count_;
  }
  return static_cast<double>(it->second) / max_count_;
}

}  // namespace ift::freq
