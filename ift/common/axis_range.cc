#include "ift/common/axis_range.h"

namespace ift::common {

void PrintTo(const AxisRange& range, std::ostream* os) {
  *os << "[" << range.start() << ", " << range.end() << "]";
}

}  // namespace ift::common