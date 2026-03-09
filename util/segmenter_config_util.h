#ifndef UTIL_SEGMENTER_CONFIG_UTIL_H_
#define UTIL_SEGMENTER_CONFIG_UTIL_H_

#include "absl/container/btree_map.h"
#include "absl/status/statusor.h"
#include "common/int_set.h"
#include "hb.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/merge_strategy.h"
#include "ift/encoder/subset_definition.h"
#include "util/segmentation_plan.pb.h"
#include "util/segmenter_config.pb.h"

namespace util {

struct SegmentationResult {
  ift::encoder::GlyphSegmentation segmentation;
  ift::proto::SegmentationPlan plan;
  absl::btree_map<common::SegmentSet, ift::encoder::MergeStrategy> merge_groups;
};

class SegmenterConfigUtil {
 public:
  SegmenterConfigUtil(std::string config_file_path)
      : config_file_path_(config_file_path) {}

  absl::StatusOr<SegmentationResult> RunSegmenter(
      hb_face_t* face, const ift::proto::SegmenterConfig& config);

  ift::encoder::SubsetDefinition SegmentProtoToSubsetDefinition(
      const ift::proto::SegmentProto& segment);

  absl::StatusOr<
      absl::btree_map<common::SegmentSet, ift::encoder::MergeStrategy>>
  ConfigToMergeGroups(const ift::proto::SegmenterConfig& config,
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
      const ift::proto::SegmenterConfig& config,
      const ift::encoder::SubsetDefinition& init_segment,
      const common::CodepointSet& font_codepoints,
      const absl::btree_set<hb_tag_t>& font_features,
      absl::flat_hash_map<SegmentId, uint32_t>& segment_id_to_index);

  absl::StatusOr<ift::freq::UnicodeFrequencies> GetFrequencyData(
      const std::string& frequency_data_file_path, bool built_in);

  absl::StatusOr<ift::encoder::MergeStrategy> ProtoToStrategy(
      const ift::proto::CostConfiguration& base, const ift::proto::CostConfiguration& config,
      common::CodepointSet& covered_codepoints);

  absl::StatusOr<std::pair<common::SegmentSet, ift::encoder::MergeStrategy>>
  ProtoToMergeGroup(const std::vector<ift::encoder::SubsetDefinition>& segments,
                    const absl::flat_hash_map<SegmentId, uint32_t>& id_to_index,
                    const ift::proto::HeuristicConfiguration& base_heuristic,
                    const ift::proto::CostConfiguration& base_cost,
                    const ift::proto::MergeGroup& group);

  static common::SegmentSet MapToIndices(
      const ift::proto::SegmentsProto& segments,
      const absl::flat_hash_map<SegmentId, uint32_t>& id_to_index);

  std::string config_file_path_;
};

}  // namespace util

#endif  // UTIL_SEGMENTER_CONFIG_UTIL_H_