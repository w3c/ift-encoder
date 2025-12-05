#include <google/protobuf/text_format.h>

#include <cstdint>
#include <cstdio>
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
#include "util/load_codepoints.h"
#include "util/segmentation_plan.pb.h"
#include "util/segmenter_config.pb.h"
#include "util/segmenter_config_util.h"

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
    bool, output_fallback_glyph_count, false,
    "If set the number of fallback glyphs in the segmentation will be output.");

ABSL_FLAG(
    int, verbosity, 0,
    "Log verbosity level from. 0 is least verbose, higher values are more.");

using absl::btree_map;
using absl::btree_set;
using absl::flat_hash_map;
using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using common::CodepointSet;
using common::FontData;
using common::FontHelper;
using common::GlyphSet;
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
using util::SegmenterConfigUtil;

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
    ClosureGlyphSegmenter segmenter(11, 11);
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

static void AddTableKeyedSegments(
    SegmentationPlan& plan,
    const btree_map<SegmentSet, MergeStrategy>& merge_groups,
    const std::vector<SubsetDefinition>& segments,
    const SubsetDefinition& init_segment) {
  std::vector<SubsetDefinition> table_keyed_segments;
  for (const auto& [segment_ids, _] : merge_groups) {
    SubsetDefinition new_segment;
    for (uint32_t s : segment_ids) {
      new_segment.Union(segments.at(s));
    }
    new_segment.Subtract(init_segment);
    table_keyed_segments.push_back(new_segment);
  }

  uint32_t max_id = 0;
  for (const auto& [id, _] : plan.segments()) {
    if (id > max_id) {
      max_id = id;
    }
  }

  uint32_t next_id = max_id + 1;
  auto* plan_segments = plan.mutable_segments();
  for (const SubsetDefinition& def : table_keyed_segments) {
    GlyphSegmentation::SubsetDefinitionToSegment(def,
                                                 (*plan_segments)[next_id]);
    SegmentsProto* segment_ids = plan.add_non_glyph_segments();
    segment_ids->add_values(next_id);
    next_id++;
  }
}

static Status OutputFallbackGlyphCount(hb_face_t* original_face,
                                       const ClosureGlyphSegmenter& segmenter,
                                       const GlyphSegmentation& segmentation) {
  uint32_t num_fallback_glyphs = segmentation.UnmappedGlyphs().size();
  uint32_t fallback_glyphs_size = 0;
  uint32_t all_glyphs_size = 0;
  TRYV(segmenter.FallbackCost(original_face, segmentation, fallback_glyphs_size,
                              all_glyphs_size));

  GlyphSet all_glyphs;
  for (const auto& [_, gids] : segmentation.GidSegments()) {
    all_glyphs.union_set(gids);
  }

  uint32_t num_glyphs = all_glyphs.size() + num_fallback_glyphs;
  std::cout << "num_fallback_glyphs, " << num_fallback_glyphs << ", "
            << num_glyphs << ", " << fallback_glyphs_size << ", "
            << all_glyphs_size << std::endl;

  return absl::OkStatus();
}

static Status Main(const std::vector<char*> args) {
  hb_face_unique_ptr font =
      TRY(LoadFont(absl::GetFlag(FLAGS_input_font).c_str()));
  SegmenterConfig config = TRY(LoadConfig());

  SegmenterConfigUtil config_util(absl::GetFlag(FLAGS_config));

  CodepointSet font_codepoints = FontHelper::ToCodepointsSet(font.get());
  btree_set<hb_tag_t> font_features = FontHelper::GetFeatureTags(font.get());
  SubsetDefinition init_segment =
      config_util.SegmentProtoToSubsetDefinition(config.initial_segment());

  std::vector<SubsetDefinition> segments;
  btree_map<SegmentSet, MergeStrategy> merge_groups =
      TRY(config_util.ConfigToMergeGroups(config, font_codepoints,
                                          font_features, segments));

  ClosureGlyphSegmenter segmenter(
      config.brotli_quality(),
      config.brotli_quality_for_initial_font_merging());
  GlyphSegmentation segmentation = TRY(segmenter.CodepointToGlyphSegments(
      font.get(), init_segment, segments, merge_groups,
      config.unmapped_glyph_handling()));

  if (absl::GetFlag(FLAGS_output_segmentation_plan)) {
    SegmentationPlan plan = segmentation.ToSegmentationPlanProto();
    if (!absl::GetFlag(FLAGS_include_initial_codepoints_in_config)) {
      // Requested to not include init codepoints in the generated config.
      plan.clear_initial_codepoints();
    }

    if (config.generate_table_keyed_segments()) {
      AddTableKeyedSegments(plan, merge_groups, segments, init_segment);
    }

    SegmentationPlan combined = config.base_segmentation_plan();
    combined.MergeFrom(plan);

    // TODO(garretrieger): assign a basic (single segment) table keyed config.
    // Later on the input to this util should include information on how the
    // segments should be grouped together for the table keyed portion of the
    // font.
    std::string config_string;
    TextFormat::PrintToString(combined, &config_string);
    std::cout << config_string;
  } else {
    // No config requested, just output a simplified plain text representation
    // of the segmentation.
    std::cout << segmentation.ToString() << std::endl;
  }

  if (absl::GetFlag(FLAGS_output_fallback_glyph_count)) {
    TRYV(OutputFallbackGlyphCount(font.get(), segmenter, segmentation));
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
