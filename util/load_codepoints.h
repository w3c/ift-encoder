#ifndef UTIL_LOAD_CODEPOINTS_H_
#define UTIL_LOAD_CODEPOINTS_H_

#include <optional>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/status/statusor.h"
#include "common/font_data.h"
#include "common/font_helper.h"
#include "common/int_set.h"
#include "ift/freq/unicode_frequencies.h"

namespace util {

template <typename T>
common::IntSet Values(const T& proto_set) {
  common::IntSet result;
  for (uint32_t v : proto_set.values()) {
    result.insert(v);
  }
  return result;
}

template <typename T>
absl::btree_set<hb_tag_t> TagValues(const T& proto_set) {
  absl::btree_set<hb_tag_t> result;
  for (const auto& tag : proto_set.values()) {
    result.insert(common::FontHelper::ToTag(tag));
  }
  return result;
}

// Loads the file at path and returns it's binary contents.
absl::StatusOr<common::FontData> LoadFile(const char* path);

// Loads a Riegeli file of CodepointCount protos and returns a
// UnicodeFrequencies instance.
//
// Append "@*" to the path to load all sharded files for this path.
// For example "FrequencyData.riegeli@*" will load all files of the
// form FrequencyData.riegeli-*-of-* into the frequency data set.
absl::StatusOr<ift::freq::UnicodeFrequencies> LoadFrequenciesFromRiegeli(
    const char* path);

// Loads frequency data from https://github.com/w3c/ift-encoder-data
//
// name is the file name to load.
// Append "@*" to the name to load all sharded files for a name.
absl::StatusOr<ift::freq::UnicodeFrequencies> LoadBuiltInFrequencies(
    const char* name);

// Given a filepath if it ends with @* this will expand the path into
// the list of paths matching the pattern: <path>-?????-of-?????
// Otherwise returns just the input path.
//
// Checks that the input path exists and will return a NotFoundError if
// it does not.
absl::StatusOr<std::vector<std::string>> ExpandShardedPath(const char* path);

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
