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
      reachable_glyphs_.insert(dest.Id());
    }
  }

  void Visit(Node dest, hb_tag_t table) {
    Visit(dest);
    tables_.insert(table);
  }

  void VisitGsub(Node dest, hb_tag_t feature) {
    Visit(dest);
    tables_.insert(HB_TAG('G', 'S', 'U', 'B'));
    feature_tags_.insert(feature);
  }

  void VisitUVS(Node dest, hb_codepoint_t a, hb_codepoint_t b) {
    Visit(dest, HB_TAG('c', 'm', 'a', 'p'));
    variation_selectors_.insert(a);
    variation_selectors_.insert(b);
  }

  void VisitContextual(Node dest, hb_tag_t feature, common::GlyphSet context_glyphs) {
    VisitGsub(dest, feature);
    context_glyphs_.union_set(context_glyphs);
    if (dest.IsGlyph()) {
      context_per_glyph_[dest.Id()].union_set(context_glyphs);
    }
  }

  void VisitLigature(Node dest, hb_tag_t feature, common::GlyphSet liga_glyphs) {
    VisitGsub(dest, feature);
    liga_glyphs_.union_set(liga_glyphs);
  }

  const absl::flat_hash_map<Node, uint64_t>& TraversedIncomingEdgeCounts() const {
    return incoming_edges_;
  }

  const absl::flat_hash_set<hb_tag_t>& TraversedTables() const {
    return tables_;
  }

  const absl::flat_hash_set<hb_tag_t>& TraversedLayoutFeatures() const {
    return feature_tags_;
  }

  const common::GlyphSet& RequiredLigaGlyphs() const {
    return liga_glyphs_;
  }

  const common::GlyphSet& ReachableGlyphs() const {
    return reachable_glyphs_;
  }

  const common::GlyphSet& ContextGlyphs() const {
    return context_glyphs_;
  }

  // Map containing the context glyphs relevant to each reachable glyph.
  const absl::flat_hash_map<encoder::glyph_id_t, common::GlyphSet>& ContextPerGlyph() const {
    return context_per_glyph_;
  }

  // Returns true if at least one traversed edge has some sort of extra conditions attached to it.
  // This is any contextual, ligature, or UVS type edge.
  bool HasConditionalGlyphs() const {
    return !context_glyphs_.empty() || !liga_glyphs_.empty() || !variation_selectors_.empty();
  }

  bool HasOnlyLigaConditionalGlyphs() const {
    return !liga_glyphs_.empty() && context_glyphs_.empty() && variation_selectors_.empty();
  }

 private:
  absl::flat_hash_map<Node, uint64_t> incoming_edges_;

  common::GlyphSet reachable_glyphs_;
  common::GlyphSet context_glyphs_;
  absl::flat_hash_map<encoder::glyph_id_t, common::GlyphSet> context_per_glyph_;
  common::GlyphSet liga_glyphs_;

  common::CodepointSet variation_selectors_;

  absl::flat_hash_set<hb_tag_t> feature_tags_;
  absl::flat_hash_set<hb_tag_t> tables_;
};

}  // namespace ift::dep_graph

#endif  // IFT_DEP_GRAPH_TRAVERSAL_H_