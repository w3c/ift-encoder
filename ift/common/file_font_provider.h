#ifndef COMMON_FILE_FONT_PROVIDER_H_
#define COMMON_FILE_FONT_PROVIDER_H_

#include <string>

#include "ift/common/font_provider.h"

namespace ift::common {

// Provides fonts by loading them from a directory on the file system.
class FileFontProvider : public FontProvider {
 public:
  explicit FileFontProvider(const std::string& base_directory)
      : base_directory_(base_directory) {}

  absl::Status GetFont(const std::string& id, FontData* out) const override;

 private:
  std::string base_directory_;
};

}  // namespace ift::common

#endif  // COMMON_FILE_FONT_PROVIDER_H_
