#ifndef COMMON_INDEXED_DATA_READER_H_
#define COMMON_INDEXED_DATA_READER_H_

#include <cstdint>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace common {

/*
 * Helper class to read indexed data from a font. Indexed data
 * is data that has been segmented into chunks which are listed
 * in an offset table (for example loca + glyf).
 */
template <typename offset_type, int offset_multiplier>
class IndexedDataReader {
 public:
  IndexedDataReader(absl::string_view offsets, absl::string_view data)
      : offsets_(offsets), data_(data) {}

  absl::StatusOr<absl::string_view> DataFor(uint32_t id) const {
    constexpr int width = sizeof(offset_type);

    uint32_t start_index = id * width;
    uint32_t end_index = (id + 1) * width;
    if (end_index + width > offsets_.size()) {
      return absl::NotFoundError(
          absl::StrCat("Entry ", id, " not found in offset table."));
    }

    uint32_t start_offset =
        ((uint32_t)ReadValue(start_index, offsets_)) * offset_multiplier;
    uint32_t end_offset =
        ((uint32_t)ReadValue(end_index, offsets_)) * offset_multiplier;
    if (end_offset < start_offset) {
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid index. end (", end_offset, ") < start (",
                       start_offset, "), width = ", width, "."));
    }

    if (end_offset > data_.size()) {
      return absl::InvalidArgumentError("Data offsets exceed data size.");
    }

    return data_.substr(start_offset, end_offset - start_offset);
  }

 private:
  offset_type ReadValue(uint32_t index, absl::string_view offsets) const {
    uint32_t value = 0;
    for (uint32_t i = 0; i < sizeof(offset_type); i++) {
      uint8_t v = offsets[index + i];
      value |= ((uint32_t)v) << (8 * (sizeof(offset_type) - i - 1));
    }
    return value;
  }

  absl::string_view offsets_;
  absl::string_view data_;
};

}  // namespace common

#endif  // COMMON_INDEXED_DATA_READER_H_