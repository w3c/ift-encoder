#ifndef PATCH_SUBSET_BINARY_PATCH_H_
#define PATCH_SUBSET_BINARY_PATCH_H_

#include "absl/status/status.h"
#include "patch_subset/font_data.h"

namespace patch_subset {

// Interface to an object which applies a binary patch
// to a binary blob.
class BinaryPatch {
 public:
  virtual ~BinaryPatch() = default;

  // Apply a patch to font_base and write the results to font_derived.
  virtual absl::Status Patch(const FontData& font_base, const FontData& patch,
                             FontData* font_derived) const = 0;

  // Apply a set of independent patches to font_base and write the results to
  // font_derived. will fail if the underlying patch is dependent.
  virtual absl::Status Patch(const FontData& font_base,
                             const std::vector<FontData>& patch,
                             FontData* font_derived) const = 0;
};

}  // namespace patch_subset

#endif  // PATCH_SUBSET_BINARY_PATCH_H_
