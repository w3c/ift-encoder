#include "ift/common/bazel_test_font_loader.h"

#include <fstream>
#include <memory>
#include <sstream>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "ift/common/try.h"

using bazel::tools::cpp::runfiles::Runfiles;

namespace ift::common {

absl::StatusOr<std::unique_ptr<TestFontLoader>> TestFontLoader::Default() {
  return BazelTestFontLoader::Create();
}

absl::StatusOr<std::unique_ptr<BazelTestFontLoader>>
BazelTestFontLoader::Create() {
  std::string error;
  std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&error));
  if (!runfiles) {
    return absl::InternalError(
        absl::StrCat("Failed to create Runfiles: ", error));
  }
  return std::unique_ptr<BazelTestFontLoader>(
      new BazelTestFontLoader(std::move(runfiles)));
}

BazelTestFontLoader::BazelTestFontLoader(std::unique_ptr<Runfiles> runfiles)
    : runfiles_(std::move(runfiles)) {}

absl::StatusOr<hb_face_unique_ptr> BazelTestFontLoader::LoadFace(
    const std::string& path) const {
  auto font_data = TRY(LoadFontData(path));
  return font_data.face();
}

absl::StatusOr<FontData> BazelTestFontLoader::LoadFontData(
    const std::string& path) const {
  std::string full_path = path;
  if (path.find("_main/") != 0) {
    full_path = absl::StrCat("_main/", path);
  }
  std::string rpath = runfiles_->Rlocation(full_path);
  if (rpath.empty()) {
    return absl::NotFoundError(
        absl::StrCat("File not found in runfiles: ", full_path));
  }

  std::ifstream file(rpath, std::ios::binary);
  if (!file.is_open()) {
    return absl::InternalError(absl::StrCat("Failed to open file: ", rpath));
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  return FontData(buffer.str());
}

}  // namespace ift::common
