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
#include "ift/encoder/types.h"

namespace ift::encoder {
  class RequestedSegmentationInformation;
}

namespace ift::dep_graph {

/*
 * Wrapper around harfbuzz's glyph depedency graph API.
 *
 * Allows exploring glyph depedencies within a font.
 */
class DependencyGraph {
 // TODO XXXX some basic tests for this class.
 public:
  static absl::StatusOr<DependencyGraph> Create(
      const ift::encoder::RequestedSegmentationInformation* segmentation_info,
      hb_face_t* face) {
    auto full_feature_set = TRY(FullFeatureSet(segmentation_info, face));
    return DependencyGraph(segmentation_info, face, full_feature_set);
  }

  // Traverse the full depedency graph (segments, unicodes, and gids), starting at one or more
  // specific starting nodes.
  absl::StatusOr<Traversal> TraverseGraph(const absl::btree_set<Node>& nodes) const;

 private:

  DependencyGraph(const ift::encoder::RequestedSegmentationInformation* segmentation_info,
                    hb_face_t* face, common::IntSet full_feature_set)
      : segmentation_info_(segmentation_info),
        original_face_(common::make_hb_face(hb_face_reference(face))),
        full_feature_set_(full_feature_set),
        unicode_to_gid_(UnicodeToGid(face)),
        dependency_graph_(hb_depend_from_face(face), &hb_depend_destroy),
        variation_selector_implied_edges_(ComputeUVSEdges()) {}

  bool ShouldFollowEdge(
    hb_tag_t table_tag,
    encoder::glyph_id_t from_gid,
    encoder::glyph_id_t to_gid,
    hb_tag_t feature_tag) const;

  void HandleUnicodeOutgoingEdges(
    hb_codepoint_t unicode,
    std::vector<Node>& next,
    Traversal& traversal
  ) const;

  absl::Status HandleGlyphOutgoingEdges(
    encoder::glyph_id_t gid,
    std::vector<Node>& next,
    Traversal& traversal
  ) const;

  void HandleSegmentOutgoingEdges(
    encoder::segment_index_t id,
    std::vector<Node>& next,
    Traversal& traversal
  ) const;

  absl::StatusOr<common::GlyphSet> GetLigaSet(hb_codepoint_t liga_set_id) const;
  absl::StatusOr<common::GlyphSet> GetContextSet(hb_codepoint_t context_set_id) const;

  static absl::StatusOr<common::IntSet> FullFeatureSet(
      const ift::encoder::RequestedSegmentationInformation* segmentation_info,
      hb_face_t* face);

  static absl::flat_hash_map<hb_codepoint_t, encoder::glyph_id_t> UnicodeToGid(
      hb_face_t* face);

  // TODO store UVS mappings (u -> g).
  const ift::encoder::RequestedSegmentationInformation* segmentation_info_;
  common::hb_face_unique_ptr original_face_;
  common::IntSet full_feature_set_;

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