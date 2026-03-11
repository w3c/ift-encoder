#ifndef COMMON_BROTLI_BINARY_PATCH_H_
#define COMMON_BROTLI_BINARY_PATCH_H_

#include "absl/status/status.h"
#include "ift/common/binary_patch.h"
#include "ift/common/font_data.h"

namespace ift::common {

// Applies a patch that was created using brotli compression
// with a shared dictionary.
class BrotliBinaryPatch : public BinaryPatch {
 public:
  absl::Status Patch(const FontData& font_base, const FontData& patch,
                     FontData* font_derived /* OUT */) const override;

  absl::Status Patch(const FontData& font_base,
                     const std::vector<FontData>& patch,
                     FontData* font_derived) const override;
};

}  // namespace ift::common

#endif  // COMMON_BROTLI_BINARY_PATCH_H_
