#include "load_codepoints.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>

#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"
#include "common/font_data.h"
#include "hb.h"
#include "ift/freq/unicode_frequencies.h"
#include "riegeli/bytes/fd_reader.h"
#include "riegeli/records/record_reader.h"
#include "util/unicode_count.pb.h"

using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using absl::string_view;
using common::FontData;
using common::hb_blob_unique_ptr;
using common::make_hb_blob;
using ift::freq::UnicodeFrequencies;

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

StatusOr<std::vector<std::string>> ExpandShardedPath(const char* path) {
  std::string full_path(path);

  if (!full_path.ends_with("@*")) {
    if (!std::filesystem::exists(full_path)) {
      return absl::NotFoundError(StrCat("Path does not exist: ", full_path));
    }
    return std::vector<std::string>{full_path};
  }

  std::filesystem::path file_path = full_path.substr(0, full_path.size() - 2);
  std::string base_name = file_path.filename();
  std::filesystem::path directory = file_path.parent_path();

  // Find the list of files matching the pattern:
  // <base name>-?????-of-?????
  std::regex file_pattern("^.*-[0-9]{5}-of-[0-9]{5}$");

  if (!std::filesystem::exists(directory) ||
      !std::filesystem::is_directory(directory)) {
    return absl::NotFoundError(StrCat(
        "Path does not exist or is not a directory: ", directory.string()));
  }

  // Collect into a set to ensure the output is sorted.
  absl::btree_set<std::string> files;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    std::string name = entry.path().filename();
    if (!name.starts_with(base_name)) {
      continue;
    }

    if (std::regex_match(name, file_pattern)) {
      files.insert(entry.path());
    }
  }

  if (files.empty()) {
    return absl::NotFoundError(
        StrCat("No files matched the shard pattern: ", full_path));
  }

  return std::vector<std::string>(files.begin(), files.end());
}

static Status LoadFrequenciesFromRiegeliIndividual(
    const char* path, UnicodeFrequencies& frequencies) {
  riegeli::RecordReader reader{riegeli::FdReader(path)};
  if (!reader.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to open file: ", path));
  }
  CodepointCount proto;
  while (reader.ReadRecord(proto)) {
    if (proto.codepoints_size() == 1) {
      frequencies.Add(proto.codepoints(0), proto.codepoints(0), proto.count());
    } else if (proto.codepoints_size() == 2) {
      frequencies.Add(proto.codepoints(0), proto.codepoints(1), proto.count());
    } else {
      return absl::InvalidArgumentError(
          "Data file has invalid format, does not have exactly 1 or 2 "
          "codepoints per message.");
    }
  }
  if (!reader.Close()) {
    return absl::InternalError(reader.status().message());
  }
  return absl::OkStatus();
}

StatusOr<UnicodeFrequencies> LoadFrequenciesFromRiegeli(const char* path) {
  auto paths = TRY(ExpandShardedPath(path));
  UnicodeFrequencies frequencies;
  for (const auto& path : paths) {
    TRYV(LoadFrequenciesFromRiegeliIndividual(path.c_str(), frequencies));
  }
  return frequencies;
}

StatusOr<UnicodeFrequencies> LoadBuiltInFrequencies(const char* name) {
  std::string path = StrCat("../ift_encoder_data+/data/", name);
  return LoadFrequenciesFromRiegeli(path.c_str());
}

}  // namespace util