#ifndef IFT_DEP_GRAPH_TRAVERSAL_H_
#define IFT_DEP_GRAPH_TRAVERSAL_H_

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "common/int_set.h"
#include "ift/dep_graph/node.h"

namespace ift::dep_graph {

// A single node in a fonts glyph depedency graph.
class Traversal {
 public:
  void SetPendingEdges() {
    has_pending_edges_ = true;
  }

  void Merge(const Traversal& other) {
    has_pending_edges_ = has_pending_edges_ | other.has_pending_edges_;

    for (const auto& [node, count] : other.incoming_edges_) {
      incoming_edges_[node] += count;
    }

    for (const auto& [glyph, glyphs] : other.context_per_glyph_) {
      context_per_glyph_[glyph].union_set(glyphs);
    }

    for (const auto& [glyph, features] : other.context_per_glyph_) {
      for (hb_tag_t f : features) {
        context_features_per_glyph_[glyph].insert(f);
      }
    }

    for (hb_tag_t f : other.reached_feature_tags_) {
      reached_feature_tags_.insert(f);
    }

    for (hb_tag_t f : other.context_feature_tags_) {
      context_feature_tags_.insert(f);
    }

    for (hb_tag_t t : other.tables_) {
      tables_.insert(t);
    }

    reached_glyphs_.union_set(other.reached_glyphs_);
    context_glyphs_.union_set(other.context_glyphs_);
  }

  void VisitInitNode(Node node) {
    incoming_edges_.insert(std::make_pair(node, 0));
  }

  void Visit(Node dest) {
    incoming_edges_[dest]++;
    if (dest.IsGlyph()) {
      reached_glyphs_.insert(dest.Id());
    }
    if (dest.IsFeature()) {
      reached_feature_tags_.insert(dest.Id());
    }
  }

  void Visit(Node dest, hb_tag_t table) {
    Visit(dest);
    tables_.insert(table);
  }

  void VisitGsub(Node dest, hb_tag_t feature) {
    Visit(dest);
    tables_.insert(HB_TAG('G', 'S', 'U', 'B'));
    context_feature_tags_.insert(feature);
    if (dest.IsGlyph()) {
      context_features_per_glyph_[dest.Id()].insert(feature);
    }
  }

  void VisitContextual(Node dest, hb_tag_t feature, common::GlyphSet context_glyphs) {
    VisitGsub(dest, feature);
    context_glyphs_.union_set(context_glyphs);
    if (dest.IsGlyph()) {
      context_per_glyph_[dest.Id()].union_set(context_glyphs);
    }
  }

  bool HasPendingEdges() const {
    return has_pending_edges_;
  }

  const absl::flat_hash_map<Node, uint64_t>& TraversedIncomingEdgeCounts() const {
    return incoming_edges_;
  }

  const absl::flat_hash_set<hb_tag_t>& TraversedTables() const {
    return tables_;
  }

  const absl::flat_hash_set<hb_tag_t>& ReachedLayoutFeatures() const {
    return reached_feature_tags_;
  }

  const absl::flat_hash_set<hb_tag_t>& ContextLayoutFeatures() const {
    return context_feature_tags_;
  }

  const common::GlyphSet& ReachedGlyphs() const {
    return reached_glyphs_;
  }

  const common::GlyphSet& ContextGlyphs() const {
    return context_glyphs_;
  }

  // Map containing the context glyphs relevant to each reachable glyph.
  const absl::flat_hash_map<encoder::glyph_id_t, common::GlyphSet>& ContextPerGlyph() const {
    return context_per_glyph_;
  }

  const absl::flat_hash_map<encoder::glyph_id_t, absl::btree_set<hb_tag_t>>& ContextFeaturesPerGlyph() const {
    return context_features_per_glyph_;
  }

  // Returns true if at least one traversed edge has some sort of extra conditions attached to it.
  // This is any contextual, ligature, or UVS type edge.
  bool HasContextGlyphs() const {
    return !context_glyphs_.empty();
  }

 private:
  // TODO XXX add flag for presence of pending edges.
  // TODO XXX remove edge specific Visit* methods, have the complete context sets
  // be passed at the end of traversal. Possibly same for incoming edges?
  absl::flat_hash_map<Node, uint64_t> incoming_edges_;

  bool has_pending_edges_ = false;
  // TODO XXXX should we track if context was enforced?
  common::GlyphSet reached_glyphs_;
  common::GlyphSet context_glyphs_;
  absl::flat_hash_map<encoder::glyph_id_t, common::GlyphSet> context_per_glyph_;
  absl::flat_hash_map<encoder::glyph_id_t, absl::btree_set<hb_tag_t>> context_features_per_glyph_;

  absl::flat_hash_set<hb_tag_t> reached_feature_tags_;
  absl::flat_hash_set<hb_tag_t> context_feature_tags_;
  absl::flat_hash_set<hb_tag_t> tables_;
  // TODO XXXX review all fields and see what we can remove
};

}  // namespace ift::dep_graph

#endif  // IFT_DEP_GRAPH_TRAVERSAL_H_