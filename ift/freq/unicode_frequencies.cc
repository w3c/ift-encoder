#include "ift/freq/unicode_frequencies.h"

#include <cstdint>
#include <utility>

#include "ift/common/int_set.h"

using ift::common::CodepointSet;

namespace ift::freq {

static uint64_t ToKey(uint32_t cp1, uint32_t cp2) {
  if (cp1 < cp2) {
    return (((uint64_t)cp1) << 32) | ((uint64_t)cp2);
  } else {
    return (((uint64_t)cp2) << 32) | ((uint64_t)cp1);
  }
}

void UnicodeFrequenciesBuilder::Add(uint32_t cp1, uint32_t cp2, uint64_t count) {
  uint64_t key = ToKey(cp1, cp2);
  uint64_t& freq = frequencies_[key];
  freq += count;
  if (freq > max_count_) {
    max_count_ = freq;
  }
}

UnicodeFrequencies UnicodeFrequenciesBuilder::Build() {
  UnicodeFrequencies result;
  result.max_count_ = max_count_;
  if (max_count_ > 0) {
    result.unknown_probability_ = 1.0 / (double)max_count_;
    for (const auto& [key, count] : frequencies_) {
      result.probabilities_[key] = (double)count / (double)max_count_;
    }
  }
  return result;
}

UnicodeFrequencies::UnicodeFrequencies(
    std::initializer_list<std::pair<std::pair<uint32_t, uint32_t>, uint64_t>>
        frequencies) {
  UnicodeFrequenciesBuilder builder;
  for (const auto& pair : frequencies) {
    builder.Add(pair.first.first, pair.first.second, pair.second);
  }
  *this = std::move(builder.Build());
}

double UnicodeFrequencies::ProbabilityFor(uint32_t cp) const {
  if (max_count_ == 0) {
    return 0.0;
  }
  auto it = probabilities_.find(ToKey(cp, cp));
  if (it != probabilities_.end()) {
    return it->second;
  }
  return unknown_probability_;
}

double UnicodeFrequencies::ProbabilityFor(uint32_t cp1, uint32_t cp2) const {
  return ProbabilityFor(cp1, cp2, ProbabilityFor(cp1), ProbabilityFor(cp2));
}

double UnicodeFrequencies::ProbabilityFor(uint32_t cp1, uint32_t cp2, double p1,
                                          double p2) const {
  if (max_count_ == 0) {
    return 0.0;
  }

  if (cp1 == cp2) {
    return p1;
  }

  auto it = probabilities_.find(ToKey(cp1, cp2));
  if (it != probabilities_.end()) {
    return it->second;
  }

  // Since we don't have data on P(cp1 n cp2), just assume the probabilities
  // for P(cp1) and P(cp2) are independent:
  return p1 * p2;
}

CodepointSet UnicodeFrequencies::CoveredCodepoints() const {
  CodepointSet out;
  for (const auto& [key, _] : probabilities_) {
    uint32_t cp1 = key >> 32;
    uint32_t cp2 = key & (uint64_t)0x00000000FFFFFFFF;
    if (cp1 == cp2) {
      out.insert(cp1);
    }
  }
  return out;
}

}  // namespace ift::freq