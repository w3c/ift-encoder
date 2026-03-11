#include "ift/common/bit_output_buffer.h"

#include <string>

#include "absl/strings/string_view.h"

using ::absl::string_view;
using std::string;

namespace ift::common {

static const unsigned int kBitsPerByte = 8;
static const unsigned int kBitsPerNibble = 4;
static const unsigned char kFirstPairMask = 0b00000011;
static const unsigned char kFirstNibbleMask = 0x0F;

static unsigned char EncodeFirstByte(BranchFactor branch_factor,
                                     unsigned int depth);

BitOutputBuffer::BitOutputBuffer(BranchFactor branch_factor, unsigned int depth)
    : branch_factor(branch_factor) {
  current_pair = 0;
  first_nibble = true;
  buffer.push_back(EncodeFirstByte(branch_factor, depth));
}

void BitOutputBuffer::append(uint32_t bits) {
  switch (branch_factor) {
    case BF2:
      if (current_pair == 0) {
        buffer.push_back((unsigned char)bits & kFirstPairMask);
        current_pair++;
      } else {
        unsigned char two_bits = bits & kFirstPairMask;
        two_bits <<= (2 * current_pair);
        buffer[buffer.size() - 1] |= two_bits;
        current_pair++;
        if (current_pair == 4) {
          current_pair = 0;
        }
      }
      break;
    case BF4:
      if (first_nibble) {
        buffer.push_back((unsigned char)bits & kFirstNibbleMask);
        first_nibble = false;
      } else {
        buffer[buffer.size() - 1] =
            ((buffer[buffer.size() - 1] & kFirstNibbleMask) |
             (((unsigned char)bits & kFirstNibbleMask) << kBitsPerNibble));
        first_nibble = true;
      }
      break;
    case BF8:
      buffer.push_back((unsigned char)bits);
      break;
    case BF32:
      buffer.push_back((unsigned char)bits);
      bits >>= kBitsPerByte;
      buffer.push_back((unsigned char)bits);
      bits >>= kBitsPerByte;
      buffer.push_back((unsigned char)bits);
      bits >>= kBitsPerByte;
      buffer.push_back((unsigned char)bits);
      break;
  }
}

std::string BitOutputBuffer::to_string() {
  return string{buffer.begin(), buffer.end()};
}

static uint8_t EncodeFirstByte(BranchFactor branch_factor, unsigned int depth) {
  uint8_t result = 0;
  switch (branch_factor) {
    case BF2:
      result = 0b00;
      break;
    case BF4:
      result = 0b01;
      break;
    case BF8:
      result = 0b10;
      break;
    case BF32:
      result = 0b11;
      break;
  }
  result |= depth << 2;
  return result;
}

}  // namespace ift::common
