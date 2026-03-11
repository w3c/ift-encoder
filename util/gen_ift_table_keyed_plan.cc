#include <google/protobuf/text_format.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "ift/common/font_data.h"
#include "ift/common/font_helper.h"
#include "ift/common/int_set.h"
#include "ift/config/load_codepoints.h"
#include "ift/config/segmentation_plan.pb.h"

using ift::config::Codepoints;
using ift::config::SegmentationPlan;

using absl::StatusOr;
using absl::StrCat;
using google::protobuf::TextFormat;
using ift::common::CodepointSet;
using ift::common::FontData;
using ift::common::FontHelper;
using ift::common::hb_blob_unique_ptr;
using ift::common::make_hb_blob;

ABSL_FLAG(
    std::optional<std::string>, font, std::nullopt,
    "Optional, path to a font. If provided the generated config will add an "
    "additional segment if needed that covers any codepoints found in the font "
    "which are not covered by the input subset files.");

ABSL_FLAG(std::optional<std::string>, existing_segmentation_plan, std::nullopt,
          "Optional, path to a segmentation plan. If provided the specified "
          "table keyed "
          "codepoint sets will be added to the existing segmentation plan "
          "instead of a new "
          "one. The combined plan is output to stdout.");

ABSL_FLAG(
    int, verbosity, 0,
    "Log verbosity level from. 0 is least verbose, higher values are more.");

template <typename ProtoType>
ProtoType ToSetProto(const CodepointSet& set) {
  ProtoType values;
  for (uint32_t v : set) {
    values.add_values(v);
  }
  return values;
}

static StatusOr<SegmentationPlan> LoadSegmentationPlan(const char* path) {
  auto config_text = ift::config::LoadFile(path);
  if (!config_text.ok()) {
    return absl::NotFoundError(
        StrCat("Failed to load config file: ", config_text.status()));
  }

  SegmentationPlan plan;
  if (!google::protobuf::TextFormat::ParseFromString(config_text->str(),
                                                     &plan)) {
    return absl::InvalidArgumentError("Failed to parse input config.");
  }

  return plan;
}

/*
 * This utility takes a font + a list of code point subsets and emits an IFT
 * encoder config that will configure the font to be extended by table keyed
 * patches (where each subset is an extension segment).
 *
 * This config can be appended onto a config which configures the glyph keyed
 * segmentation plan to produce a complete mixed mode configuration.
 */

int main(int argc, char** argv) {
  absl::SetProgramUsageMessage(
      "Generates a table keyed segmentation plan for a font.\n"
      "\n"
      "Usage: gen_ift_table_keyed_plan --font=\"myfont.ttf\" <initial "
      "subset fil> <subset 1 file> [... <subset n file>]\n"
      "\n"
      "Where a subset file lists one codepoint per line in hexadecimal format: "
      "0xXXXX\n"
      "\n"
      "If you don't want the config to contain an initial codepoint set, pass "
      "an empty file as the first argument.\n");
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::SetGlobalVLogLevel(absl::GetFlag(FLAGS_verbosity));
  auto args = absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  SegmentationPlan config;
  CodepointSet init_codepoints;

  std::vector<CodepointSet> sets;
  if (absl::GetFlag(FLAGS_existing_segmentation_plan).has_value()) {
    auto plan = LoadSegmentationPlan(
        absl::GetFlag(FLAGS_existing_segmentation_plan)->c_str());
    if (!plan.ok()) {
      std::cerr << "Error: " << plan.status() << std::endl;
      return -1;
    }
    config = *plan;
    for (unsigned cp : plan->initial_codepoints().values()) {
      init_codepoints.insert(cp);
    }

    CodepointSet empty;
    sets.push_back(empty);
  } else if (args.size() <= 1) {
    std::cerr << "Usage:" << std::endl
              << "gen_ift_table_keyed_plan <initial font subset file> "
                 "<table keyed subset 1 file> [... <table keyed subset file n>]"
              << std::endl
              << std::endl
              << "Where a subset file lists one codepoint per line in "
                 "hexadecimal format: 0xXXXX"
              << std::endl
              << std::endl
              << "If you don't want the config to contain an initial codepoint "
                 "set, pass an empty file as the first argument."
              << std::endl;
    return -1;
  }

  for (size_t i = 1; i < args.size(); i++) {
    const char* arg = args[i];
    std::string_view arg_str(arg);

    CodepointSet cps;
    auto result = ift::config::LoadCodepointsOrdered(arg);
    if (!result.ok()) {
      std::cerr << "Failed to load codepoints from " << arg << ": "
                << result.status() << std::endl;
      return -1;
    }

    for (const auto& cp_and_freq : *result) {
      cps.insert(cp_and_freq.codepoint);
    }

    if (!sets.empty()) {
      cps.subtract(init_codepoints);
    }

    sets.push_back(cps);
  }

  std::optional<std::string> input_font = absl::GetFlag(FLAGS_font);
  if (input_font.has_value()) {
    // If a font is supplied check if it contains any codepoints not accounted
    // for in an input subset. Add all of these to one last segment.
    auto font_data = ift::config::LoadFile(input_font->c_str());
    if (!font_data.ok()) {
      std::cerr << "Failed to load font, " << *input_font << std::endl;
      return -1;
    }

    auto face = font_data->face();
    auto font_codepoints = FontHelper::ToCodepointsSet(face.get());
    for (const auto& set : sets) {
      for (uint32_t v : set) {
        font_codepoints.erase(v);
      }
    }

    if (!font_codepoints.empty()) {
      sets.push_back(font_codepoints);
    }
  }

  bool initial = true;
  for (const auto& set : sets) {
    if (initial) {
      initial = false;
      if (!set.empty()) {
        *config.mutable_initial_codepoints() = ToSetProto<Codepoints>(set);
      }
      continue;
    }

    if (!set.empty()) {
      *config.add_non_glyph_codepoint_segmentation() =
          ToSetProto<Codepoints>(set);
    }
  }

  std::string config_string;
  TextFormat::PrintToString(config, &config_string);
  std::cout << config_string;

  return 0;
}