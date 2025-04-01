#ifndef IFT_PROTO_FORMAT_2_PATCH_MAP_H_
#define IFT_PROTO_FORMAT_2_PATCH_MAP_H_

#include <optional>

#include "absl/status/statusor.h"
#include "ift/proto/ift_table.h"

namespace ift::proto {

class Format2PatchMap {
 public:
  static absl::StatusOr<std::string> Serialize(
      const IFTTable& ift_table, std::optional<uint32_t> cff_charstrings_offset,
      std::optional<uint32_t> cff2_charstrings_offset);
};

}  // namespace ift::proto

#endif  // IFT_PROTO_FORMAT_2_PATCH_MAP_H_
