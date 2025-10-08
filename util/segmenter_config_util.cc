#include "util/segmenter_config_util.h"

#include "common/int_set.h"
#include "common/try.h"
#include "ift/encoder/merge_strategy.h"
#include "ift/freq/unicode_frequencies.h"
#include "util/load_codepoints.h"
#include "ift/encoder/subset_definition.h"

using absl::btree_map;
using absl::flat_hash_map;
using absl::StatusOr;
using common::CodepointSet;
using common::SegmentSet;
using ift::encoder::SubsetDefinition;
using ift::encoder::MergeStrategy;
using ift::freq::UnicodeFrequencies;

namespace util {

// Loads unicode frequency data from either a dedicated frequency data file or
// from the codepoint and frequency entries if no data file is given.
StatusOr<UnicodeFrequencies> SegmenterConfigUtil::GetFrequencyData(
    const std::string& frequency_data_file_path) {
  std::filesystem::path freq_path = frequency_data_file_path;
  std::filesystem::path resolved_path = freq_path;
  if (freq_path.is_relative()) {
    std::filesystem::path config_path = config_file_path_;
    resolved_path = config_path.parent_path() / freq_path;
  }

  return util::LoadFrequenciesFromRiegeli(resolved_path.c_str());
}

SubsetDefinition SegmenterConfigUtil::SegmentProtoToSubsetDefinition(
    const SegmentProto& segment) {
  SubsetDefinition def;
  def.codepoints.union_set(util::Values(segment.codepoints()));
  def.feature_tags = util::TagValues(segment.features());
  return def;
}

std::vector<SubsetDefinition> SegmenterConfigUtil::ConfigToSegments(
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

StatusOr<MergeStrategy> SegmenterConfigUtil::ProtoToStrategy(
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

 static MergeStrategy ProtoToStrategy(const HeuristicConfiguration& base,
                                      const HeuristicConfiguration& config) {
  HeuristicConfiguration merged = base;
  merged.MergeFrom(config);
  return MergeStrategy::Heuristic(merged.min_patch_size(),
                                  merged.max_patch_size());
}

StatusOr<std::pair<SegmentSet, MergeStrategy>> SegmenterConfigUtil::ProtoToMergeGroup(
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
        ::util::ProtoToStrategy(base_heuristic, group.heuristic_config());
    return std::make_pair(segment_indices, strategy);
  }
}

StatusOr<btree_map<SegmentSet, MergeStrategy>> SegmenterConfigUtil::ConfigToMergeGroups(
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

}  // namespace util