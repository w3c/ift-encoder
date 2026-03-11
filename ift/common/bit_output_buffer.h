#ifndef COMMON_BIT_OUTPUT_BUFFER_H_
#define COMMON_BIT_OUTPUT_BUFFER_H_

#include <vector>

#include "absl/strings/string_view.h"
#include "ift/common/branch_factor.h"

namespace ift::common {

/*
 * A class to help write out an encoded sparse bit set. The first byte
 * encodes the branch factor and the depth. Based on the branch factor,
 * groups of 4, 8, 16 or 32 bits will be concatenated at a time.
 * The final result is returned as a std::string.
 */
class BitOutputBuffer {
 public:
  // Construct an initially empty BitOutputBuffer.
  BitOutputBuffer(BranchFactor branch_factor, unsigned int depth);

  // The lowest/rightmost bits of the value bits are appended. The number of
  // bits appended depends on the BranchFactor this BitOutputBuffer was
  // constructed with. For example with a BranchFactor of BF4, append(0b11100)
  // would append 1100, in the order 0, 0, 1, 1.
  void append(uint32_t bits);

  // Returns the bits as a string. The first bits written are in the first byte.
  // If there are not enough bits to fill out the last byte, 0s are added.
  std::string to_string();

 private:
  const BranchFactor branch_factor;
  std::vector<unsigned char> buffer;
  uint8_t current_pair;
  bool first_nibble;
};

}  // namespace ift::common

#endif  // COMMON_BIT_OUTPUT_BUFFER_H_
