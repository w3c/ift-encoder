#ifndef COMMON_BINARY_DIFF_H_
#define COMMON_BINARY_DIFF_H_

#include "absl/status/status.h"
#include "ift/common/font_data.h"

namespace ift::common {

// Interface to an object which computes a binary diff between
// two binary blobs.
class BinaryDiff {
 public:
  virtual ~BinaryDiff() = default;

  // Compute a patch which can be applied to binary a to transform
  // it into binary b.
  virtual absl::Status Diff(const FontData& font_base,
                            const FontData& font_derived,
                            FontData* patch /* OUT */) const = 0;
};

}  // namespace ift::common

#endif  // COMMON_BINARY_DIFF_H_
