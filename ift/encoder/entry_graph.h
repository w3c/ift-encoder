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

// TODO(garretrieger): better optimize patch id encodings by ordering entries by
// patch ids where possible.

struct EntryNode {
  common::CodepointSet and_codepoints;
  absl::flat_hash_set<hb_tag_t> and_features;
  absl::flat_hash_map<hb_tag_t, ift::common::AxisRange> and_design_space;

  ChildMode child_mode = NONE;
  common::IntSet children_ids;

  // Generate an estimated encoding cost, ignores the impact of last patch index
  // and the default format selection on final encoding size.
  absl::StatusOr<int64_t> EncodingCost() const;
};

struct SubsumptionResult {
  int64_t cost_delta = 0;  // negative indicates savings
  common::IntSet subsumed_children;

  // The final sets to be applied to the node or instantiated as new children.
  std::optional<common::CodepointSet> codepoints;
  std::optional<absl::flat_hash_set<hb_tag_t>> features;
  std::optional<absl::flat_hash_map<hb_tag_t, ift::common::AxisRange>>
      design_space;
};

// Models the condition graph formed by the patch map entries of an IFT
// table. Can be used to construct the corresponding entry list.
class EntryGraph {
 public:
  // Create a new graph based on 'conditions' and 'segments'. Will utilize
  // maximum sharing by default, and leave each segment as an individual node.
  static absl::StatusOr<EntryGraph> Create(
      absl::Span<const ActivationCondition> conditions,
      const absl::flat_hash_map<segment_index_t, SubsetDefinition>& segments);

  // Modify this entry graph where possible to reduce the overall encoding cost
  // without changing it's functionality.
  void Optimize();

  // Returns the cost delta of subsuming child nodes for 'node_id'.
  absl::StatusOr<SubsumptionResult> CalculateSubsumptionCostDelta(
      uint32_t node_id) const;

  // Actuates the subsumption for 'node_id' based on 'result'.
  absl::Status ActuateSubsumption(uint32_t node_id,
                                  const SubsumptionResult& result);

  // Convert this entry graph into an list of patch map entries, with child
  // indices fully resolved.
  absl::StatusOr<std::vector<proto::PatchMap::Entry>> ToPatchMapEntries(
      proto::PatchEncoding default_encoding) const;

  // Return a topological sorting of node ids for this graph.
  absl::StatusOr<std::vector<uint32_t>> TopologicalSort() const;

 private:
  EntryGraph() = default;

  absl::StatusOr<SubsumptionResult> CalculateDisjunctiveSubsumption(
      uint32_t node_id) const;
  absl::StatusOr<SubsumptionResult> CalculateConjunctiveSubsumption(
      uint32_t node_id) const;

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

  common::IntSet ReachableNodes() const;

  absl::Status AddChildrenToNode(uint32_t node_id, ChildMode mode,
                                 absl::Span<const uint32_t> children_ids);

  absl::flat_hash_map<ActivationCondition, std::vector<uint32_t>>
      patch_mappings;
  absl::flat_hash_map<ActivationCondition, proto::PatchEncoding>
      patch_encodings;

  std::vector<EntryNode> nodes;
  std::vector<uint32_t> incoming_edge_count;
  // TODO(garretrieger): track which nodes are fully disjunctive after being
  // fully resolved (ie. including children)

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