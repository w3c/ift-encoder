#ifndef UTIL_LOAD_CODEPOINTS_H_
#define UTIL_LOAD_CODEPOINTS_H_

#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "common/font_data.h"

namespace util {

// Loads the file at path and returns it's binary contents.
absl::StatusOr<common::FontData> LoadFile(const char* path);

struct CodepointAndFrequency {
  uint32_t codepoint;
  std::optional<uint64_t> frequency;

  bool operator<(const CodepointAndFrequency& rhs) const {
    if (frequency == rhs.frequency) {
      return codepoint < rhs.codepoint;
    }

    if (frequency.has_value() && !rhs.frequency.has_value()) {
      return true;
    }

    if (!frequency.has_value() && rhs.frequency.has_value()) {
      return false;
    }

    // Sort from highest to lowest frequency.
    return *frequency > *rhs.frequency;
  }

  friend void PrintTo(const CodepointAndFrequency& point, std::ostream* os);
};

// Loads the codepoint file at path and returns it contents.
//
// - Retains the ordering and any duplicate codepoints listed in the original
// file.
// - A codepoint file has one codepoint per line in hexadecimal form 0xXXXX
// - An optional frequency can be provided as a second column, comma separated.
// - Lines starting with "#" are ignored.
absl::StatusOr<std::vector<CodepointAndFrequency>> LoadCodepointsOrdered(
    const char* path);

}  // namespace util

#endif  // UTIL_LOAD_CODEPOINTS_H_
