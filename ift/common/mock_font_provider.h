#ifndef COMMON_MOCK_FONT_PROVIDER_H_
#define COMMON_MOCK_FONT_PROVIDER_H_

#include <string>

#include "gtest/gtest.h"
#include "ift/common/font_provider.h"

namespace ift::common {

// Provides fonts by loading them from a directory on the file system.
class MockFontProvider : public FontProvider {
 public:
  MOCK_METHOD(absl::Status, GetFont, (const std::string& id, FontData* out),
              (const override));
};

}  // namespace ift::common

#endif  // COMMON_MOCK_FONT_PROVIDER_H_
