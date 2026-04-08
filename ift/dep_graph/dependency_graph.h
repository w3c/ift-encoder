#ifndef IFT_DEP_GRAPH_DEPENDENCY_GRAPH_H_
#define IFT_DEP_GRAPH_DEPENDENCY_GRAPH_H_

#include <memory>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "hb.h"
#include "ift/common/font_data.h"
#include "ift/common/hb_set_unique_ptr.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/dep_graph/node.h"
#include "ift/dep_graph/traversal.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"

namespace ift::encoder {
class RequestedSegmentationInformation;
}

namespace ift::dep_graph {

// Conditions for an edge in CNF form using node presence as inputs.
// (node_1 or ...) and (node_i or ...)
typedef absl::btree_set<absl::btree_set<Node>> EdgeConditonsCnf;

template <typename CallbackT>
class TraversalContext;

/*
 * Wrapper around harfbuzz's glyph dependency graph API.
 *
 * Allows exploring glyph dependencies within a font.
 */
class DependencyGraph {
 public:
  static absl::StatusOr<DependencyGraph> Create(
      const ift::encoder::RequestedSegmentationInformation* segmentation_info,
      hb_face_t* face) {
    auto full_feature_set = TRY(FullFeatureSet(segmentation_info, face));
    auto init_font_feature_set = TRY(InitFeatureSet(segmentation_info, face));
    hb_depend_t* depend = hb_depend_from_face_or_fail(face);
    if (!depend) {
      return absl::InternalError(
          "Call to hb_depend_from_face_or_fail() failed.");
    }
    return DependencyGraph(segmentation_info, depend, face, full_feature_set);
  }

  // Traverse the full dependency graph (segments, unicodes, and gids), starting
  // at one or more specific starting nodes. Attempts to mimic hb glyph closure
  // and does the traversal in phases by table. Additionally if enforce_context
  // is true, edges will only be traversed when their requirements have been
  // reached.
  //
  // If filter is non-null, then only glyph nodes in that set will be traversed.
  // If filter is null then the filter defaults to the set of non init font
  // glyphs in segmentation info.
  absl::StatusOr<Traversal> ClosureTraversal(
      const ift::common::SegmentSet& start, bool enforce_context = true) const;
  absl::StatusOr<Traversal> ClosureTraversal(
      const absl::btree_set<Node>& nodes,
      const ift::common::GlyphSet* glyph_filter_ptr = nullptr,
      const ift::common::CodepointSet* unicode_filter_ptr = nullptr,
      bool enforce_context = true) const;

  const absl::flat_hash_set<hb_tag_t>& FullFeatureSet() const {
    return full_feature_set_;
  }

  const absl::StatusOr<absl::flat_hash_set<hb_tag_t>> InitFontFeatureSet()
      const {
    return InitFeatureSet(segmentation_info_, original_face_.get());
  }

  // Returns the set of all glyphs that could satisfy the requirements of a
  // pending edge. For contextual substitutions this is the union of all glyphs
  // in all sub-groups.
  absl::StatusOr<ift::common::GlyphSet> RequiredGlyphsFor(
      const PendingEdge& edge) const;

  // Returns a topological sorting of the dependency graph's nodes, excluding
  // any nodes that are in the init font.
  absl::StatusOr<std::vector<Node>> TopologicalSorting() const;

  // Computes the incoming edges for every node in the dependency graph, taking
  // into account all context requirements and implicit dependencies.
  //
  // The return value is a map from each node to each unique edge requirement.
  // Edge requirements are represented as a CNF expression over other nodes.
  absl::StatusOr<absl::flat_hash_map<Node, absl::btree_set<EdgeConditonsCnf>>>
  CollectIncomingEdges() const;

 private:
  DependencyGraph(
      const ift::encoder::RequestedSegmentationInformation* segmentation_info,
      hb_depend_t* depend, hb_face_t* face,
      absl::flat_hash_set<hb_tag_t> full_feature_set)
      : segmentation_info_(segmentation_info),
        original_face_(ift::common::make_hb_face(hb_face_reference(face))),
        full_feature_set_(full_feature_set),
        unicode_to_gid_(UnicodeToGid(face)),
        dependency_graph_(depend, &hb_depend_destroy),
        variation_selector_implied_edges_(ComputeUVSEdges()),
        layout_feature_implied_edges_(ComputeFeatureEdges()),
        context_glyph_implied_edges_(ComputeContextGlyphEdges()) {}

  struct ClosureState {
    std::vector<Node> next{};
    absl::flat_hash_set<Node> visited{};
    Traversal traversal;

    void Visit(Node dest);
    void Visit(Node dest, hb_tag_t table);
    void VisitGsub(Node dest, hb_tag_t feature);
    void VisitContextual(Node dest, hb_tag_t feature,
                         ift::common::GlyphSet context_glyphs);
    void VisitPending(const PendingEdge& edge) {}

    std::optional<Node> GetNext();
    bool Reached(Node node);
    void SetStartNodes(const absl::btree_set<Node>& start);
  };

  absl::StatusOr<Traversal> TraverseGraph(
      TraversalContext<ClosureState>* context) const;

  absl::Status ClosureSubTraversal(
      const TraversalContext<ClosureState>* base_context, hb_tag_t table,
      Traversal& traversal) const;

  template <typename CallbackT>
  absl::Status HandleOutgoingEdges(Node node,
                                   TraversalContext<CallbackT>* context) const;

  template <typename CallbackT>
  absl::Status HandleUnicodeOutgoingEdges(
      hb_codepoint_t unicode, TraversalContext<CallbackT>* context) const;

  template <typename CallbackT>
  absl::Status HandleGlyphOutgoingEdges(
      encoder::glyph_id_t gid, TraversalContext<CallbackT>* context) const;

  template <typename CallbackT>
  absl::Status HandleGsubGlyphOutgoingEdges(
      encoder::glyph_id_t source_gid, encoder::glyph_id_t dest_gid,
      hb_tag_t layout_tag, hb_codepoint_t ligature_set,
      hb_codepoint_t context_set, TraversalContext<CallbackT>* context) const;

  template <typename CallbackT>
  absl::Status HandleFeatureOutgoingEdges(
      hb_tag_t feature_tag, TraversalContext<CallbackT>* context) const;

  template <typename CallbackT>
  void HandleSegmentOutgoingEdges(encoder::segment_index_t id,
                                  TraversalContext<CallbackT>* context) const;

  template <typename CallbackT>
  void HandleSubsetDefinitionOutgoingEdges(
      Node source, const encoder::SubsetDefinition& subset_def,
      TraversalContext<CallbackT>* context) const;

  absl::StatusOr<ift::common::GlyphSet> GetLigaSet(
      hb_codepoint_t liga_set_id) const;

  // Extracts the specific node dependencies required to satisfy a given
  // PendingEdge.
  //
  // Returns a list of requirements as CNF expression on node presence.
  absl::StatusOr<EdgeConditonsCnf> ExtractRequirements(
      const PendingEdge& edge) const;

  static absl::StatusOr<absl::flat_hash_set<hb_tag_t>> FullFeatureSet(
      const ift::encoder::RequestedSegmentationInformation* segmentation_info,
      hb_face_t* face);

  static absl::StatusOr<absl::flat_hash_set<hb_tag_t>> InitFeatureSet(
      const ift::encoder::RequestedSegmentationInformation* segmentation_info,
      hb_face_t* face);

  static absl::flat_hash_map<hb_codepoint_t, encoder::glyph_id_t> UnicodeToGid(
      hb_face_t* face);

  const ift::encoder::RequestedSegmentationInformation* segmentation_info_;
  ift::common::hb_face_unique_ptr original_face_;
  absl::flat_hash_set<hb_tag_t> full_feature_set_;

  absl::flat_hash_map<hb_codepoint_t, encoder::glyph_id_t> unicode_to_gid_;
  std::unique_ptr<hb_depend_t, decltype(&hb_depend_destroy)> dependency_graph_;

  struct VariationSelectorEdge {
    hb_codepoint_t unicode;
    hb_codepoint_t gid;
  };

  struct LayoutFeatureEdge {
    hb_tag_t layout_tag;
    hb_codepoint_t source_gid;
    hb_codepoint_t dest_gid;
    hb_codepoint_t ligature_set;
    hb_codepoint_t context_set;

    bool operator==(const LayoutFeatureEdge& other) const {
      return layout_tag == other.layout_tag && source_gid == other.source_gid &&
             dest_gid == other.dest_gid && ligature_set == other.ligature_set &&
             context_set == other.context_set;
    }

    bool operator<(const LayoutFeatureEdge& other) const {
      if (layout_tag != other.layout_tag) {
        return layout_tag < other.layout_tag;
      }
      if (source_gid != other.source_gid) {
        return source_gid < other.source_gid;
      }
      if (dest_gid != other.dest_gid) {
        return dest_gid < other.dest_gid;
      }
      if (ligature_set != other.ligature_set) {
        return ligature_set < other.ligature_set;
      }
      return context_set < other.context_set;
    }
  };

  absl::flat_hash_map<hb_codepoint_t, std::vector<VariationSelectorEdge>>
  ComputeUVSEdges() const;
  absl::flat_hash_map<hb_tag_t, std::vector<LayoutFeatureEdge>>
  ComputeFeatureEdges() const;
  absl::flat_hash_map<encoder::glyph_id_t, std::vector<LayoutFeatureEdge>>
  ComputeContextGlyphEdges() const;

  absl::flat_hash_map<hb_codepoint_t, std::vector<VariationSelectorEdge>>
      variation_selector_implied_edges_;

  absl::flat_hash_map<hb_tag_t, std::vector<LayoutFeatureEdge>>
      layout_feature_implied_edges_;

  absl::flat_hash_map<encoder::glyph_id_t, std::vector<LayoutFeatureEdge>>
      context_glyph_implied_edges_;

  common::hb_set_unique_ptr scratch_set_ = common::make_hb_set();
  common::hb_set_unique_ptr scratch_set_aux_ = common::make_hb_set();
};

}  // namespace ift::dep_graph

#endif  // IFT_DEP_GRAPH_DEPENDENCY_GRAPH_H_