#include <google/protobuf/text_format.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "hb.h"
#include "ift/common/bazel_data_file_resolver.h"
#include "ift/common/data_file_resolver.h"
#include "ift/common/font_data.h"
#include "ift/common/try.h"
#include "ift/config/auto_segmenter_config.h"
#include "ift/config/config_compiler.h"
#include "ift/config/load_codepoints.h"
#include "ift/config/segmentation_plan.pb.h"
#include "ift/config/segmenter_config.pb.h"
#include "ift/config/segmenter_config_util.h"
#include "ift/encoder/unicode_range_compiler.h"
#include "util/auto_config_flags.h"
#include "util/path_util.h"

using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using ift::common::BazelDataFileResolver;
using ift::common::DataFileResolver;
using ift::common::FontData;
using ift::common::hb_face_unique_ptr;
using ift::config::AutoSegmenterConfig;
using ift::config::ConfigCompiler;
using ift::config::SegmentationPlan;
using ift::encoder::UnicodeRangeCompiler;
using ift::util::JoinAndValidatePath;

/*
 * [EXPERIMENTAL]
 *
 * Note: this feature is still under development and performance of produced unicode ranges
 *
 * Utility that converts a standard font file into a set of WOFF2 font subsets
 * and a @font-face unicode-range CSS file, optionally following a supplied
 * segmentation plan.
 *
 * Configuration is provided as a textproto file following the
 * segmentation_plan.proto schema.
 *
 * If no configuration is supplied it will be auto generated.
 */

ABSL_FLAG(std::string, input_font, "in.ttf",
          "Name of the font to convert to unicode-range subsets.");

ABSL_FLAG(std::string, plan, "auto",
          "Path to a plan file which is a textproto following the "
          "segmentation_plan.proto schema. If set to \"auto\", then "
          "segmentation plan will be automatically generated.");

ABSL_FLAG(std::string, output_path, "./",
          "Path to write output files under (CSS and WOFF2 subsets).");

ABSL_FLAG(std::string, output_css, "out.css",
          "Name of the outputted CSS file.");

ABSL_FLAG(std::string, font_family, "",
          "Optional override for the font-family name in the generated CSS. If "
          "empty, extracted from the input font's name table.");

ABSL_FLAG(bool, woff2_encode, true,
          "If enabled the output font subsets will be woff2 encoded.");

ABSL_FLAG(
    int, verbosity, 0,
    "Log verbosity level from. 0 is least verbose, higher values are more.");

static StatusOr<hb_face_unique_ptr> load_font(const char* filename) {
  return TRY(ift::config::LoadFile(filename)).face();
}

static Status write_bytes(const std::string& name, absl::string_view data) {
  std::ofstream output(name,
                       std::ios::out | std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    return absl::NotFoundError(StrCat("File ", name, " was not found."));
  }
  output.write(data.data(), data.size());
  if (output.bad()) {
    output.close();
    return absl::InternalError(StrCat("Failed to write to ", name, "."));
  }

  output.close();
  return absl::OkStatus();
}

static Status write_output(const UnicodeRangeCompiler::Encoding& encoding) {
  std::string output_path = absl::GetFlag(FLAGS_output_path);
  std::string output_css = absl::GetFlag(FLAGS_output_css);

  std::string css_full_path = TRY(JoinAndValidatePath(output_path, output_css));
  std::cerr << "  Writing CSS: " << css_full_path << std::endl;
  TRYV(write_bytes(css_full_path, encoding.css));

  for (const auto& [name, subset] : encoding.subsets) {
    std::string subset_full_path = TRY(JoinAndValidatePath(output_path, name));
    std::cerr << "  Writing subset: " << subset_full_path << std::endl;
    TRYV(write_bytes(subset_full_path, subset.str()));
  }

  return absl::OkStatus();
}

static StatusOr<SegmentationPlan> CreateSegmentationPlan(
    hb_face_t* font, std::shared_ptr<DataFileResolver> resolver) {
  SegmentationPlan plan;
  if (absl::GetFlag(FLAGS_plan).empty() ||
      absl::GetFlag(FLAGS_plan) == "auto") {
    std::cerr << ">> auto generating unicode-range segmentation plan:"
              << std::endl;
    std::optional<int> quality_level = std::nullopt;
    if (absl::GetFlag(FLAGS_auto_config_quality) > 0) {
      quality_level = absl::GetFlag(FLAGS_auto_config_quality);
    }

    std::optional<std::string> primary_script = std::nullopt;
    if (!absl::GetFlag(FLAGS_auto_config_primary_script).empty()) {
      primary_script = absl::GetFlag(FLAGS_auto_config_primary_script);
    }
    auto config = AutoSegmenterConfig::GenerateConfig(
        font, *resolver, primary_script, quality_level,
        ift::config::UNICODE_RANGE);
    if (!config.ok()) {
      return absl::InternalError(
          StrCat("Failed to generate config: ", config.status().message()));
    }
    ift::config::SegmenterConfigUtil config_util("", resolver);
    auto result = config_util.RunSegmenter(font, *config);
    if (!result.ok()) {
      return absl::InternalError(
          StrCat("Failed to run segmenter: ", result.status().message()));
    }
    plan = std::move(result->plan);
  } else {
    auto config_text = ift::config::LoadFile(absl::GetFlag(FLAGS_plan).c_str());
    if (!config_text.ok()) {
      return absl::InternalError(StrCat("Failed to load config file: ",
                                        config_text.status().message()));
    }

    if (!google::protobuf::TextFormat::ParseFromString(config_text->str(),
                                                       &plan)) {
      return absl::InternalError("Failed to parse input config.");
    }
  }
  return plan;
}

int main(int argc, char** argv) {
  absl::SetProgramUsageMessage(
      "Converts OpenType and TrueType fonts into @font-face unicode-range "
      "WOFF2 subsets and a CSS stylesheet.\n"
      "\n"
      "Usage: font2ur --input_font=\"myfont.ttf\" --output_path=\"ur/\" "
      "--output_css=\"myfont.css\"\n"
      "\n"
      "Optional: a segmentation plan can be provided with the --plan flag. If "
      "one is not given then it will be generated.");
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::SetGlobalVLogLevel(absl::GetFlag(FLAGS_verbosity));
  auto args = absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  std::string input_font_flag = absl::GetFlag(FLAGS_input_font);
  auto font = load_font(input_font_flag.c_str());
  if (!font.ok()) {
    std::cerr << "Failed to load input font: " << font.status() << std::endl;
    return -1;
  }

  auto resolver_status = BazelDataFileResolver::Create(argv[0]);
  if (!resolver_status.ok()) {
    std::cerr << "Failed to create data file resolver: "
              << resolver_status.status() << std::endl;
    return -1;
  }
  auto resolver = std::move(*resolver_status);

  auto plan = CreateSegmentationPlan(font->get(), resolver);
  if (!plan.ok()) {
    std::cerr << plan.status().message() << std::endl;
    return -1;
  }

  std::string font_name = std::filesystem::path(input_font_flag).stem().string();
  if (font_name.empty()) {
    font_name = "out";
  }

  UnicodeRangeCompiler compiler;
  compiler.SetFace(font->get());
  compiler.SetFontName(font_name);
  compiler.SetFontFamily(absl::GetFlag(FLAGS_font_family));
  compiler.SetWoff2Encode(absl::GetFlag(FLAGS_woff2_encode));

  auto sc = ConfigCompiler::Configure(*plan, compiler);
  if (!sc.ok()) {
    std::cerr << "Failed to apply configuration to the compiler: " << sc
              << std::endl;
    return -1;
  }

  std::cout << ">> compiling subsets:" << std::endl;
  auto encoding = compiler.Compile();
  if (!encoding.ok()) {
    std::cerr << "Compilation failed: " << encoding.status() << std::endl;
    return -1;
  }

  std::cout << ">> writing output files:" << std::endl;
  auto status = write_output(*encoding);
  if (!status.ok()) {
    std::cerr << status.message() << std::endl;
    return -1;
  }

  return 0;
}
