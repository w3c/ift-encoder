#include <google/protobuf/text_format.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/font_data.h"
#include "common/font_helper.h"
#include "common/int_set.h"
#include "common/try.h"
#include "hb.h"
#include "ift/encoder/closure_glyph_segmenter.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/merge_strategy.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/unicode_frequencies.h"
#include "ift/proto/segmenter_config.pb.h"
#include "util/load_codepoints.h"
#include "util/segmentation_plan.pb.h"

/*
 * Given a code point based segmentation creates an appropriate glyph based
 * segmentation and associated activation conditions that maintain the "closure
 * requirement".
 */

ABSL_FLAG(std::string, input_font, "in.ttf",
          "Name of the font to convert to IFT.");

ABSL_FLAG(
    std::string, config, "config.textpb",
    "Path to a text proto file containing the configuration for the segmenter. "
    "Should contain a single SegmenterConfig message.");

ABSL_FLAG(bool, output_segmentation_plan, false,
          "If set a segmentation plan representing the determined segmentation "
          "will be output to stdout.");

ABSL_FLAG(bool, include_initial_codepoints_in_config, true,
          "If set the generated encoder config will include the initial "
          "codepoint set.");

ABSL_FLAG(bool, output_segmentation_analysis, true,
          "If set an analysis of the segmentation will be output to stderr.");

ABSL_FLAG(
    int, verbosity, 0,
    "Log verbosity level from. 0 is least verbose, higher values are more.");

using absl::btree_map;
using absl::flat_hash_map;
using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using common::CodepointSet;
using common::FontData;
using common::FontHelper;
using common::hb_face_unique_ptr;
using common::SegmentSet;
using google::protobuf::TextFormat;
using ift::encoder::ClosureGlyphSegmenter;
using ift::encoder::GlyphSegmentation;
using ift::encoder::MergeStrategy;
using ift::encoder::Segment;
using ift::encoder::SegmentationCost;
using ift::encoder::SubsetDefinition;
using ift::freq::UnicodeFrequencies;

// TODO XXXX filter produced segments by font codepoints.
// TODO XXXX unit tests for the config -> merge groups functionality.

static StatusOr<SegmenterConfig> LoadConfig() {
  FontData config_text =
      TRY(util::LoadFile(absl::GetFlag(FLAGS_config).c_str()));
  SegmenterConfig config;
  if (!google::protobuf::TextFormat::ParseFromString(config_text.str(),
                                                     &config)) {
    return absl::InvalidArgumentError(StrCat(
        "Failed to parse the input config: ", absl::GetFlag(FLAGS_config)));
  }
  return config;
}

StatusOr<hb_face_unique_ptr> LoadFont(const char* filename) {
  return TRY(util::LoadFile(filename)).face();
}

// Loads unicode frequency data from either a dedicated frequency data file or
// from the codepoint and frequency entries if no data file is given.
StatusOr<UnicodeFrequencies> GetFrequencyData(
    const std::string& frequency_data_file_path) {
  std::filesystem::path freq_path = frequency_data_file_path;
  std::filesystem::path resolved_path = freq_path;
  if (freq_path.is_relative()) {
    std::filesystem::path config_path = absl::GetFlag(FLAGS_config);
    resolved_path = config_path.parent_path() / freq_path;
  }

  return util::LoadFrequenciesFromRiegeli(resolved_path.c_str());
}

static Status Analysis(hb_face_t* font,
                       const btree_map<SegmentSet, MergeStrategy>& merge_groups,
                       const GlyphSegmentation& segmentation) {
  unsigned group_index = 0;
  double overall_cost = 0.0;
  for (const auto& [_, strategy] : merge_groups) {
    if (!strategy.UseCosts()) {
      // Can only evaluate costs for strategies that utilize costs.
      continue;
    }

    auto calculator = strategy.ProbabilityCalculator();
    ClosureGlyphSegmenter segmenter;
    SegmentationCost cost =
        TRY(segmenter.TotalCost(font, segmentation, *calculator));

    overall_cost += cost.total_cost;

    std::cerr << "non_ift_cost_bytes[" << group_index
              << "] = " << (uint64_t)cost.cost_for_non_segmented << std::endl;
    std::cerr << "total_cost_bytes[" << group_index
              << "] = " << (uint64_t)cost.total_cost << std::endl;
    std::cerr << "ideal_cost_bytes[" << group_index
              << "] = " << (uint64_t)cost.ideal_cost << std::endl;
    std::cerr << std::endl;

    group_index++;
  }

  std::cerr << "total_cost_across_groups = " << overall_cost << std::endl;

  return absl::OkStatus();
}

static SubsetDefinition SegmentProtoToSubsetDefinition(
    const SegmentProto& segment) {
  SubsetDefinition def;
  def.codepoints.union_set(util::Values(segment.codepoints()));
  def.feature_tags = util::TagValues(segment.features());
  return def;
}

static std::vector<SubsetDefinition> ConfigToSegments(
    const SegmenterConfig& config, const CodepointSet& font_codepoints,
    flat_hash_map<uint32_t, uint32_t>& segment_id_to_index) {
  if (config.segments().empty()) {
    // No segments provided set up our own. Each codepoint in the font is mapped
    // to one segment. segment id's are the codepoint values.
    unsigned i = 0;
    std::vector<SubsetDefinition> segments;
    for (hb_codepoint_t cp : font_codepoints) {
      segments.push_back(SubsetDefinition{cp});
      segment_id_to_index[cp] = i++;
    }
    return segments;
  }

  std::vector<SubsetDefinition> segments;
  unsigned i = 0;
  for (const auto& [id, segment] : config.segments()) {
    segment_id_to_index[id] = i++;
    segments.push_back(SegmentProtoToSubsetDefinition(segment));
  }

  return segments;
}

static MergeStrategy ProtoToStrategy(const HeuristicConfiguration& base,
                                     const HeuristicConfiguration& config) {
  HeuristicConfiguration merged = base;
  merged.MergeFrom(config);
  return MergeStrategy::Heuristic(merged.min_patch_size(),
                                  merged.max_patch_size());
}

static StatusOr<MergeStrategy> ProtoToStrategy(
    const CostConfiguration& base, const CostConfiguration& config,
    CodepointSet& covered_codepoints) {
  CostConfiguration merged = base;
  merged.MergeFrom(config);

  if (merged.path_to_frequency_data().empty()) {
    return absl::InvalidArgumentError(
        "Path to frequency data must be provided.");
  }

  UnicodeFrequencies freq =
      TRY(GetFrequencyData(merged.path_to_frequency_data()));
  covered_codepoints = freq.CoveredCodepoints();

  MergeStrategy strategy = MergeStrategy::None();
  if (merged.use_bigrams()) {
    strategy = TRY(MergeStrategy::BigramCostBased(
        std::move(freq), merged.network_overhead_cost(),
        merged.min_group_size()));
  } else {
    strategy = TRY(MergeStrategy::CostBased(std::move(freq),
                                            merged.network_overhead_cost(),
                                            merged.min_group_size()));
  }

  strategy.SetOptimizationCutoffFraction(merged.optimization_cutoff_fraction());
  strategy.SetInitFontMergeThreshold(merged.init_font_merge_threshold());

  return strategy;
}

static SegmentSet MapToIndices(
    const SegmentsProto& segments,
    const flat_hash_map<uint32_t, uint32_t>& id_to_index) {
  SegmentSet mapped;
  for (uint32_t s_id : segments.values()) {
    mapped.insert(id_to_index.at(s_id));
  }
  return mapped;
}

static StatusOr<std::pair<SegmentSet, MergeStrategy>> ProtoToMergeGroup(
    const std::vector<SubsetDefinition>& segments,
    const flat_hash_map<uint32_t, uint32_t>& id_to_index,
    const HeuristicConfiguration& base_heuristic,
    const CostConfiguration& base_cost, const MergeGroup& group) {
  if (group.has_cost_config()) {
    CodepointSet covered_codepoints;
    MergeStrategy strategy = TRY(
        ProtoToStrategy(base_cost, group.cost_config(), covered_codepoints));

    SegmentSet segment_indices;
    if (!group.segment_ids().values().empty()) {
      segment_indices = MapToIndices(group.segment_ids(), id_to_index);
    } else if (!id_to_index.empty()) {
      for (const auto& [_, index] : id_to_index) {
        if (segments[index].codepoints.intersects(covered_codepoints)) {
          segment_indices.insert(index);
        }
      }
    }

    return std::make_pair(segment_indices, strategy);
  } else {
    SegmentSet segment_indices;
    if (!group.segment_ids().values().empty()) {
      segment_indices = MapToIndices(group.segment_ids(), id_to_index);
    } else if (!id_to_index.empty()) {
      // For heuristic, the default segment set is just all segments.
      segment_indices.insert_range(0, id_to_index.size() - 1);
    }

    MergeStrategy strategy =
        ProtoToStrategy(base_heuristic, group.heuristic_config());
    return std::make_pair(segment_indices, strategy);
  }
}

static StatusOr<btree_map<SegmentSet, MergeStrategy>> ConfigToMergeGroups(
    const SegmenterConfig& config, const CodepointSet& font_codepoints,
    std::vector<SubsetDefinition>& segments) {
  flat_hash_map<uint32_t, uint32_t> segment_id_to_index;
  segments = ConfigToSegments(config, font_codepoints, segment_id_to_index);

  btree_map<SegmentSet, MergeStrategy> merge_groups;
  for (const auto& merge_group : config.merge_groups()) {
    merge_groups.insert(TRY(ProtoToMergeGroup(
        segments, segment_id_to_index, config.base_heuristic_config(),
        config.base_cost_config(), merge_group)));
  }

  return merge_groups;
}

static Status Main(const std::vector<char*> args) {
  hb_face_unique_ptr font =
      TRY(LoadFont(absl::GetFlag(FLAGS_input_font).c_str()));
  SegmenterConfig config = TRY(LoadConfig());

  CodepointSet font_codepoints = FontHelper::ToCodepointsSet(font.get());
  SubsetDefinition init_segment =
      SegmentProtoToSubsetDefinition(config.initial_segment());
  std::vector<SubsetDefinition> segments;
  btree_map<SegmentSet, MergeStrategy> merge_groups =
      TRY(ConfigToMergeGroups(config, font_codepoints, segments));

  ClosureGlyphSegmenter segmenter;
  GlyphSegmentation segmentation = TRY(segmenter.CodepointToGlyphSegments(
      font.get(), init_segment, segments, merge_groups,
      config.brotli_quality()));

  if (absl::GetFlag(FLAGS_output_segmentation_plan)) {
    SegmentationPlan plan = segmentation.ToSegmentationPlanProto();
    if (!absl::GetFlag(FLAGS_include_initial_codepoints_in_config)) {
      // Requested to not include init codepoints in the generated config.
      plan.clear_initial_codepoints();
    }

    // TODO(garretrieger): assign a basic (single segment) table keyed config.
    // Later on the input to this util should include information on how the
    // segments should be grouped together for the table keyed portion of the
    // font.
    std::string config_string;
    TextFormat::PrintToString(plan, &config_string);
    std::cout << config_string;
  } else {
    // No config requested, just output a simplified plain text representation
    // of the segmentation.
    std::cout << segmentation.ToString() << std::endl;
  }

  if (!absl::GetFlag(FLAGS_output_segmentation_analysis)) {
    return absl::OkStatus();
  }

  std::cerr << ">> Analysis" << std::endl;
  return Analysis(font.get(), merge_groups, segmentation);
}

int main(int argc, char** argv) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::SetGlobalVLogLevel(absl::GetFlag(FLAGS_verbosity));
  auto args = absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  auto sc = Main(args);
  if (!sc.ok()) {
    std::cerr << "Error: " << sc << std::endl;
    return -1;
  }
  return 0;
}
