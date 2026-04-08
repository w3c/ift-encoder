#ifndef IFT_DEP_GRAPH_PENDING_EDGE_H_
#define IFT_DEP_GRAPH_PENDING_EDGE_H_

#include <optional>
#include <utility>

#include "hb.h"
#include "ift/dep_graph/node.h"
#include "ift/encoder/types.h"

namespace ift::dep_graph {

// Tracks an edge who's context requirements are not yet satisfied.
struct PendingEdge {
 public:
  static PendingEdge Disjunctive(Node source, Node dest, hb_tag_t table) {
    PendingEdge edge(source, dest, table);
    return edge;
  }

  static PendingEdge Uvs(hb_codepoint_t a, hb_codepoint_t b,
                         encoder::glyph_id_t gid) {
    PendingEdge edge(Node::Unicode(a), Node::Glyph(gid),
                     HB_TAG('c', 'm', 'a', 'p'));
    edge.required_codepoints = std::make_pair(a, b);
    return edge;
  }

  static PendingEdge Gsub(encoder::glyph_id_t source_gid, hb_tag_t feature,
                          encoder::glyph_id_t dest_gid) {
    PendingEdge edge(Node::Glyph(source_gid), Node::Glyph(dest_gid),
                     HB_TAG('G', 'S', 'U', 'B'));
    edge.required_feature = feature;
    return edge;
  }

  static PendingEdge Ligature(encoder::glyph_id_t source_gid, hb_tag_t feature,
                              encoder::glyph_id_t dest_gid,
                              hb_codepoint_t liga_set_index) {
    PendingEdge edge(Node::Glyph(source_gid), Node::Glyph(dest_gid),
                     HB_TAG('G', 'S', 'U', 'B'));
    edge.required_feature = feature;
    edge.required_liga_set_index = liga_set_index;
    return edge;
  }

  static PendingEdge Context(encoder::glyph_id_t source_gid, hb_tag_t feature,
                             encoder::glyph_id_t dest_gid,
                             hb_codepoint_t context_set_index) {
    PendingEdge edge(Node::Glyph(source_gid), Node::Glyph(dest_gid),
                     HB_TAG('G', 'S', 'U', 'B'));
    edge.required_feature = feature;
    edge.required_context_set_index = context_set_index;
    return edge;
  }

  Node source;
  Node dest;
  hb_tag_t table_tag;

  std::optional<hb_tag_t> required_feature = std::nullopt;
  std::optional<uint32_t> required_liga_set_index = std::nullopt;
  std::optional<uint32_t> required_context_set_index = std::nullopt;
  std::optional<std::pair<hb_codepoint_t, hb_codepoint_t>> required_codepoints =
      std::nullopt;

  bool operator==(const PendingEdge& other) const {
    return dest == other.dest && table_tag == other.table_tag &&
           source == other.source &&
           required_feature == other.required_feature &&
           required_liga_set_index == other.required_liga_set_index &&
           required_context_set_index == other.required_context_set_index &&
           required_codepoints == other.required_codepoints;
  }

  bool operator<(const PendingEdge& other) const {
    if (source != other.source) {
      return source < other.source;
    }
    if (dest != other.dest) {
      return dest < other.dest;
    }
    if (table_tag != other.table_tag) {
      return table_tag < other.table_tag;
    }
    if (required_feature != other.required_feature) {
      return required_feature < other.required_feature;
    }
    if (required_liga_set_index != other.required_liga_set_index) {
      return required_liga_set_index < other.required_liga_set_index;
    }
    if (required_context_set_index != other.required_context_set_index) {
      return required_context_set_index < other.required_context_set_index;
    }
    return required_codepoints < other.required_codepoints;
  }

  template <typename H>
  friend H AbslHashValue(H h, const PendingEdge& e) {
    return H::combine(std::move(h), e.source, e.dest, e.table_tag,
                      e.required_feature, e.required_liga_set_index,
                      e.required_context_set_index, e.required_codepoints);
  }

 private:
  PendingEdge(Node source_, Node dest_, hb_tag_t table_tag_)
      : source(source_), dest(dest_), table_tag(table_tag_) {}
};

}  // namespace ift::dep_graph

#endif  // IFT_DEP_GRAPH_PENDING_EDGE_H_
