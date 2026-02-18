#ifndef IFT_DEP_GRAPH_NODE_H_
#define IFT_DEP_GRAPH_NODE_H_

#include <string>

#include "absl/strings/str_cat.h"
#include "ift/encoder/types.h"
#include "hb.h"
#include "common/font_helper.h"

namespace ift::dep_graph {

// A single node in a fonts glyph depedency graph.
class Node {
 public:
  enum NodeType {
    INIT_FONT = 0x01,
    SEGMENT = 0x02,
    UNICODE = 0x04,
    GLYPH = 0x08,
    FEATURE = 0x10,
  };

  static Node InitFont() {
    return Node(0, INIT_FONT);
  }

  static Node Glyph(encoder::glyph_id_t id) {
    return Node(id, GLYPH);
  }

  static Node Unicode(hb_codepoint_t id) {
    return Node(id, UNICODE);
  }

  static Node Segment(encoder::segment_index_t id) {
    return Node(id, SEGMENT);
  }

  static Node Feature(hb_tag_t tag) {
    return Node(tag, FEATURE);
  }

  bool IsUnicode() const { return type_ == UNICODE; }
  bool IsGlyph() const { return type_ == GLYPH; }
  bool IsSegment() const { return type_ == SEGMENT; }
  bool IsInitFont() const { return type_ == INIT_FONT; }
  bool IsFeature() const { return type_ == FEATURE; }
  bool Matches(uint32_t filter) const {
    return filter & type_;
  }

  uint32_t Id() const { return id_; }

  std::string ToString() const {
    switch (type_) {
    case SEGMENT:
        return absl::StrCat("s", id_);
      case UNICODE:
        return absl::StrCat("u", id_);
      case GLYPH:
        return absl::StrCat("g", id_);
      case FEATURE:
        return absl::StrCat(common::FontHelper::ToString(id_));
      default:
        return absl::StrCat("X", id_);
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

}  // namespace ift::dep_graph

#endif  // IFT_DEP_GRAPH_NODE_H_