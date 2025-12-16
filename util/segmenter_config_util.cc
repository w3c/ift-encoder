#include "util/segmenter_config_util.h"

#include <cstdint>

#include "common/int_set.h"
#include "common/try.h"
#include "ift/encoder/merge_strategy.h"
#include "ift/encoder/subset_definition.h"
#include "ift/feature_registry/feature_registry.h"
#include "ift/freq/unicode_frequencies.h"
#include "util/load_codepoints.h"

using absl::btree_map;
using absl::btree_set;
using absl::flat_hash_map;
using absl::StatusOr;
using common::CodepointSet;
using common::SegmentSet;
using ift::encoder::MergeStrategy;
using ift::encoder::SubsetDefinition;
using ift::feature_registry::DefaultFeatureTags;
using ift::freq::UnicodeFrequencies;

namespace util {

// Loads unicode frequency data from either a dedicated frequency data file or
// from the codepoint and frequency entries if no data file is given.
StatusOr<UnicodeFrequencies> SegmenterConfigUtil::GetFrequencyData(
    const std::string& frequency_data_file_path, bool built_in) {
  if (built_in) {
    return util::LoadBuiltInFrequencies(frequency_data_file_path.c_str());
  }

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
    const SegmenterConfig& config, const SubsetDefinition& init_segment,
    const CodepointSet& font_codepoints,
    const btree_set<hb_tag_t>& font_features,
    flat_hash_map<SegmentId, uint32_t>& segment_id_to_index) {
  std::vector<SubsetDefinition> segments;
  unsigned index = 0;

  if (!config.feature_segments().empty()) {
    for (const auto& [id, features] : config.feature_segments()) {
      SubsetDefinition def;
      def.feature_tags = util::TagValues(features);
      segments.push_back(def);
      segment_id_to_index[SegmentId{.feature = true, .id_value = id}] = index++;
    }
  } else if (config.generate_feature_segments()) {
    uint32_t id = 0;
    for (hb_tag_t tag : font_features) {
      if (DefaultFeatureTags().contains(tag)) {
        continue;
      }
      SubsetDefinition def;
      def.feature_tags = {tag};
      segments.push_back(def);
      segment_id_to_index[SegmentId{.feature = true, .id_value = id++}] =
          index++;
    }
  }

  if (config.segments().empty()) {
    // No segments provided set up our own. Each codepoint in the font is mapped
    // to one segment. segment id's are the codepoint values.
    for (hb_codepoint_t cp : font_codepoints) {
      if (init_segment.codepoints.contains(cp)) {
        continue;
      }

      segments.push_back(SubsetDefinition{cp});
      segment_id_to_index[SegmentId{.id_value = cp}] = index++;
    }
    return segments;
  }

  // protobuf maps are unordered, so to get a consistent iteration order
  // first convert to an ordered map (on id).
  absl::btree_map<uint32_t, SegmentProto> ordered(config.segments().begin(),
                                                  config.segments().end());

  for (const auto& [id, segment] : ordered) {
    segment_id_to_index[SegmentId{.id_value = id}] = index++;
    SubsetDefinition def = SegmentProtoToSubsetDefinition(segment);
    def.codepoints.intersect(font_codepoints);
    segments.push_back(def);
  }

  return segments;
}

StatusOr<MergeStrategy> SegmenterConfigUtil::ProtoToStrategy(
    const CostConfiguration& base, const CostConfiguration& config,
    CodepointSet& covered_codepoints) {
  CostConfiguration merged = base;
  merged.MergeFrom(config);

  if (merged.path_to_frequency_data().empty() &&
      merged.built_in_freq_data_name().empty()) {
    return absl::InvalidArgumentError(
        "Path to frequency data must be provided.");
  }

  UnicodeFrequencies freq =
      config.has_built_in_freq_data_name()
          ? TRY(GetFrequencyData(merged.built_in_freq_data_name(), true))
          : TRY(GetFrequencyData(merged.path_to_frequency_data(), false));

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

  strategy.SetUsePatchMerges(merged.experimental_use_patch_merges());

  strategy.SetOptimizationCutoffFraction(merged.optimization_cutoff_fraction());
  strategy.SetBestCaseSizeReductionFraction(
      merged.best_case_size_reduction_fraction());

  if (merged.has_initial_font_merge_threshold()) {
    strategy.SetInitFontMergeThreshold(merged.initial_font_merge_threshold());
  }

  if (merged.has_initial_font_merge_probability_threshold()) {
    strategy.SetInitFontMergeProbabilityThreshold(
        merged.initial_font_merge_probability_threshold());
  }

  return strategy;
}

SegmentSet SegmenterConfigUtil::MapToIndices(
    const SegmentsProto& segments,
    const flat_hash_map<SegmentId, uint32_t>& id_to_index) {
  SegmentSet mapped;
  for (uint32_t s_id : segments.values()) {
    mapped.insert(id_to_index.at(SegmentId{.id_value = s_id}));
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

StatusOr<std::pair<SegmentSet, MergeStrategy>>
SegmenterConfigUtil::ProtoToMergeGroup(
    const std::vector<SubsetDefinition>& segments,
    const flat_hash_map<SegmentId, uint32_t>& id_to_index,
    const HeuristicConfiguration& base_heuristic,
    const CostConfiguration& base_cost, const MergeGroup& group) {
  SegmentSet segment_indices;
  for (uint32_t feature_group_id : group.feature_segment_ids().values()) {
    segment_indices.insert(id_to_index.at(
        SegmentId{.feature = true, .id_value = feature_group_id}));
  }

  MergeStrategy strategy = MergeStrategy::None();

  if (group.has_cost_config()) {
    CodepointSet covered_codepoints;
    strategy = TRY(
        ProtoToStrategy(base_cost, group.cost_config(), covered_codepoints));

    if (group.has_segment_ids()) {
      segment_indices.union_set(MapToIndices(group.segment_ids(), id_to_index));
    } else if (!id_to_index.empty()) {
      for (const auto& [_, index] : id_to_index) {
        if (segments[index].codepoints.intersects(covered_codepoints)) {
          segment_indices.insert(index);
        }
      }
    }

    strategy.SetPreClosureGroupSize(group.preprocess_merging_group_size());
    strategy.SetPreClosureProbabilityThreshold(
        group.preprocess_merging_probability_threshold());
  } else {
    if (group.has_segment_ids()) {
      segment_indices.union_set(MapToIndices(group.segment_ids(), id_to_index));
    } else if (!id_to_index.empty()) {
      // For heuristic, the default segment set is just all segments.
      segment_indices.insert_range(0, id_to_index.size() - 1);
    }

    strategy =
        ::util::ProtoToStrategy(base_heuristic, group.heuristic_config());

    strategy.SetPreClosureGroupSize(group.preprocess_merging_group_size());
    strategy.SetPreClosureProbabilityThreshold(1.0);
  }

  if (group.has_name()) {
    strategy.SetName(group.name());
  }

  return std::make_pair(segment_indices, strategy);
}

StatusOr<btree_map<SegmentSet, MergeStrategy>>
SegmenterConfigUtil::ConfigToMergeGroups(
    const SegmenterConfig& config, const CodepointSet& font_codepoints,
    const btree_set<hb_tag_t>& font_features,
    std::vector<SubsetDefinition>& segments) {
  SubsetDefinition initial_segment =
      SegmentProtoToSubsetDefinition(config.initial_segment());

  flat_hash_map<SegmentId, uint32_t> segment_id_to_index;
  segments = ConfigToSegments(config, initial_segment, font_codepoints,
                              font_features, segment_id_to_index);

  btree_map<SegmentSet, MergeStrategy> merge_groups;
  for (const auto& merge_group : config.merge_groups()) {
    merge_groups.insert(TRY(ProtoToMergeGroup(
        segments, segment_id_to_index, config.base_heuristic_config(),
        config.base_cost_config(), merge_group)));
  }

  // If provided add a final merge group that applies to any segments not yet
  // covered.
  if (!config.has_ungrouped_config() || segments.empty()) {
    return merge_groups;
  }

  SegmentSet covered_segments;
  for (const auto& [segments, _] : merge_groups) {
    covered_segments.union_set(segments);
  }

  SegmentSet uncovered_segments;
  uncovered_segments.insert_range(0, segments.size() - 1);
  uncovered_segments.subtract(covered_segments);
  if (uncovered_segments.empty()) {
    // Final group is not needed.
    return merge_groups;
  }

  MergeStrategy strategy = util::ProtoToStrategy(config.base_heuristic_config(),
                                                 config.ungrouped_config());
  strategy.SetName("Ungrouped");
  strategy.SetPreClosureGroupSize(
      config.preprocess_merging_group_size_for_ungrouped());
  strategy.SetPreClosureProbabilityThreshold(1.0);

  merge_groups.insert(std::make_pair(uncovered_segments, strategy));

  return merge_groups;
}

}  // namespace util