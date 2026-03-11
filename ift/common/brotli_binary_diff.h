#ifndef COMMON_BROTLI_BINARY_DIFF_H_
#define COMMON_BROTLI_BINARY_DIFF_H_

#include <vector>

#include "absl/status/status.h"
#include "ift/common/binary_diff.h"
#include "ift/common/font_data.h"

namespace ift::common {

// Computes a binary diff using brotli compression
// with a shared dictionary.
class BrotliBinaryDiff : public BinaryDiff {
 public:
  BrotliBinaryDiff() : quality_(9) {}
  BrotliBinaryDiff(unsigned quality) : quality_(quality) {}

  absl::Status Diff(const FontData& font_base, const FontData& font_derived,
                    FontData* patch /* OUT */) const override;

  // For use in stitching together a brotli patch.
  absl::Status Diff(const FontData& font_base, ::absl::string_view data,
                    unsigned stream_offset, bool is_last,
                    std::vector<uint8_t>& sink) const;

 private:
  unsigned quality_;
};

}  // namespace ift::common

#endif  // COMMON_BROTLI_BINARY_DIFF_H_
