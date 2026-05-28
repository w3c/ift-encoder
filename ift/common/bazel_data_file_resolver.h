#ifndef IFT_COMMON_BAZEL_DATA_FILE_RESOLVER_H_
#define IFT_COMMON_BAZEL_DATA_FILE_RESOLVER_H_

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "ift/common/data_file_resolver.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace ift::common {

class BazelDataFileResolver : public DataFileResolver {
 public:
  static absl::StatusOr<std::shared_ptr<DataFileResolver>> Create(
      const std::string& argv0);

  static absl::StatusOr<std::shared_ptr<DataFileResolver>> CreateForTest();

  absl::StatusOr<std::string> GetUnicodeDataPath() const override;
  absl::StatusOr<std::string> GetDerivedNormalizationPropsPath() const override;
  absl::StatusOr<std::string> GetFrequencyDataDirectory() const override;

 private:
  BazelDataFileResolver(
      std::unique_ptr<bazel::tools::cpp::runfiles::Runfiles> runfiles);

  std::unique_ptr<bazel::tools::cpp::runfiles::Runfiles> runfiles_;
};

}  // namespace ift::common

#endif  // IFT_COMMON_BAZEL_DATA_FILE_RESOLVER_H_
