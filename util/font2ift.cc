#include <google/protobuf/text_format.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "hb.h"
#include "ift/common/axis_range.h"
#include "ift/common/font_data.h"
#include "ift/common/font_helper.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/config/auto_segmenter_config.h"
#include "ift/config/config_compiler.h"
#include "ift/config/load_codepoints.h"
#include "ift/config/segmentation_plan.pb.h"
#include "ift/config/segmenter_config.pb.h"
#include "ift/config/segmenter_config_util.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/compiler.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/subset_definition.h"
#include "util/auto_config_flags.h"

using ift::config::ActivationConditionProto;
using ift::config::ConfigCompiler;
using ift::config::DesignSpace;
using ift::config::SegmentationPlan;

/*
 * Utility that converts a standard font file into an IFT font file optionally
 * following a supplied segmentation plan.
 *
 * Configuration is provided as a textproto file following the
 * segmentation_plan.proto schema.
 *
 * If no configuration is supplied it will be auto generated.
 */

ABSL_FLAG(std::string, input_font, "in.ttf",
          "Name of the font to convert to IFT.");

ABSL_FLAG(std::string, plan, "auto",
          "Path to a plan file which is a textproto following the "
          "segmentation_plan.proto schema. If set to \"auto\", then "
          "segmentation plan will be automatically generated.");

ABSL_FLAG(std::string, output_path, "./",
          "Path to write output files under (base font and patches).");

ABSL_FLAG(std::string, output_font, "out.woff2",
          "Name of the outputted base font.");

ABSL_FLAG(bool, woff2_encode, true,
          "If enabled the output font will be woff2 encoded. Transformations "
          "in woff2 will be disabled when necessary to keep the woff2 encoding "
          "compatible with IFT.");

ABSL_FLAG(
    int, verbosity, 0,
    "Log verbosity level from. 0 is least verbose, higher values are more.");

using absl::btree_set;
using absl::flat_hash_map;
using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using ift::common::AxisRange;
using ift::common::CodepointSet;
using ift::common::FontData;
using ift::common::FontHelper;
using ift::common::hb_blob_unique_ptr;
using ift::common::hb_face_unique_ptr;
using ift::common::IntSet;
using ift::common::make_hb_blob;
using ift::common::SegmentSet;
using ift::config::AutoSegmenterConfig;
using ift::encoder::ActivationCondition;
using ift::encoder::Compiler;
using ift::encoder::design_space_t;
using ift::encoder::GlyphSegmentation;
using ift::encoder::SubsetDefinition;

// TODO(garretrieger): add check that all glyph patches have at least one
// activation condition.
// TODO(garretrieger): add check that warns when not all parts of the input font
// are reachable in the generated encoding.
//                     (all glyph ids covered by a patch, all codepoints, etc,
//                     covered by non glyph segments).

StatusOr<hb_face_unique_ptr> load_font(const char* filename) {
  return TRY(ift::config::LoadFile(filename)).face();
}

Status write_file(const std::string& name, const FontData& data) {
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

void write_patch(const std::string& url, const FontData& patch) {
  std::string output_path = absl::GetFlag(FLAGS_output_path);
  std::cerr << "  Writing patch: " << StrCat(output_path, "/", url)
            << std::endl;
  auto sc = write_file(StrCat(output_path, "/", url), patch);
  if (!sc.ok()) {
    std::cerr << sc.message() << std::endl;
    exit(-1);
  }
}

int write_output(const Compiler::Encoding& encoding) {
  std::string output_path = absl::GetFlag(FLAGS_output_path);
  std::string output_font = absl::GetFlag(FLAGS_output_font);

  std::cerr << "  Writing init font: " << StrCat(output_path, "/", output_font)
            << std::endl;
  auto sc =
      write_file(StrCat(output_path, "/", output_font), encoding.init_font);
  if (!sc.ok()) {
    std::cerr << sc.message() << std::endl;
    return -1;
  }

  for (const auto& p : encoding.patches) {
    write_patch(p.first, p.second);
  }

  return 0;
}

StatusOr<SegmentationPlan> CreateSegmentationPlan(hb_face_t* font) {
  SegmentationPlan plan;
  if (absl::GetFlag(FLAGS_plan).empty() ||
      absl::GetFlag(FLAGS_plan) == "auto") {
    std::cerr << ">> auto generating segmentation plan:" << std::endl;
    std::optional<int> quality_level = std::nullopt;
    if (absl::GetFlag(FLAGS_auto_config_quality) > 0) {
      quality_level = absl::GetFlag(FLAGS_auto_config_quality);
    }
    auto config = AutoSegmenterConfig::GenerateConfig(
        font, absl::GetFlag(FLAGS_auto_config_primary_script), quality_level);
    if (!config.ok()) {
      return absl::InternalError(
          StrCat("Failed to generate config: ", config.status().message()));
    }
    ift::config::SegmenterConfigUtil config_util("");
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
      "Converts OpenType and TrueType fonts into IFT encoded fonts.\n"
      "\n"
      "Usage: font2ift --input_font=\"myfont.ttf\" --output_path=\"ift/\" "
      "--output_font=\"myfont.itf.ttf\"\n"
      "\n"
      "Optional: a segmentation plan can be provided with the --plan flag. If "
      "one is not given then it will be generated.");
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::SetGlobalVLogLevel(absl::GetFlag(FLAGS_verbosity));
  auto args = absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  auto font = load_font(absl::GetFlag(FLAGS_input_font).c_str());
  if (!font.ok()) {
    std::cerr << "Failed to load input font: " << font.status() << std::endl;
    return -1;
  }

  auto plan = CreateSegmentationPlan(font->get());
  if (!plan.ok()) {
    std::cerr << plan.status().message() << std::endl;
    return -1;
  }

  Compiler compiler;
  compiler.SetFace(font->get());
  compiler.SetWoff2Encode(absl::GetFlag(FLAGS_woff2_encode));

  auto sc = ConfigCompiler::Configure(*plan, compiler);
  if (!sc.ok()) {
    std::cerr << "Failed to apply configuration to the encoder: " << sc
              << std::endl;
    return -1;
  }

  std::cout << ">> encoding:" << std::endl;
  auto encoding = compiler.Compile();
  if (!encoding.ok()) {
    std::cerr << "Encoding failed: " << encoding.status() << std::endl;
    return -1;
  }

  std::cout << ">> generating output patches:" << std::endl;
  return write_output(*encoding);
}
