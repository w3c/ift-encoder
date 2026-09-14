#include "ift/common/bazel_data_file_resolver.h"

#include <filesystem>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace ift::common {

using absl::StatusOr;
using bazel::tools::cpp::runfiles::Runfiles;

StatusOr<std::shared_ptr<DataFileResolver>> BazelDataFileResolver::Create(
    const std::string& argv0) {
  std::string error;
  auto runfiles = std::unique_ptr<Runfiles>(Runfiles::Create(argv0, &error));
  if (!runfiles) {
    return absl::InternalError(
        absl::StrCat("Failed to create runfiles: ", error));
  }
  return std::shared_ptr<BazelDataFileResolver>(
      new BazelDataFileResolver(std::move(runfiles)));
}

StatusOr<std::shared_ptr<DataFileResolver>>
BazelDataFileResolver::CreateForTest() {
  std::string error;
  auto runfiles = std::unique_ptr<Runfiles>(Runfiles::CreateForTest(&error));
  if (!runfiles) {
    return absl::InternalError(
        absl::StrCat("Failed to create runfiles for test: ", error));
  }
  return std::shared_ptr<BazelDataFileResolver>(
      new BazelDataFileResolver(std::move(runfiles)));
}

BazelDataFileResolver::BazelDataFileResolver(std::unique_ptr<Runfiles> runfiles)
    : runfiles_(std::move(runfiles)) {}

StatusOr<std::string> BazelDataFileResolver::GetUnicodeDataPath() const {
  std::string path = runfiles_->Rlocation(UNICODE_DATA_PATH);
  if (path.empty() || !std::filesystem::exists(path)) {
    return absl::NotFoundError(
        absl::StrCat("Failed to find UnicodeData.txt via runfiles: ", path));
  }
  return path;
}

StatusOr<std::string> BazelDataFileResolver::GetDerivedNormalizationPropsPath()
    const {
  std::string path = runfiles_->Rlocation(DERIVED_PROPS_PATH);
  if (path.empty() || !std::filesystem::exists(path)) {
    return absl::NotFoundError(absl::StrCat(
        "Failed to find DerivedNormalizationProps.txt via runfiles: ", path));
  }
  return path;
}

StatusOr<std::string> BazelDataFileResolver::GetFrequencyDataDirectory() const {
  std::string metadata_path = runfiles_->Rlocation(FREQ_DATA_METADATA);
  if (metadata_path.empty() || !std::filesystem::exists(metadata_path)) {
    return absl::NotFoundError(
        "Failed to find frequency data directory via runfiles");
  }
  return std::filesystem::path(metadata_path).parent_path().string();
}

StatusOr<std::string> BazelDataFileResolver::GetUnigramFrequencyDataDirectory()
    const {
  // There's no metadata file for the unigram data, so locate the directory
  // via one of the data files it contains.
  std::string data_file_path = runfiles_->Rlocation(UNIGRAM_FREQ_DATA_FILE);
  if (data_file_path.empty() || !std::filesystem::exists(data_file_path)) {
    return absl::NotFoundError(
        "Failed to find unigram frequency data directory via runfiles");
  }
  return std::filesystem::path(data_file_path).parent_path().string();
}

}  // namespace ift::common
