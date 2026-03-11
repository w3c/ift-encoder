#ifndef COMMON_BIT_INPUT_BUFFER_H_
#define COMMON_BIT_INPUT_BUFFER_H_

#include <vector>

#include "absl/strings/string_view.h"
#include "ift/common/branch_factor.h"

namespace ift::common {

/*
 * A class for reading from an encoded sparse bit set. Groups of 4, 8, 16 or 32
 * bits will be read at a time, depending on the branch factor encoded in the
 * first byte of the data.
 */
class BitInputBuffer {
 public:
  BitInputBuffer(absl::string_view bits);

  const BranchFactor GetBranchFactor() const;
  const unsigned int Depth() const;
  absl::string_view Remaining() const;

  // The lowest/rightmost bits of the value bits are set, the remaining are
  // cleared. The number of bits set depends on the BranchFactor this
  // BitInputBuffer was constructed with.
  bool read(uint32_t *out);

 private:
  const BranchFactor branch_factor;
  const uint32_t depth;
  absl::string_view bits;
  uint32_t current_byte;
  uint8_t current_pair;
  bool first_nibble;
};

}  // namespace ift::common

#endif  // COMMON_BIT_INPUT_BUFFER_H_
