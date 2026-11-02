#include "util/auto_config_flags.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "hb.h"
#include "ift/common/data_file_resolver.h"
#include "ift/common/font_helper.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/config/auto_segmenter_config.h"
#include "ift/config/segmenter_config.pb.h"

ABSL_FLAG(
    int, auto_config_quality, 0,
    "The quality level to use when generating a segmenter config. A value of 0 "
    "means auto pick. Valid values are 1-8.");

ABSL_FLAG(
    std::string, auto_config_primary_script, "",
    "When auto_config is enabled this sets the primary script or "
    "language frequency data file the font is expected to be used with. "
    "This is used to signal the merger to prioritize performance with the "
    "specified script/language. Concretely: initial font merging will be "
    "done using the primary script/language frequency data. Additionally, "
    "when multiple scripts overlap (eg. like with CJK) merging will "
    "prioritize optimizing against the primary script/language instead of "
    "all overlapping scripts equally.");

ABSL_FLAG(
    bool, auto_config_unicode_range, false,
    "When auto_config is enabled, generate a segmenter config with target_mode "
    "set to UNICODE_RANGE instead of IFT.");

ABSL_FLAG(
    std::vector<std::string>, auto_config_additional_default_features, {},
    "Comma-separated list of OpenType feature tags (e.g. smcp,dlig) to add to "
    "the initial font in the produced segmenter config.");

ABSL_FLAG(
    std::vector<std::string>, auto_config_default_codepoints, {},
    "Comma-separated list of Unicode codepoints or ranges (in hex like 0x41 or "
    "U+0041, or decimal, with ranges using - or .. like 0xFE00-0xFE0F) to add "
    "to the initial font in the produced segmenter config.");

namespace ift::util {

static absl::StatusOr<hb_codepoint_t> ParseSingleCodepoint(
    absl::string_view token) {
  token = absl::StripAsciiWhitespace(token);
  if (token.empty()) {
    return absl::InvalidArgumentError("Empty codepoint");
  }

  uint32_t val = 0;
  if (absl::StartsWithIgnoreCase(token, "0x") ||
      absl::StartsWithIgnoreCase(token, "u+")) {
    absl::string_view hex_part = token.substr(2);
    if (hex_part.empty()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid hex codepoint: ", token));
    }
    for (char c : hex_part) {
      if (!absl::ascii_isxdigit(c)) {
        return absl::InvalidArgumentError(
            absl::StrCat("Invalid character in hex codepoint: ", token));
      }
    }
    if (!absl::SimpleHexAtoi(hex_part, &val)) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to parse hex codepoint: ", token));
    }
  } else {
    for (char c : token) {
      if (!absl::ascii_isdigit(c)) {
        return absl::InvalidArgumentError(
            absl::StrCat("Invalid character in decimal codepoint: ", token));
      }
    }
    if (!absl::SimpleAtoi(token, &val)) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to parse decimal codepoint: ", token));
    }
  }

  if (val > 0x10FFFF) {
    return absl::InvalidArgumentError(
        absl::StrCat("Codepoint value out of range (max 0x10FFFF): ", token));
  }
  return static_cast<hb_codepoint_t>(val);
}

absl::StatusOr<ift::common::CodepointSet> ParseCodepointsFlag(
    absl::Span<const std::string> items) {
  ift::common::CodepointSet result;
  for (const std::string& item : items) {
    for (absl::string_view token :
         absl::StrSplit(item, ',', absl::SkipEmpty())) {
      token = absl::StripAsciiWhitespace(token);
      if (token.empty()) {
        continue;
      }

      size_t sep_pos = token.find("..");
      size_t sep_len = 2;
      if (sep_pos == absl::string_view::npos) {
        sep_pos = token.find('-');
        sep_len = 1;
      }
      if (sep_pos != absl::string_view::npos) {
        absl::string_view start_str = token.substr(0, sep_pos);
        absl::string_view end_str = token.substr(sep_pos + sep_len);
        hb_codepoint_t start = TRY(ParseSingleCodepoint(start_str));
        hb_codepoint_t end = TRY(ParseSingleCodepoint(end_str));
        if (start > end) {
          return absl::InvalidArgumentError(
              absl::StrCat("Range start must be <= end: ", token));
        }
        result.insert_range(start, end);
        continue;
      }

      hb_codepoint_t cp = TRY(ParseSingleCodepoint(token));
      result.insert(cp);
    }
  }
  return result;
}

absl::StatusOr<absl::btree_set<hb_tag_t>> ParseFeatureTagsFlag(
    absl::Span<const std::string> items) {
  absl::btree_set<hb_tag_t> result;
  for (const std::string& item : items) {
    for (absl::string_view tag_str :
         absl::StrSplit(item, ',', absl::SkipEmpty())) {
      if (tag_str.empty()) {
        continue;
      }
      if (tag_str.size() > 4) {
        tag_str = absl::StripAsciiWhitespace(tag_str);
      }
      if (tag_str.size() != 4) {
        return absl::InvalidArgumentError(
            absl::StrCat("Invalid feature tag (must be 4 characters): ",
                         tag_str));
      }
      result.insert(ift::common::FontHelper::ToTag(std::string(tag_str)));
    }
  }
  return result;
}

absl::StatusOr<ift::config::SegmenterConfig>
GenerateAutoSegmenterConfigFromFlags(
    hb_face_t* font, const ift::common::DataFileResolver& resolver,
    std::optional<ift::config::TargetMode> target_mode_override) {
  std::optional<int> quality_level = std::nullopt;
  if (absl::GetFlag(FLAGS_auto_config_quality) > 0) {
    quality_level = absl::GetFlag(FLAGS_auto_config_quality);
  }

  std::optional<std::string> primary_script = std::nullopt;
  if (!absl::GetFlag(FLAGS_auto_config_primary_script).empty()) {
    primary_script = absl::GetFlag(FLAGS_auto_config_primary_script);
  }

  ift::config::TargetMode target_mode =
      target_mode_override.value_or(
          absl::GetFlag(FLAGS_auto_config_unicode_range)
              ? ift::config::UNICODE_RANGE
              : ift::config::IFT);

  auto default_codepoints = TRY(
      ParseCodepointsFlag(absl::GetFlag(FLAGS_auto_config_default_codepoints)));

  auto additional_default_features = TRY(ParseFeatureTagsFlag(
      absl::GetFlag(FLAGS_auto_config_additional_default_features)));

  return ift::config::AutoSegmenterConfig::GenerateConfig(
      font, resolver, primary_script, quality_level, target_mode,
      default_codepoints, additional_default_features);
}

}  // namespace ift::util
