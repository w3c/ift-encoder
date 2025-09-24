#include "common/woff2.h"

#include "woff2/decode.h"
#include "woff2/encode.h"
#include "woff2/output.h"

using absl::StatusOr;
using absl::string_view;
using woff2::ComputeWOFF2FinalSize;
using woff2::ConvertTTFToWOFF2;
using woff2::ConvertWOFF2ToTTF;
using woff2::MaxWOFF2CompressedSize;
using woff2::WOFF2Params;
using woff2::WOFF2StringOut;

namespace common {

StatusOr<FontData> Woff2::EncodeWoff2(string_view font, bool glyf_transform,
                                      int quality) {
  WOFF2Params params;
  params.brotli_quality = quality;
  params.allow_transforms = glyf_transform;
  size_t buffer_size =
      MaxWOFF2CompressedSize((const uint8_t*)font.data(), font.size());
  uint8_t* buffer = (uint8_t*)malloc(buffer_size);
  if (!ConvertTTFToWOFF2((const uint8_t*)font.data(), font.size(), buffer,
                         &buffer_size, params)) {
    free(buffer);
    return absl::InternalError("WOFF2 encoding failed.");
  }

  hb_blob_t* blob = hb_blob_create((const char*)buffer, buffer_size,
                                   HB_MEMORY_MODE_READONLY, buffer, free);
  FontData result(blob);
  hb_blob_destroy(blob);
  return result;
}

StatusOr<FontData> Woff2::DecodeWoff2(string_view font) {
  size_t buffer_size =
      ComputeWOFF2FinalSize((const uint8_t*)font.data(), font.size());
  if (!buffer_size) {
    return absl::InternalError("Failed computing woff2 output size.");
  }

  std::string buffer;
  buffer.resize(buffer_size);
  WOFF2StringOut out(&buffer);

  if (!ConvertWOFF2ToTTF((const uint8_t*)font.data(), font.size(), &out)) {
    return absl::InternalError("WOFF2 decoding failed.");
  }

  FontData result(buffer);
  return result;
}

}  // namespace common