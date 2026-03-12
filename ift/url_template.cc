#include "ift/url_template.h"

#include <cstdint>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "base32_hex.hpp"
#include "base64_url.hpp"

using absl::Span;
using absl::StatusOr;
using cppcodec::base32_hex;
using cppcodec::base64_url;

namespace ift {

constexpr uint8_t OPCODES_START = 128;
constexpr uint8_t ID32 = 0;
constexpr uint8_t D1 = 1;
constexpr uint8_t D2 = 2;
constexpr uint8_t D3 = 3;
constexpr uint8_t D4 = 4;
constexpr uint8_t ID64 = 5;
constexpr uint8_t OPCODE_COUNT = 6;
constexpr uint8_t OPCODES_END = OPCODES_START + OPCODE_COUNT - 1;

static std::string Id64Expansion(const uint8_t* bytes, uint32_t length) {
  std::string base64 = base64_url::encode(bytes, length);
  std::string base64_escaped;
  base64_escaped.reserve(base64.size() +
                         4 /* room for possible padding escapes */);
  for (int i = 0; i < base64.size(); i++) {
    if (base64[i] != '=') {
      base64_escaped.insert(base64_escaped.end(), base64[i]);
    } else {
      base64_escaped.append("%3D");
    }
  }
  return base64_escaped;
}

void PopulateExpansions(uint32_t patch_idx,
                        std::string expansions[OPCODE_COUNT]) {
  uint8_t bytes[4];
  bytes[0] = (patch_idx >> 24) & 0x000000FFu;
  bytes[1] = (patch_idx >> 16) & 0x000000FFu;
  bytes[2] = (patch_idx >> 8) & 0x000000FFu;
  bytes[3] = patch_idx & 0x000000FFu;

  size_t start = 0;
  while (start < 3 && !bytes[start]) {
    start++;
  }

  std::string base32 = base32_hex::encode(bytes + start, 4 - start);
  base32.erase(std::find_if(base32.rbegin(), base32.rend(),
                            [](unsigned char ch) { return ch != '='; })
                   .base(),
               base32.end());

  expansions[ID32] = base32;
  if (base32.size() >= 1) {
    expansions[D1] = base32.substr(base32.size() - 1, 1);
  } else {
    expansions[D1] = "_";
  }

  if (base32.size() >= 2) {
    expansions[D2] = base32.substr(base32.size() - 2, 1);
  } else {
    expansions[D2] = "_";
  }

  if (base32.size() >= 3) {
    expansions[D3] = base32.substr(base32.size() - 3, 1);
  } else {
    expansions[D3] = "_";
  }

  if (base32.size() >= 4) {
    expansions[D4] = base32.substr(base32.size() - 4, 1);
  } else {
    expansions[D4] = "_";
  }

  expansions[ID64] = Id64Expansion(bytes + start, 4 - start);
}

StatusOr<std::string> URLTemplate::PatchToUrl(Span<const uint8_t> url_template,
                                              uint32_t patch_idx) {
  std::string expansions[OPCODE_COUNT];
  PopulateExpansions(patch_idx, expansions);

  std::string out;
  uint32_t index = 0;
  while (index < url_template.size()) {
    uint8_t op_code = url_template[index++];
    if (!(op_code & 0b10000000)) {
      uint8_t num_literals = op_code & 0b01111111;
      if (index + num_literals > url_template.size()) {
        return absl::InvalidArgumentError(
            "Unexpected end of bytes expanding the url template.");
      }
      if (!num_literals) {
        return absl::InvalidArgumentError(
            absl::StrCat("invalid opcode: ", op_code));
      }
      out.insert(out.end(), url_template.begin() + index,
                 url_template.begin() + index + num_literals);
      index += num_literals;
    } else {
      if (op_code < OPCODES_START || op_code > OPCODES_END) {
        return absl::InvalidArgumentError(
            absl::StrCat("invalid opcode: ", op_code));
      }

      const auto& value = expansions[op_code - OPCODES_START];
      out.insert(out.end(), value.begin(), value.end());
    }
  }
  return out;
}

}  // namespace ift
