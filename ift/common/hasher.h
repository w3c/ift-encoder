#ifndef COMMON_HASHER_H_
#define COMMON_HASHER_H_

#include <cstdint>

#include "absl/strings/string_view.h"

namespace ift::common {

// Computes checksums of binary data.
class Hasher {
 public:
  virtual ~Hasher() = default;

  // Compute checksum of the provided data.
  virtual uint64_t Checksum(::absl::string_view data) const = 0;
};

}  // namespace ift::common

#endif  // COMMON_HASHER_H_
