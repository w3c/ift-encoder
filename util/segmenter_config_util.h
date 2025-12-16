#ifndef UTIL_SEGMENTER_CONFIG_UTIL_H_
#define UTIL_SEGMENTER_CONFIG_UTIL_H_

#include "absl/container/btree_map.h"
#include "absl/status/statusor.h"
#include "common/int_set.h"
#include "ift/encoder/merge_strategy.h"
#include "ift/encoder/subset_definition.h"
#include "util/segmenter_config.pb.h"

namespace util {

class SegmenterConfigUtil {
 public:
  SegmenterConfigUtil(std::string config_file_path)
      : config_file_path_(config_file_path) {}

  ift::encoder::SubsetDefinition SegmentProtoToSubsetDefinition(
      const SegmentProto& segment);

  absl::StatusOr<
      absl::btree_map<common::SegmentSet, ift::encoder::MergeStrategy>>
  ConfigToMergeGroups(const SegmenterConfig& config,
                      const common::CodepointSet& font_codepoints,
                      const absl::btree_set<hb_tag_t>& font_features,
                      std::vector<ift::encoder::SubsetDefinition>& segments);

 private:
  struct SegmentId {
    bool feature = false;
    uint32_t id_value;

    bool operator==(const SegmentId& other) const {
      return feature == other.feature && id_value == other.id_value;
    }

    template <typename H>
    friend H AbslHashValue(H h, const SegmentId& id) {
      return H::combine(std::move(h), id.feature, id.id_value);
    }
  };

  std::vector<ift::encoder::SubsetDefinition> ConfigToSegments(
      const SegmenterConfig& config,
      const ift::encoder::SubsetDefinition& init_segment,
      const common::CodepointSet& font_codepoints,
      const absl::btree_set<hb_tag_t>& font_features,
      absl::flat_hash_map<SegmentId, uint32_t>& segment_id_to_index);

  absl::StatusOr<ift::freq::UnicodeFrequencies> GetFrequencyData(
      const std::string& frequency_data_file_path, bool built_in);

  absl::StatusOr<ift::encoder::MergeStrategy> ProtoToStrategy(
      const CostConfiguration& base, const CostConfiguration& config,
      common::CodepointSet& covered_codepoints);

  absl::StatusOr<std::pair<common::SegmentSet, ift::encoder::MergeStrategy>>
  ProtoToMergeGroup(const std::vector<ift::encoder::SubsetDefinition>& segments,
                    const absl::flat_hash_map<SegmentId, uint32_t>& id_to_index,
                    const HeuristicConfiguration& base_heuristic,
                    const CostConfiguration& base_cost,
                    const MergeGroup& group);

  static common::SegmentSet MapToIndices(
      const SegmentsProto& segments,
      const absl::flat_hash_map<SegmentId, uint32_t>& id_to_index);

  std::string config_file_path_;
};

}  // namespace util

#endif  // UTIL_SEGMENTER_CONFIG_UTIL_H_