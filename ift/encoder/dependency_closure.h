#ifndef IFT_ENCODER_DEPENDENCY_CLOSURE_H_
#define IFT_ENCODER_DEPENDENCY_CLOSURE_H_

#include <memory>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "common/font_data.h"
#include "common/int_set.h"
#include "common/try.h"
#include "hb.h"
#include "ift/encoder/types.h"

namespace ift::encoder {

class RequestedSegmentationInformation;

/*
 * A cache of the results of glyph closure on a specific font face.
 */
class DependencyClosure {
 public:
  static absl::StatusOr<std::unique_ptr<DependencyClosure>> Create(
      const RequestedSegmentationInformation* segmentation_info,
      hb_face_t* face) {
    auto full_feature_set = TRY(FullFeatureSet(segmentation_info, face));
    return std::unique_ptr<DependencyClosure>(new DependencyClosure(segmentation_info, face, full_feature_set));
  }

  // Attempts to analyze the given segment using a glyph dependency graph
  // from harfbuzz. Returns true if a accurate analysis is possible, otherwise
  // false.
  //
  // When false is returned GlyphClosureCache should be used instead to analyze
  // the segment.
  //
  // If true is returned then the input sets *_gids will have glyphs appended to
  // them based on the analysis classification.
  //
  // TODO XXXX explain the meaning behind the three gid sets.
  absl::StatusOr<bool> AnalyzeSegment(segment_index_t segment_id,
                      common::GlyphSet& and_gids,
                      common::GlyphSet& or_gids,
                      common::GlyphSet& exclusive_gids) const;

 private:
  enum NodeType {
    SEGMENT,
    UNICODE,
    GLYPH,
  };

  class Node {
   public:
    static Node Glyph(glyph_id_t id) {
      return Node(id, GLYPH);
    }

    static Node Unicode(hb_codepoint_t id) {
      return Node(id, UNICODE);
    }

    static Node Segment(segment_index_t id) {
      return Node(id, SEGMENT);
    }

    bool IsUnicode() const { return type_ == UNICODE; };
    bool IsGlyph() const { return type_ == GLYPH; };
    bool IsSegment() const { return type_ == SEGMENT; };
    uint32_t Id() const { return id_; }

    std::string ToString() const {
      switch (type_) {
      case SEGMENT:
        return absl::StrCat("s", id_);
      case UNICODE:
        return absl::StrCat("u", id_);
      case GLYPH:
      default:
        return absl::StrCat("g", id_);
      }
    }

    bool operator<(const Node& other) const {
      if (type_ != other.type_) {
        return type_ < other.type_;
      }
      return id_ < other.id_;
    }

    bool operator==(const Node& other) const {
      return id_ == other.id_ && type_ == other.type_;
    }

    bool operator!=(const Node& other) const {
      return !(*this == other);
    }

    template <typename H>
    friend H AbslHashValue(H h, const Node& n) {
      return H::combine(std::move(h), n.id_, n.type_);
    }

   private:
    Node(uint32_t id, NodeType type) : id_(id), type_(type) {}
    uint32_t id_;
    NodeType type_;
  };

  DependencyClosure(const RequestedSegmentationInformation* segmentation_info,
                    hb_face_t* face, common::IntSet full_feature_set)
      : segmentation_info_(segmentation_info),
        original_face_(common::make_hb_face(hb_face_reference(face))),
        full_feature_set_(full_feature_set),
        unicode_to_gid_(UnicodeToGid(face)),
        dependency_graph_(hb_depend_from_face(face), &hb_depend_destroy),
        incoming_edge_count_() {
    incoming_edge_count_ = ComputeIncomingEdgeCount();
  }

  bool ShouldFollowEdge(
    hb_tag_t table_tag,
    glyph_id_t from_gid,
    glyph_id_t to_gid,
    hb_tag_t feature_tag) const;

  // Traverse the full depedency graph (segments, unicodes, and gids), starting at one or more
  // specific unicode values.
  bool TraverseGraph(const absl::btree_set<Node>& nodes,
                     absl::flat_hash_map<Node, unsigned>& traversed_edges) const;

  // Traverse the glyph only portion of the dependency graph.
  bool TraverseGlyphGraph(const common::GlyphSet& glyphs,
                          absl::flat_hash_map<Node, unsigned>& traversed_edges) const;

  bool HandleUnicodeOutgoingEdges(
    hb_codepoint_t unicode,
    std::vector<Node>& next,
    absl::flat_hash_map<Node, unsigned>& traversed_edges
  ) const;

  bool HandleGlyphOutgoingEdges(
    glyph_id_t gid,
    std::vector<Node>& next,
    absl::flat_hash_map<Node, unsigned>& traversed_edges
  ) const;

  bool HandleSegmentOutgoingEdges(
    segment_index_t id,
    std::vector<Node>& next,
    absl::flat_hash_map<Node, unsigned>& traversed_edges
  ) const;

  static absl::StatusOr<common::IntSet> FullFeatureSet(
      const RequestedSegmentationInformation* segmentation_info,
      hb_face_t* face);

  absl::flat_hash_map<Node, glyph_id_t> ComputeIncomingEdgeCount() const;

  static absl::flat_hash_map<hb_codepoint_t, glyph_id_t> UnicodeToGid(
      hb_face_t* face);

  const RequestedSegmentationInformation* segmentation_info_;
  common::hb_face_unique_ptr original_face_;
  common::IntSet full_feature_set_;

  absl::flat_hash_map<hb_codepoint_t, glyph_id_t> unicode_to_gid_;
  std::unique_ptr<hb_depend_t, decltype(&hb_depend_destroy)> dependency_graph_;
  absl::flat_hash_map<Node, unsigned> incoming_edge_count_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_DEPENDENCY_CLOSURE_H_