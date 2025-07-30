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

StatusOr<std::vector<CodepointAndFrequency>> LoadCodepointsOrdered(
    const char* path) {
  std::vector<CodepointAndFrequency> out;
  std::ifstream in(path);

  if (!in.is_open()) {
    return absl::NotFoundError(
        StrCat("Codepoints file ", path, " was not found."));
  }

  std::string line;
  while (std::getline(in, line)) {
    std::istringstream iss(line);
    std::string token;
    if (!(iss >> token)) {
      continue;
    }
    if (token[0] == '#') {
      continue;
    }

    std::string hex_code_str;
    std::optional<uint64_t> frequency;

    size_t comma_pos = token.find(',');
    if (comma_pos != std::string::npos) {
      hex_code_str = token.substr(0, comma_pos);
      std::string freq_str = token.substr(comma_pos + 1);
      if (!freq_str.empty()) {
        try {
          size_t consumed = 0;
          uint64_t f = std::stoull(freq_str, &consumed, 10);
          if (consumed != freq_str.length()) {
            return absl::InvalidArgumentError(
                "trailing unused text in the frequency number: " + freq_str);
          }
          frequency = f;
        } catch (const std::out_of_range& oor) {
          return absl::InvalidArgumentError(
              StrCat("Error converting frequency '", freq_str,
                     "' to integer: ", oor.what()));
        } catch (const std::invalid_argument& ia) {
          return absl::InvalidArgumentError(StrCat(
              "Invalid argument for frequency '", freq_str, "': ", ia.what()));
        }
      }
    } else {
      hex_code_str = token;
    }

    if (hex_code_str.substr(0, 2) != "0x") {
      return absl::InvalidArgumentError("Invalid hex code format: " +
                                        hex_code_str);
    }

    uint32_t cp;
    try {
      size_t consumed = 0;
      cp = std::stoul(hex_code_str.substr(2), &consumed, 16);
      if (consumed != hex_code_str.length() - 2) {
        return absl::InvalidArgumentError(
            "trailing unused text in the hex number: " + hex_code_str);
      }
    } catch (const std::out_of_range& oor) {
      return absl::InvalidArgumentError(StrCat("Error converting hex code '",
                                               hex_code_str,
                                               "' to integer: ", oor.what()));
    } catch (const std::invalid_argument& ia) {
      return absl::InvalidArgumentError(StrCat(
          "Invalid argument for hex code '", hex_code_str, "': ", ia.what()));
    }
    out.push_back({cp, frequency});
  }

  in.close();
  return out;
}

}  // namespace util