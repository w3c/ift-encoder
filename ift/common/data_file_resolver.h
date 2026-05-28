#ifndef IFT_COMMON_DATA_FILE_RESOLVER_H_
#define IFT_COMMON_DATA_FILE_RESOLVER_H_

#include <string>
#include "absl/status/statusor.h"

namespace ift::common {

class DataFileResolver {
 public:
  virtual ~DataFileResolver() = default;

  // Returns the path to UnicodeData.txt
  virtual absl::StatusOr<std::string> GetUnicodeDataPath() const = 0;

  // Returns the path to DerivedNormalizationProps.txt
  virtual absl::StatusOr<std::string> GetDerivedNormalizationPropsPath() const = 0;

  // Returns the path to the directory containing frequency data files.
  virtual absl::StatusOr<std::string> GetFrequencyDataDirectory() const = 0;
};

}  // namespace ift::common

#endif  // IFT_COMMON_DATA_FILE_RESOLVER_H_
