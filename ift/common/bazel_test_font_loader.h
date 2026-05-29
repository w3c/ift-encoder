#ifndef IFT_COMMON_BAZEL_TEST_FONT_LOADER_H_
#define IFT_COMMON_BAZEL_TEST_FONT_LOADER_H_

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "ift/common/font_data.h"
#include "ift/common/test_font_loader.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace ift::common {

class BazelTestFontLoader : public TestFontLoader {
 public:
  static absl::StatusOr<std::unique_ptr<BazelTestFontLoader>> Create();

  absl::StatusOr<hb_face_unique_ptr> LoadFace(
      const std::string& path) const override;
  absl::StatusOr<FontData> LoadFontData(const std::string& path) const override;

 private:
  BazelTestFontLoader(
      std::unique_ptr<bazel::tools::cpp::runfiles::Runfiles> runfiles);

  std::unique_ptr<bazel::tools::cpp::runfiles::Runfiles> runfiles_;
};

}  // namespace ift::common

#endif  // IFT_COMMON_BAZEL_TEST_FONT_LOADER_H_
