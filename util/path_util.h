#ifndef UTIL_PATH_UTIL_H_
#define UTIL_PATH_UTIL_H_

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace ift::util {

// Joins base_dir and sub_path and validates that the resulting canonical path
// resides strictly within base_dir. Returns PermissionDeniedError if sub_path
// attempts path traversal outside of base_dir.
absl::StatusOr<std::string> JoinAndValidatePath(absl::string_view base_dir,
                                                absl::string_view sub_path);

}  // namespace ift::util

#endif  // UTIL_PATH_UTIL_H_
