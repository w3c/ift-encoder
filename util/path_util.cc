#include "util/path_util.h"

#include <filesystem>
#include <string>
#include <system_error>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

using absl::StatusOr;

namespace ift::util {

StatusOr<std::string> JoinAndValidatePath(absl::string_view base_dir,
                                          absl::string_view sub_path) {
  if (base_dir.empty()) {
    return absl::InvalidArgumentError("base_dir must not be empty");
  }

  std::filesystem::path base(base_dir);
  std::filesystem::path target = base / sub_path;

  std::error_code ec_base, ec_target;
  std::filesystem::path base_canon =
      std::filesystem::weakly_canonical(base, ec_base);
  std::filesystem::path target_canon =
      std::filesystem::weakly_canonical(target, ec_target);

  if (ec_base || ec_target) {
    return absl::InvalidArgumentError(
        absl::StrCat("Path resolution failed: base_dir=", base_dir,
                     ", sub_path=", sub_path));
  }

  auto it_base = base_canon.begin();
  auto it_target = target_canon.begin();

  for (; it_base != base_canon.end(); ++it_base, ++it_target) {
    if (it_target == target_canon.end() || *it_base != *it_target) {
      return absl::PermissionDeniedError(
          absl::StrCat("Path traversal attempt detected: target '", sub_path,
                       "' escapes base directory '", base_dir, "'"));
    }
  }

  return target_canon.string();
}

}  // namespace ift::util
