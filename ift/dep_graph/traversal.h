#ifndef IFT_DEP_GRAPH_TRAVERSAL_H_
#define IFT_DEP_GRAPH_TRAVERSAL_H_

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "ift/common/int_set.h"
#include "ift/dep_graph/node.h"
#include "ift/dep_graph/pending_edge.h"

namespace ift::dep_graph {

// A single node in a fonts glyph dependency graph.
class Traversal {
 public:
  void SetPendingEdges() { has_pending_edges_ = true; }

  void Merge(const Traversal& other) {
    has_pending_edges_ = has_pending_edges_ | other.has_pending_edges_;

    for (const auto& edge : other.pending_edges_) {
      pending_edges_.push_back(edge);
    }

    for (const auto& [glyph, glyphs] : other.context_per_glyph_) {
      context_per_glyph_[glyph].union_set(glyphs);
    }

    for (const auto& [glyph, features] : other.context_features_per_glyph_) {
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

  void Visit(Node dest) {
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

  void VisitContextual(Node dest, hb_tag_t feature,
                       ift::common::GlyphSet context_glyphs) {
    VisitGsub(dest, feature);
    context_glyphs_.union_set(context_glyphs);
    if (dest.IsGlyph()) {
      context_per_glyph_[dest.Id()].union_set(context_glyphs);
    }
  }

  void AddPending(PendingEdge edge) { pending_edges_.push_back(edge); }

  bool HasPendingEdges() const { return !pending_edges_.empty(); }

  const std::vector<PendingEdge>& PendingEdges() const {
    return pending_edges_;
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

  const ift::common::GlyphSet& ReachedGlyphs() const { return reached_glyphs_; }

  const ift::common::GlyphSet& ContextGlyphs() const { return context_glyphs_; }

  // Map containing the context glyphs relevant to each reachable glyph.
  const absl::flat_hash_map<encoder::glyph_id_t, ift::common::GlyphSet>&
  ContextPerGlyph() const {
    return context_per_glyph_;
  }

  const absl::flat_hash_map<encoder::glyph_id_t, absl::btree_set<hb_tag_t>>&
  ContextFeaturesPerGlyph() const {
    return context_features_per_glyph_;
  }

  // Returns true if at least one traversed edge has some sort of extra
  // conditions attached to it. This is any contextual, ligature, or UVS type
  // edge.
  bool HasContextGlyphs() const { return !context_glyphs_.empty(); }

 private:
  bool has_pending_edges_ = false;
  std::vector<PendingEdge> pending_edges_;

  ift::common::GlyphSet reached_glyphs_;
  ift::common::GlyphSet context_glyphs_;
  absl::flat_hash_map<encoder::glyph_id_t, ift::common::GlyphSet>
      context_per_glyph_;

  absl::flat_hash_map<encoder::glyph_id_t, absl::btree_set<hb_tag_t>>
      context_features_per_glyph_;

  absl::flat_hash_set<hb_tag_t> reached_feature_tags_;
  absl::flat_hash_set<hb_tag_t> context_feature_tags_;
  absl::flat_hash_set<hb_tag_t> tables_;
};

}  // namespace ift::dep_graph

#endif  // IFT_DEP_GRAPH_TRAVERSAL_H_