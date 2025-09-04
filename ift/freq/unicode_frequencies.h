#ifndef IFT_FREQ_UNICODE_FREQUENCIES_H_
#define IFT_FREQ_UNICODE_FREQUENCIES_H_

#include <cstdint>

#include "absl/container/flat_hash_map.h"

namespace ift::freq {

class UnicodeFrequencies {
 public:
  // Add frequency data for the codepoint pair (cp1, cp2).
  // When cp1 == cp2 this supplies frequency for a single codepoint.
  void Add(uint32_t cp1, uint32_t cp2, uint64_t count);

  // Returns the probability of codepoint cp occurring.
  double ProbabilityFor(uint32_t cp) const;

  // Returns the probability of codepoint pair (cp1, cp2) occurring.
  double ProbabilityFor(uint32_t cp1, uint32_t cp2) const;

 private:
  absl::flat_hash_map<std::pair<uint32_t, uint32_t>, uint64_t> frequencies_;
  uint64_t max_count_ = 0;
};

}  // namespace ift::freq

#endif  // IFT_FREQ_UNICODE_FREQUENCIES_H_
