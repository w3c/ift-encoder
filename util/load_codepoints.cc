#include "load_codepoints.h"

#include <fstream>
#include <iostream>
#include <sstream>

#include "absl/strings/str_cat.h"
#include "common/font_data.h"
#include "hb.h"

using absl::StatusOr;
using absl::StrCat;
using absl::string_view;
using common::FontData;
using common::hb_blob_unique_ptr;
using common::make_hb_blob;

namespace util {

StatusOr<common::FontData> LoadFile(const char* path) {
  hb_blob_unique_ptr blob =
      make_hb_blob(hb_blob_create_from_file_or_fail(path));
  if (!blob.get()) {
    return absl::NotFoundError(StrCat("File ", path, " was not found."));
  }
  return FontData(blob.get());
}

StatusOr<std::vector<uint32_t>> LoadCodepointsOrdered(const char* path) {
  std::vector<uint32_t> out;
  std::ifstream in(path);

  if (!in.is_open()) {
    return absl::NotFoundError(
        StrCat("Codepoints file ", path, " was not found."));
  }

  std::string line;
  while (std::getline(in, line)) {
    std::istringstream iss(line);
    std::string hex_code;
    std::string description;

    // Extract the hex code and description
    if (iss >> hex_code) {
      if (hex_code.empty() || hex_code.substr(0, 1) == "#") {
        // comment line, skip
        continue;
      } else if (hex_code.substr(0, 2) == "0x") {
        try {
          size_t consumed = 0;
          uint32_t cp = std::stoul(hex_code.substr(2), &consumed, 16);
          if (consumed + 2 < hex_code.length()) {
            return absl::InvalidArgumentError(
                "trailing unused text in the hex number.");
          }
          out.push_back(cp);
        } catch (const std::out_of_range& oor) {
          return absl::InvalidArgumentError(
              StrCat("Error converting hex code '", hex_code,
                     "' to integer: ", oor.what()));
        } catch (const std::invalid_argument& ia) {
          return absl::InvalidArgumentError(StrCat(
              "Invalid argument for hex code '", hex_code, "': ", ia.what()));
        }
      } else {
        return absl::InvalidArgumentError("Invalid hex code format: " +
                                          hex_code);
      }
    }
  }

  in.close();
  return out;
}

}  // namespace util