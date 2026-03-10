#include <google/protobuf/text_format.h>

#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/status/status.h"
#include "common/font_data.h"
#include "common/try.h"
#include "ift/config/auto_segmenter_config.h"
#include "ift/config/load_codepoints.h"

using ift::config::SegmenterConfig;

ABSL_FLAG(std::string, input_font, "in.ttf",
          "Path to the font file to analyze.");

ABSL_FLAG(std::string, primary_script, "Script_latin",
          "The primary script or language frequency data file to use.");

ABSL_FLAG(int, quality, 0,
          "The quality level to use. A value of 0 means auto pick. Valid "
          "values are 1-8.");

using absl::Status;
using common::hb_face_unique_ptr;
using ift::config::AutoSegmenterConfig;

static Status Main(const std::vector<char*> args) {
  std::string input_font_path = absl::GetFlag(FLAGS_input_font);
  auto font_data = TRY(ift::config::LoadFile(input_font_path.c_str()));
  hb_face_unique_ptr font = font_data.face();

  std::optional<int> quality_level = std::nullopt;
  if (absl::GetFlag(FLAGS_quality) > 0) {
    quality_level = absl::GetFlag(FLAGS_quality);
  }

  auto config = TRY(AutoSegmenterConfig::GenerateConfig(
      font.get(), absl::GetFlag(FLAGS_primary_script), quality_level));

  std::string output;
  if (!google::protobuf::TextFormat::PrintToString(config, &output)) {
    return absl::InternalError(
        "Failed to format SegmenterConfig as textproto.");
  }

  std::cout << output;
  return absl::OkStatus();
}

int main(int argc, char** argv) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  auto args = absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  Status sc = Main(args);
  if (!sc.ok()) {
    std::cerr << "Error: " << sc << std::endl;
    return -1;
  }
  return 0;
}
