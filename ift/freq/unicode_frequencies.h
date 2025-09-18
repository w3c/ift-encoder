#ifndef IFT_FREQ_UNICODE_FREQUENCIES_H_
#define IFT_FREQ_UNICODE_FREQUENCIES_H_

#include <cstdint>

#include "absl/container/flat_hash_map.h"
#include "hb.h"

namespace ift::freq {

class UnicodeFrequencies {
 public:
  UnicodeFrequencies() = default;
  UnicodeFrequencies(UnicodeFrequencies&& other) = default;
  UnicodeFrequencies(
      std::initializer_list<std::pair<std::pair<uint32_t, uint32_t>, uint64_t>>
          frequencies);

  UnicodeFrequencies(const UnicodeFrequencies&) = delete;
  UnicodeFrequencies& operator=(const UnicodeFrequencies&) = delete;

  bool HasData() const { return max_count_ > 0; }

  // Add frequency data for the codepoint pair (cp1, cp2).
  // When cp1 == cp2 this supplies frequency for a single codepoint.
  void Add(uint32_t cp1, uint32_t cp2, uint64_t count);

  // Returns the probability of codepoint cp occurring.
  double ProbabilityFor(uint32_t cp) const;

  // Returns the probability of codepoint pair (cp1, cp2) occurring.
  double ProbabilityFor(uint32_t cp1, uint32_t cp2) const;

  // Returns the probability of layout tag occurring.
  double ProbabilityForLayoutTag(hb_tag_t tag) const {
    // TODO(garretrieger): this is a temporary hack (just assumes all tags have
    // low probability) update this to actually hold and return real
    // frequency data.
    return 0.001;
  }

 private:
  absl::flat_hash_map<uint64_t, uint64_t> frequencies_;
  absl::flat_hash_map<uint64_t, double> probabilities_;
  uint64_t max_count_ = 0;
  double unknown_probability = 1.0;
};

}  // namespace ift::freq

#endif  // IFT_FREQ_UNICODE_FREQUENCIES_H_
