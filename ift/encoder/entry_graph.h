#ifndef IFT_ENCODER_ENTRY_GRAPH_H_
#define IFT_ENCODER_ENTRY_GRAPH_H_

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "ift/common/int_set.h"
#include "ift/encoder/activation_condition.h"
#include "ift/proto/patch_encoding.h"
#include "ift/proto/patch_map.h"

namespace ift::encoder {

enum ChildMode {
  NONE,
  AND,
  OR,
};

// TODO(garretrieger): better optimize patch id encodings by ordering entries by patch ids
// where possible.

struct EntryNode {
  uint32_t id;
  common::CodepointSet and_codepoints;
  absl::flat_hash_set<hb_tag_t> and_features;
  absl::flat_hash_map<hb_tag_t, ift::common::AxisRange> and_design_space;

  ChildMode child_mode;
  common::IntSet children_ids;

  int64_t EncodingCost() const;
};

class EntryGraph {
 public:
  static absl::StatusOr<EntryGraph> Create(
      absl::Span<const ActivationCondition> conditions,
      const absl::flat_hash_map<segment_index_t, SubsetDefinition>& segments);

  void Optimize();

  absl::StatusOr<std::vector<proto::PatchMap::Entry>> ToPatchMapEntries(
      proto::PatchEncoding default_encoding) const;

  absl::StatusOr<std::vector<uint32_t>> TopologicalSort() const;

 private:
  EntryGraph() = default;

  // Returns node id. De-dups if possible.
  absl::StatusOr<uint32_t> AddMapping(
      const ActivationCondition& condition,
      const absl::flat_hash_map<segment_index_t, SubsetDefinition>& segments);

  // Returns node id. De-dups if possible.
  absl::StatusOr<uint32_t> AddCodepointsOnly(
      const common::CodepointSet& codepoints);

  // Returns node id. De-dups if possible.
  absl::StatusOr<uint32_t> AddFeaturesOnly(
      const absl::btree_set<hb_tag_t>& features);

  // Returns node id. De-dups if possible.
  absl::StatusOr<uint32_t> AddDesignSpaceOnly(
      const absl::flat_hash_map<hb_tag_t, ift::common::AxisRange>&
          design_space);

  absl::StatusOr<uint32_t> CreateNode();

  absl::Status AddChildrenToNode(
      uint32_t node_id, ChildMode mode,
      absl::Span<const uint32_t> children_ids);

  absl::flat_hash_map<ActivationCondition, std::vector<uint32_t>>
      patch_mappings;
  absl::flat_hash_map<ActivationCondition, proto::PatchEncoding>
      patch_encodings;

  std::vector<EntryNode> nodes;
  std::vector<uint32_t> incoming_edge_count;
  // TODO(garretrieger): track which nodes are fully disjunctive after being fully
  // resolved (ie. including children)

  absl::flat_hash_map<ActivationCondition, uint32_t> condition_to_node_id;
  absl::flat_hash_map<common::CodepointSet, uint32_t> codepoints_to_node_id;
  absl::flat_hash_map<absl::flat_hash_set<hb_tag_t>, uint32_t>
      features_to_node_id;
  absl::flat_hash_map<absl::flat_hash_map<hb_tag_t, ift::common::AxisRange>,
                      uint32_t>
      design_space_to_node_id;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_ENTRY_GRAPH_H_