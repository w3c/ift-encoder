#include "ift/freq/unicode_frequencies.h"

#include <cstdint>
#include <utility>

namespace ift::freq {

UnicodeFrequencies::UnicodeFrequencies(
    std::initializer_list<std::pair<std::pair<uint32_t, uint32_t>, uint64_t>>
        frequencies) {
  for (const auto& pair : frequencies) {
    Add(pair.first.first, pair.first.second, pair.second);
  }
}

static uint64_t ToKey(uint32_t cp1, uint32_t cp2) {
  if (cp1 < cp2) {
    return (((uint64_t)cp1) << 32) | ((uint64_t)cp2);
  } else {
    return (((uint64_t)cp2) << 32) | ((uint64_t)cp1);
  }
}

void UnicodeFrequencies::Add(uint32_t cp1, uint32_t cp2, uint64_t count) {
  uint64_t key = ToKey(cp1, cp2);
  uint64_t& freq = frequencies_[key];
  freq += count;
  if (freq > max_count_) {
    max_count_ = freq;
    unknown_probability = 1.0 / (double)max_count_;
    for (auto it = frequencies_.begin(); it != frequencies_.end(); it++) {
      probabilities_[it->first] = (double)it->second / (double)max_count_;
    }
  } else {
    probabilities_[key] = (double)freq / (double)max_count_;
  }
}

double UnicodeFrequencies::ProbabilityFor(uint32_t cp) const {
  return ProbabilityFor(cp, cp);
}

double UnicodeFrequencies::ProbabilityFor(uint32_t cp1, uint32_t cp2) const {
  if (max_count_ == 0) {
    return 0.0;
  }
  auto it = probabilities_.find(ToKey(cp1, cp2));
  if (it != probabilities_.end()) {
    return it->second;
  }
  return unknown_probability;
}

}  // namespace ift::freq
