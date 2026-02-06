#ifndef IFT_DEP_GRAPH_DEPENDENCY_GRAPH_H_
#define IFT_DEP_GRAPH_DEPENDENCY_GRAPH_H_

#include <memory>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"

#include "common/font_data.h"
#include "common/int_set.h"
#include "common/try.h"

#include "hb.h"

#include "ift/dep_graph/node.h"
#include "ift/dep_graph/traversal.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"

namespace ift::encoder {
  class RequestedSegmentationInformation;
}

namespace ift::dep_graph {

class TraversalContext;

/*
 * Wrapper around harfbuzz's glyph depedency graph API.
 *
 * Allows exploring glyph depedencies within a font.
 */
class DependencyGraph {
 public:
  static absl::StatusOr<DependencyGraph> Create(
      const ift::encoder::RequestedSegmentationInformation* segmentation_info,
      hb_face_t* face) {
    auto full_feature_set = TRY(FullFeatureSet(segmentation_info, face));
    auto init_font_feature_set = TRY(InitFeatureSet(segmentation_info, face));
    return DependencyGraph(segmentation_info, face, full_feature_set);
  }

  // Traverse the full depedency graph (segments, unicodes, and gids), starting at one or more
  // specific starting nodes. All edges are traversed regardless of whether their context conditions
  // are satisfied or not.
  //
  // If filter is non-null, then only glyph nodes in that set will be traversed. If filter is
  // null then the filter defaults to the set of non init font glyphs in segmentation info.
  absl::StatusOr<Traversal> TraverseGraph(
    const absl::btree_set<Node>& nodes,
    const common::GlyphSet* glyph_filter_ptr = nullptr,
    const common::CodepointSet* unicode_filter_ptr = nullptr
  ) const;

  // Traverse the full dependency graph (segments, unicodes, and gids), starting at one or more
  // specific starting nodes. Unlike TraverseGraph this respects context conditions and won't
  // traverse and edge until it's conditions are satisfied. This attempts to mimic as closely
  // as possible the behaviour of harfbuzz's closure operation. However, the result is not always
  // gauranteed to match.
  //
  // TODO return value to signal accuracy.
  absl::StatusOr<Traversal> ClosureTraversal(const common::SegmentSet& start) const;

  const absl::flat_hash_set<hb_tag_t>& FullFeatureSet() const {
    return full_feature_set_;
  }

  const absl::StatusOr<absl::flat_hash_set<hb_tag_t>> InitFontFeatureSet() const {
    return InitFeatureSet(segmentation_info_, original_face_.get());
  }

 private:

  DependencyGraph(const ift::encoder::RequestedSegmentationInformation* segmentation_info,
                    hb_face_t* face, absl::flat_hash_set<hb_tag_t> full_feature_set)
      : segmentation_info_(segmentation_info),
        original_face_(common::make_hb_face(hb_face_reference(face))),
        full_feature_set_(full_feature_set),
        unicode_to_gid_(UnicodeToGid(face)),
        dependency_graph_(hb_depend_from_face(face), &hb_depend_destroy),
        variation_selector_implied_edges_(ComputeUVSEdges()) {}

  absl::StatusOr<Traversal> TraverseGraph(
    TraversalContext* context
  ) const;

  void HandleUnicodeOutgoingEdges(
    hb_codepoint_t unicode,
    TraversalContext* context
  ) const;

  absl::Status HandleGlyphOutgoingEdges(
    encoder::glyph_id_t gid,
    TraversalContext* context
  ) const;

  void HandleSegmentOutgoingEdges(
    encoder::segment_index_t id,
    TraversalContext* context
  ) const;

  void HandleSubsetDefinitionOutgoingEdges(
    const encoder::SubsetDefinition& subset_def,
    TraversalContext* context
  ) const;

  absl::StatusOr<common::GlyphSet> GetLigaSet(hb_codepoint_t liga_set_id) const;
  absl::StatusOr<common::GlyphSet> GetContextSet(hb_codepoint_t context_set_id) const;

  static absl::StatusOr<absl::flat_hash_set<hb_tag_t>> FullFeatureSet(
      const ift::encoder::RequestedSegmentationInformation* segmentation_info,
      hb_face_t* face);

  static absl::StatusOr<absl::flat_hash_set<hb_tag_t>> InitFeatureSet(
    const ift::encoder::RequestedSegmentationInformation* segmentation_info,
    hb_face_t* face);

  static absl::flat_hash_map<hb_codepoint_t, encoder::glyph_id_t> UnicodeToGid(
      hb_face_t* face);

  const ift::encoder::RequestedSegmentationInformation* segmentation_info_;
  common::hb_face_unique_ptr original_face_;
  absl::flat_hash_set<hb_tag_t> full_feature_set_;

  absl::flat_hash_map<hb_codepoint_t, encoder::glyph_id_t> unicode_to_gid_;
  std::unique_ptr<hb_depend_t, decltype(&hb_depend_destroy)> dependency_graph_;

  struct VariationSelectorEdge {
    hb_codepoint_t unicode;
    hb_codepoint_t gid;
  };

  absl::flat_hash_map<hb_codepoint_t, std::vector<VariationSelectorEdge>> ComputeUVSEdges() const;

  absl::flat_hash_map<hb_codepoint_t, std::vector<VariationSelectorEdge>> variation_selector_implied_edges_;
};

}  // namespace ift::dep_graph

#endif  // IFT_DEP_GRAPH_DEPENDENCY_GRAPH_H_