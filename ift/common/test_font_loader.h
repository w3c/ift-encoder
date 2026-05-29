#ifndef IFT_COMMON_TEST_FONT_LOADER_H_
#define IFT_COMMON_TEST_FONT_LOADER_H_

#include <string>

#include "absl/status/statusor.h"
#include "ift/common/font_data.h"

namespace ift::common {

class TestFontLoader {
 public:
  static absl::StatusOr<std::unique_ptr<TestFontLoader>> Default();

  virtual ~TestFontLoader() = default;

  // Loads a font file from the given path (relative to project root) into a
  // hb_face_unique_ptr.
  virtual absl::StatusOr<hb_face_unique_ptr> LoadFace(
      const std::string& path) const = 0;

  // Loads a font file from the given path (relative to project root) into
  // FontData.
  virtual absl::StatusOr<FontData> LoadFontData(
      const std::string& path) const = 0;
};

}  // namespace ift::common

#endif  // IFT_COMMON_TEST_FONT_LOADER_H_
