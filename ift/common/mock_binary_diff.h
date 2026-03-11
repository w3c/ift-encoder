#ifndef COMMON_MOCK_BINARY_DIFF_H_
#define COMMON_MOCK_BINARY_DIFF_H_

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "ift/common/binary_diff.h"

namespace ift::common {

class MockBinaryDiff : public BinaryDiff {
 public:
  MOCK_METHOD(absl::Status, Diff,
              (const FontData& font_base, const FontData& font_derived,
               FontData* patch /* OUT */),
              (const override));
};

}  // namespace ift::common

#endif  // COMMON_MOCK_BINARY_DIFF_H_
