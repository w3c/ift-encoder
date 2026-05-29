#ifndef IFT_COMMON_FIXED_DATA_FILE_RESOLVER_H_
#define IFT_COMMON_FIXED_DATA_FILE_RESOLVER_H_

#include <string>

#include "absl/status/statusor.h"
#include "ift/common/data_file_resolver.h"

namespace ift::common {

class FixedDataFileResolver : public DataFileResolver {
 public:
  FixedDataFileResolver(std::string unicode_data_path,
                        std::string derived_props_path,
                        std::string frequency_data_dir)
      : unicode_data_path_(std::move(unicode_data_path)),
        derived_props_path_(std::move(derived_props_path)),
        frequency_data_dir_(std::move(frequency_data_dir)) {}

  absl::StatusOr<std::string> GetUnicodeDataPath() const override {
    return unicode_data_path_;
  }

  absl::StatusOr<std::string> GetDerivedNormalizationPropsPath()
      const override {
    return derived_props_path_;
  }

  absl::StatusOr<std::string> GetFrequencyDataDirectory() const override {
    return frequency_data_dir_;
  }

 private:
  std::string unicode_data_path_;
  std::string derived_props_path_;
  std::string frequency_data_dir_;
};

}  // namespace ift::common

#endif  // IFT_COMMON_FIXED_DATA_FILE_RESOLVER_H_
