#ifndef COMMON_FONT_PROVIDER_H_
#define COMMON_FONT_PROVIDER_H_

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "ift/common/font_data.h"

namespace ift::common {

struct Woff2 {
  static absl::StatusOr<FontData> EncodeWoff2(absl::string_view font,
                                              bool glyf_transform = true,
                                              int quality = 11);
  static absl::StatusOr<FontData> DecodeWoff2(absl::string_view font);
};

}  // namespace ift::common

#endif  // COMMON_FONT_PROVIDER_H_