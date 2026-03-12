#ifndef IFT_CONFIG_SEGMENTER_CONFIG_UTIL_H_
#define IFT_CONFIG_SEGMENTER_CONFIG_UTIL_H_

#include "absl/container/btree_map.h"
#include "absl/status/statusor.h"
#include "hb.h"
#include "ift/common/int_set.h"
#include "ift/config/segmentation_plan.pb.h"
#include "ift/config/segmenter_config.pb.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/merge_strategy.h"
#include "ift/encoder/subset_definition.h"

namespace ift::config {

struct SegmentationResult {
  ift::encoder::GlyphSegmentation segmentation;
  SegmentationPlan plan;
  absl::btree_map<ift::common::SegmentSet, ift::encoder::MergeStrategy>
      merge_groups;
};

/*
 * Assists in loading and running the segmenter from a protobuf segmenter
 * config.
 */
class SegmenterConfigUtil {
 public:
  SegmenterConfigUtil(std::string config_file_path)
      : config_file_path_(config_file_path) {}

  /*
   * Create a new segmenter, configure it with config, and then run the
   * segmenter on face.
   *
   * Returns the result of the segmenter run.
   */
  absl::StatusOr<SegmentationResult> RunSegmenter(
      hb_face_t* face, const SegmenterConfig& config);

  /*
   * Converts SegmentProto to a SubsetDefition.
   */
  ift::encoder::SubsetDefinition SegmentProtoToSubsetDefinition(
      const SegmentProto& segment);

  /*
   * Converts the merge groups specified in a segmenter config to the equivalent
   * merge strategy objects.
   *
   * font_codepoints/features gives the full list of codepoint and features in
   * the font the segmenter config is for.
   */
  absl::StatusOr<
      absl::btree_map<ift::common::SegmentSet, ift::encoder::MergeStrategy>>
  ConfigToMergeGroups(const SegmenterConfig& config,
                      const ift::common::CodepointSet& font_codepoints,
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
      const ift::common::CodepointSet& font_codepoints,
      const absl::btree_set<hb_tag_t>& font_features,
      absl::flat_hash_map<SegmentId, uint32_t>& segment_id_to_index);

  absl::StatusOr<ift::freq::UnicodeFrequencies> GetFrequencyData(
      const std::string& frequency_data_file_path, bool built_in);

  absl::StatusOr<ift::encoder::MergeStrategy> ProtoToCostStrategy(
      const CostConfiguration& base, const CostConfiguration& config,
      ift::common::CodepointSet& covered_codepoints);

  absl::StatusOr<
      std::pair<ift::common::SegmentSet, ift::encoder::MergeStrategy>>
  ProtoToMergeGroup(const std::vector<ift::encoder::SubsetDefinition>& segments,
                    const absl::flat_hash_map<SegmentId, uint32_t>& id_to_index,
                    const HeuristicConfiguration& base_heuristic,
                    const CostConfiguration& base_cost,
                    const MergeGroup& group);

  static ift::common::SegmentSet MapToIndices(
      const SegmentsProto& segments,
      const absl::flat_hash_map<SegmentId, uint32_t>& id_to_index);

  std::string config_file_path_;
};

}  // namespace ift::config

#endif  // IFT_CONFIG_SEGMENTER_CONFIG_UTIL_H_