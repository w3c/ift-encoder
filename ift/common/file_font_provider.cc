#include "ift/common/file_font_provider.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "ift/common/font_data.h"

namespace ift::common {

using absl::Status;

Status FileFontProvider::GetFont(const std::string& id, FontData* out) const {
  std::string path = base_directory_ + id;
  hb_blob_t* blob = hb_blob_create_from_file(path.c_str());
  if (!hb_blob_get_length(blob)) {
    hb_blob_destroy(blob);
    return absl::NotFoundError(absl::StrCat(path, " does not exist."));
  }

  out->set(blob);
  hb_blob_destroy(blob);

  return absl::OkStatus();
}

}  // namespace ift::common
