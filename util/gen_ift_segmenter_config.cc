#include <google/protobuf/text_format.h>

#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/status/status.h"
#include "ift/common/bazel_data_file_resolver.h"
#include "ift/common/font_data.h"
#include "ift/common/try.h"
#include "ift/config/auto_segmenter_config.h"
#include "ift/config/load_codepoints.h"
#include "util/auto_config_flags.h"

using ift::config::SegmenterConfig;

ABSL_FLAG(std::string, input_font, "in.ttf",
          "Path to the font file to analyze.");

ABSL_FLAG(std::string, primary_script, "",
    "This sets the primary script or "
    "language frequency data file the font is expected to be used with. "
    "This is used to signal the merger to prioritize performance with the "
    "specified script/language. Concretely: initial font merging will be "
    "done using the primary script/language frequency data. Additionally, "
    "when multiple scripts overlap (eg. like with CJK) merging will "
    "prioritize optimizing against the primary script/language instead of "
    "all overlapping scripts equally.");

ABSL_FLAG(int, quality, 0,
          "The quality level to use. A value of 0 means auto pick. Valid "
          "values are 1-8.");

ABSL_FLAG(bool, unicode_range, false,
          "If true, generate a segmenter config with target_mode set to "
          "UNICODE_RANGE instead of IFT.");

ABSL_FLAG(
    std::vector<std::string>, additional_default_features, {},
    "Comma-separated list of OpenType feature tags (e.g. smcp,dlig) to add to "
    "the initial font in the produced segmenter config.");

ABSL_FLAG(
    std::vector<std::string>, default_codepoints, {},
    "Comma-separated list of Unicode codepoints or ranges (in hex like 0x41 or "
    "U+0041, or decimal, with ranges using - or .. like 0xFE00-0xFE0F) to add "
    "to the initial font in the produced segmenter config.");

ABSL_FLAG(
    int, verbosity, 0,
    "Log verbosity level from. 0 is least verbose, higher values are more.");

using absl::Status;
using ift::common::BazelDataFileResolver;
using ift::common::hb_face_unique_ptr;
using ift::config::AutoSegmenterConfig;

static Status Main(const std::vector<char*> args) {
  auto resolver = TRY(BazelDataFileResolver::Create(args[0]));

  std::string input_font_path = absl::GetFlag(FLAGS_input_font);
  auto font_data = TRY(ift::config::LoadFile(input_font_path.c_str()));
  hb_face_unique_ptr font = font_data.face();

  std::optional<int> quality_level = std::nullopt;
  if (absl::GetFlag(FLAGS_quality) > 0) {
    quality_level = absl::GetFlag(FLAGS_quality);
  }

  std::optional<std::string> primary_script = std::nullopt;
  if (!absl::GetFlag(FLAGS_primary_script).empty()) {
    primary_script = absl::GetFlag(FLAGS_primary_script);
  }
  ift::config::TargetMode target_mode = absl::GetFlag(FLAGS_unicode_range)
                                            ? ift::config::UNICODE_RANGE
                                            : ift::config::IFT;
  auto default_codepoints =
      TRY(ift::util::ParseCodepointsFlag(absl::GetFlag(FLAGS_default_codepoints)));
  auto additional_default_features = TRY(ift::util::ParseFeatureTagsFlag(
      absl::GetFlag(FLAGS_additional_default_features)));
  auto config = TRY(AutoSegmenterConfig::GenerateConfig(
      font.get(), *resolver, primary_script, quality_level, target_mode,
      default_codepoints, additional_default_features));

  std::string output;
  if (!google::protobuf::TextFormat::PrintToString(config, &output)) {
    return absl::InternalError(
        "Failed to format SegmenterConfig as textproto.");
  }

  std::cout << output;
  return absl::OkStatus();
}

int main(int argc, char** argv) {
  absl::SetProgramUsageMessage(
      "Generates a segmenter config to use with a font.\n"
      "\n"
      "Usage: gen_ift_segmenter_config --font=\"myfont.ttf\" [--quality=n] "
      "[--primary_script=Script_foo] [--unicode_range] "
      "[--additional_default_features=tag1,tag2] "
      "[--default_codepoints=cp1,cp2-cp3]\n");
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::SetGlobalVLogLevel(absl::GetFlag(FLAGS_verbosity));
  auto args = absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  Status sc = Main(args);
  if (!sc.ok()) {
    std::cerr << "Error: " << sc << std::endl;
    return -1;
  }
  return 0;
}
