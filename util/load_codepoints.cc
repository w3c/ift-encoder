#include "load_codepoints.h"

#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>

#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"
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

void PrintTo(const CodepointAndFrequency& cp_and_freq, std::ostream* os) {
  if (cp_and_freq.frequency.has_value()) {
    *os << "[" << cp_and_freq.codepoint << ", " << *cp_and_freq.frequency
        << "]";
  } else {
    *os << cp_and_freq.codepoint;
  }
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
    std::string trimmed_line(absl::StripAsciiWhitespace(line));

    if (trimmed_line.empty() || trimmed_line[0] == '#') {
      continue;
    }

    std::istringstream iss(trimmed_line);

    std::string hex_code_str;
    if (!std::getline(iss, hex_code_str, ',')) {
      continue;
    }

    std::string freq_str;
    std::optional<uint64_t> frequency = std::nullopt;
    if (std::getline(iss, freq_str, ',')) {
      size_t consumed = 0;
      try {
        frequency = std::stoull(freq_str, &consumed, 10);
        if (consumed < freq_str.length() && freq_str[consumed] != ' ') {
          return absl::InvalidArgumentError(
              "trailing unused text in the frequency.");
        }
      } catch (const std::out_of_range& oor) {
        return absl::InvalidArgumentError(StrCat("Error converting frequency '",
                                                 freq_str,
                                                 "' to integer: ", oor.what()));
      } catch (const std::invalid_argument& ia) {
        if (freq_str == " COMMA") {
          // name files sometimes have an entry like '0x002C  , COMMA' which
          // should not be confused with a frequency.
          frequency = std::nullopt;
        } else {
          return absl::InvalidArgumentError(StrCat(
              "Invalid argument for frequency '", freq_str, "': ", ia.what()));
        }
      }
    }

    if (hex_code_str.substr(0, 2) != "0x") {
      return absl::InvalidArgumentError("Invalid hex code format: " +
                                        hex_code_str);
    }

    uint32_t cp;
    try {
      size_t consumed = 0;
      cp = std::stoul(hex_code_str.substr(2), &consumed, 16);
      if (consumed + 2 < hex_code_str.length() &&
          hex_code_str[consumed + 2] != ' ') {
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