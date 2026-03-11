#ifndef COMMON_COMPAT_ID_H_
#define COMMON_COMPAT_ID_H_

#include <cstdint>
#include <iostream>

#include "ift/common/font_helper.h"

namespace ift::common {

/*
 * Represents an IFT compatibility id.
 */
class CompatId {
 public:
  friend void PrintTo(const CompatId& id, std::ostream* os);

  CompatId() : CompatId(0, 0, 0, 0) {}

  CompatId(uint32_t values[4])
      : value_{values[0], values[1], values[2], values[3]} {}

  CompatId(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
      : value_{a, b, c, d} {}

  const uint32_t* as_ptr() const {
    return reinterpret_cast<const uint32_t*>(value_);
  }

  uint32_t* as_ptr() { return reinterpret_cast<uint32_t*>(value_); }

  void WriteTo(std::string& out) const {
    FontHelper::WriteUInt32(value_[0], out);
    FontHelper::WriteUInt32(value_[1], out);
    FontHelper::WriteUInt32(value_[2], out);
    FontHelper::WriteUInt32(value_[3], out);
  }

  bool operator==(const CompatId& other) const {
    return value_[0] == other.value_[0] && value_[1] == other.value_[1] &&
           value_[2] == other.value_[2] && value_[3] == other.value_[3];
  }

 private:
  uint32_t value_[4];
};

}  // namespace ift::common

#endif  // COMMON_COMPAT_ID_H_