#include "ift/common/bit_input_buffer.h"

#include "absl/strings/string_view.h"

using absl::ClippedSubstr;
using absl::string_view;

namespace ift::common {

static const uint32_t kBitsPerByte = 8;
static const uint32_t kBitsPerTwoBytes = 16;
static const uint32_t kBitsPerThreeBytes = 24;
static const uint32_t kBitsPerNibble = 4;
static const uint8_t kFirstNibbleMask = 0x0F;
static const uint8_t kFirstTwoBitsMask = 0b11;

static BranchFactor DecodeBranchFactor(string_view bits);
static uint32_t DecodeDepth(string_view bits);

BitInputBuffer::BitInputBuffer(string_view bits)
    : branch_factor(DecodeBranchFactor(bits)), depth(DecodeDepth(bits)) {
  this->bits = bits;
  current_byte = 1;
  current_pair = 0;
  first_nibble = true;
}

const BranchFactor BitInputBuffer::GetBranchFactor() const {
  return branch_factor;
}

const uint32_t BitInputBuffer::Depth() const { return depth; }

absl::string_view BitInputBuffer::Remaining() const {
  int extra = 0;
  switch (branch_factor) {
    case BF2:
      extra = (current_pair > 0) ? 1 : 0;
      return ClippedSubstr(bits, current_byte + extra);
    case BF4:
      extra = !first_nibble ? 1 : 0;
      return ClippedSubstr(bits, current_byte + extra);
    case BF8:
      // Fallthrough
    case BF32:
      return ClippedSubstr(bits, current_byte);
    default:
      // Invalid branch factor, nothing will be consumed.
      return bits;
  }
}

bool BitInputBuffer::read(uint32_t *out) {
  if (!out) {
    return false;
  }
  switch (branch_factor) {
    case BF2:
      if (current_byte >= bits.size()) {
        return false;
      }
      *out = (bits[current_byte] >> (2 * current_pair)) & kFirstTwoBitsMask;
      current_pair++;
      if (current_pair == 4) {
        current_byte++;
        current_pair = 0;
      }
      break;
    case BF4:
      if (current_byte >= bits.size()) {
        return false;
      }
      if (first_nibble) {
        *out = (uint8_t)bits[current_byte] & kFirstNibbleMask;
        first_nibble = false;
      } else {
        *out = (uint8_t)bits[current_byte++] >> kBitsPerNibble;
        first_nibble = true;
      }
      break;
    case BF8:
      if (current_byte >= bits.size()) {
        return false;
      }
      *out = (uint8_t)bits[current_byte++];
      break;
    case BF32:
      if (current_byte + 3 >= bits.size()) {
        return false;
      }
      *out = ((uint8_t)bits[current_byte + 3] << kBitsPerThreeBytes) |
             ((uint8_t)bits[current_byte + 2] << kBitsPerTwoBytes) |
             ((uint8_t)bits[current_byte + 1] << kBitsPerByte) |
             (uint8_t)bits[current_byte];
      current_byte += 4;
      break;
  }
  return true;
}

static BranchFactor DecodeBranchFactor(string_view bits) {
  if (!bits.size()) {
    return BF2;
  }

  unsigned char first_byte = bits[0];
  switch (first_byte & 0b11) {
    case 0b00:
      return BF2;
    case 0b01:
      return BF4;
    case 0b10:
      return BF8;
    default:
      return BF32;
  }
}

static uint32_t DecodeDepth(string_view bits) {
  if (!bits.size()) {
    return 0;
  }

  // Only look at bits 2..6.
  // Bits 0 and 1 are branch factor. Bits 7 is reserved for future use.
  unsigned char first_byte = bits[0];
  return ((first_byte & 0b01111100u) >> 2);
}

}  // namespace ift::common
