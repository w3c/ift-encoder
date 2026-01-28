#ifndef IFT_DEP_GRAPH_TRAVERSAL_H_
#define IFT_DEP_GRAPH_TRAVERSAL_H_

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "common/int_set.h"
#include "ift/dep_graph/node.h"

namespace ift::dep_graph {

// A single node in a fonts glyph depedency graph.
class Traversal {
 public:
  void VisitInitNode(Node node) {
    incoming_edges_.insert(std::make_pair(node, 0));
  }

  void Visit(Node dest) {
    incoming_edges_[dest]++;
    if (dest.IsGlyph()) {
      reached_glyphs_.insert(dest.Id());
    }
  }

  void Visit(Node dest, hb_tag_t table) {
    Visit(dest);
    tables_.insert(table);
  }

  void VisitUVS(Node dest, hb_codepoint_t variation_selector) {
    Visit(dest, HB_TAG('c', 'm', 'a', 'p'));
    variation_selectors_.insert(variation_selector);
  }

  void VisitContextual(Node dest, common::GlyphSet context_glyphs) {
    Visit(dest, HB_TAG('G', 'S', 'U', 'B'));
    context_glyphs_.union_set(context_glyphs);
  }

  void VisitLigature(Node dest, common::GlyphSet liga_glyphs) {
    Visit(dest, HB_TAG('G', 'S', 'U', 'B'));
    liga_glyphs_.union_set(liga_glyphs);
  }

  const absl::flat_hash_map<Node, uint64_t>& TraversedIncomingEdgeCounts() const {
    return incoming_edges_;
  }

  const absl::flat_hash_set<hb_tag_t> TraversedTables() const {
    return tables_;
  }

  const common::GlyphSet& ReachedGlyphs() const {
    return reached_glyphs_;
  }

  const common::GlyphSet& ContextGlyphs() const {
    return context_glyphs_;
  }

  // Returns true if at least one traversed edge has some sort of extra conditions attached to it.
  // This is any contextual, ligature, or UVS type edge.
  bool HasConditionalGlyphs() const {
    return !context_glyphs_.empty() || !liga_glyphs_.empty() || !variation_selectors_.empty();
  }

 private:
  absl::flat_hash_map<Node, uint64_t> incoming_edges_;
  common::GlyphSet reached_glyphs_;
  common::GlyphSet context_glyphs_;
  common::GlyphSet liga_glyphs_;
  common::CodepointSet variation_selectors_;
  absl::flat_hash_set<hb_tag_t> tables_;
};

}  // namespace ift::dep_graph

#endif  // IFT_DEP_GRAPH_TRAVERSAL_H_