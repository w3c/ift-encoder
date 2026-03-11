#ifndef COMMON_MOCK_BINARY_PATCH_H_
#define COMMON_MOCK_BINARY_PATCH_H_

#include <string>

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "ift/common/binary_patch.h"

namespace ift::common {

class MockBinaryPatch : public BinaryPatch {
 public:
  MOCK_METHOD(absl::Status, Patch,
              (const FontData& font_base, const FontData& patch,
               FontData* derived /* OUT */),
              (const override));

  absl::Status Patch(const FontData& font_base,
                     const std::vector<FontData>& patch,
                     FontData* font_derived) const {
    // TODO(garretrieger): mock this properly.
    return absl::InvalidArgumentError("not implemented in mock yet.");
  }
};

class ApplyPatch {
 public:
  explicit ApplyPatch(::absl::string_view patched) : patched_(patched) {}

  absl::Status operator()(const FontData& font_base, const FontData& patch,
                          FontData* font_derived) {
    font_derived->copy(patched_);
    return absl::OkStatus();
  }

 private:
  ::absl::string_view patched_;
};

}  // namespace ift::common

#endif  // COMMON_MOCK_BINARY_PATCH_H_
