#include "ift/common/font_data.h"

namespace ift::common {

void PrintTo(const FontData& data, std::ostream* os) {
  *os << "[";
  unsigned count = 0;
  for (uint8_t byte : data.str()) {
    *os << std::hex << std::uppercase << std::setfill('0') << std::setw(2)
        << +byte;
    if (++count < data.size()) {
      *os << " ";
    }
  }
  *os << "]" << std::endl;
}

}  // namespace ift::common