#include "ift/common/compat_id.h"

namespace ift::common {

void PrintTo(const CompatId& id, std::ostream* os) {
  *os << "{" << id.value_[0] << ", " << id.value_[0] << ", " << id.value_[0]
      << ", " << id.value_[0] << "}";
}

}  // namespace ift::common