#ifndef IFT_ENCODER_DEPENDENCY_CLOSURE_H_
#define IFT_ENCODER_DEPENDENCY_CLOSURE_H_

#include <memory>
#include <optional>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "hb.h"
#include "ift/common/data_file_resolver.h"
#include "ift/common/font_data.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"

#include "ift/dep_graph/dependency_graph.h"
#include "ift/dep_graph/node.h"

namespace ift::encoder {

class RequestedSegmentationInformation;

/*
 * Extracts glyph activation conditions and interaction sets using a dependency
 * graph instead of repeated subsetting closure. In some cases may overestimate
 * activation conditions versus real subsetting closure due to reliance on the
 * dependency graph.
 */
class DependencyClosure {
 public:
  static absl::StatusOr<std::unique_ptr<DependencyClosure>> Create(
      const RequestedSegmentationInformation* segmentation_info,
      hb_face_t* face, const ift::common::DataFileResolver& resolver) {
    dep_graph::DependencyGraph graph = TRY(
        dep_graph::DependencyGraph::Create(segmentation_info, face, resolver));
    auto result = std::unique_ptr<DependencyClosure>(
        new DependencyClosure(std::move(graph), segmentation_info, face));

    TRYV(result->InitFontChanged(ift::common::SegmentSet::all()));

    return result;
  }

  // Extracts the full activations conditions (as specified by the dependency
  // graph) for all glyphs. In some cases may overestimate activation conditions
  // versus real subsetting closure due to reliance on the dependency graph.
  const absl::flat_hash_map<glyph_id_t, ActivationCondition>&
  AllGlyphConditions() const {
    return glyph_condition_cache_;
  }

  common::GlyphSet SegmentsToAffectedGlyphs(
      const common::SegmentSet& segments) const;

  absl::flat_hash_set<dep_graph::Node> SegmentsToAffectedNodeConditions(
      const common::SegmentSet& segments) const;

  const ift::common::SegmentSet& InertSegments() const {
    return inert_segments_;
  }

  // This structure caches information derived from the segmentation info
  // segments. These two function signal that segmentation info segments have
  // changed and recomputes the internal cached information.
  //
  // segments is the set of segments whose definition's have been modified
  // by the change in init font definition.
  absl::Status InitFontChanged(const ift::common::SegmentSet& segments);

  absl::Status SegmentsMerged(segment_index_t base_segment,
                              const ift::common::SegmentSet& segments);

  absl::Status SegmentsReset();

  // The collection of SegmentsThatInteractWith(...) methods are used to
  // locate segments that have interactions with unicodes/features/glyphs.
  //
  // Interaction is defined as: given a node n, a segment interacts with
  // n if the segment can influence the presence in the glyph
  // closure of n or any things that depend on n (directly or indirectly).
  //
  // Utilizes the dependency graph to make the determination, so it's possible
  // that the result may be overestimated.
  absl::StatusOr<ift::common::SegmentSet> SegmentsThatInteractWith(
      const common::GlyphSet& glyphs) const;
  absl::StatusOr<ift::common::SegmentSet> SegmentsThatInteractWith(
      const SubsetDefinition& def) const;
  absl::StatusOr<ift::common::SegmentSet> SegmentsThatInteractWith(
      const absl::flat_hash_set<dep_graph::Node> nodes) const;

 private:

  // Extracts the full activations conditions (as specified by the dependency
  // graph) for all graph nodes. In some cases may overestimate activation
  // conditions versus real subsetting closure due to reliance on the dependency
  // graph.
  absl::Status UpdateAllNodeConditions(
      const common::SegmentSet& changed_segments);

  absl::StatusOr<absl::flat_hash_set<dep_graph::Node>>
  ReachableNonInitFontNodes(
      const common::SegmentSet& changed_segments,
      const absl::flat_hash_set<dep_graph::Node>& new_init_nodes) const;

  absl::StatusOr<const absl::flat_hash_map<
      dep_graph::Node, std::vector<dep_graph::EdgeConditionsCnf>>&>
  IncomingEdgesForClosurePhase(
      uint32_t phase, bool full_recalc,
      const absl::flat_hash_set<dep_graph::Node>* node_filter);

  absl::StatusOr<absl::flat_hash_set<dep_graph::Node>> InitFontNodes(
      const absl::flat_hash_set<dep_graph::Node>& previous_init_font_nodes,
      absl::flat_hash_set<dep_graph::Node>& new_nodes) const;

  ift::common::SegmentSet ComputeInertSegments(
      const absl::flat_hash_map<glyph_id_t, ActivationCondition>& conditions)
      const;

  absl::Status InitializeConditions(
      absl::flat_hash_map<dep_graph::Node, ActivationCondition>& conditions,
      const common::SegmentSet& changed_segments,
      const absl::flat_hash_set<dep_graph::Node>& new_init_font_nodes) const;

  static absl::StatusOr<std::optional<ActivationCondition>>
  EdgeConditionsToActivationCondition(
      const dep_graph::EdgeConditionsCnf& edge_conditions,
      const absl::flat_hash_map<dep_graph::Node, ActivationCondition>&
          node_conditions);

  absl::Status PropagateConditions(
      const absl::flat_hash_map<dep_graph::Node,
                                std::vector<dep_graph::EdgeConditionsCnf>>&
          incoming_edges,
      const std::vector<std::vector<dep_graph::Node>>& sccs,
      absl::flat_hash_map<dep_graph::Node, ActivationCondition>& conditions,
      absl::flat_hash_set<dep_graph::Node>& modified) const;

  DependencyClosure(dep_graph::DependencyGraph&& graph,
                    const RequestedSegmentationInformation* segmentation_info,
                    hb_face_t* face)
      : segmentation_info_(segmentation_info),
        original_face_(ift::common::make_hb_face(hb_face_reference(face))),
        graph_(std::move(graph)) {}

  absl::Status InitNodeConditionsCache(
      const common::SegmentSet& changed_segments);
  void UpdateNodeConditionsCache(segment_index_t base_segment,
                                 const common::SegmentSet& segments);

  const RequestedSegmentationInformation* segmentation_info_;
  ift::common::hb_face_unique_ptr original_face_;
  dep_graph::DependencyGraph graph_;

  // Inert segments are segments that:
  // - Appear only in a single unitary condition, that is
  //   they do not interact with any other segments.
  ift::common::SegmentSet inert_segments_;

  absl::flat_hash_map<glyph_id_t, ActivationCondition> glyph_condition_cache_;
  absl::flat_hash_map<dep_graph::Node, ActivationCondition>
      node_condition_cache_;
  absl::flat_hash_map<segment_index_t, absl::flat_hash_set<dep_graph::Node>>
      node_conditions_with_segment_;

  absl::flat_hash_map<dep_graph::Node, ActivationCondition>
      phase_node_condition_cache_
          [dep_graph::DependencyGraph::kNumberOfClosurePhases];

  // Cache for CollectIncomingEdges results for non-cmap phases.
  std::optional<uint32_t> last_seen_full_closure_size_ = std::nullopt;
  absl::flat_hash_map<
      uint32_t,
      std::optional<absl::flat_hash_map<
          dep_graph::Node, std::vector<dep_graph::EdgeConditionsCnf>>>>
      incoming_edges_cache_;

  // Cache of previously processed InitFont nodes, used to determine newly added
  // init items.
  absl::flat_hash_set<dep_graph::Node> init_font_nodes_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_DEPENDENCY_CLOSURE_H_