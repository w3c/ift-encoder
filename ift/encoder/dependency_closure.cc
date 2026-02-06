#include "ift/encoder/dependency_closure.h"

#include "absl/log/log.h"
#include "common/font_helper.h"
#include "common/hb_set_unique_ptr.h"
#include "common/int_set.h"
#include "ift/dep_graph/dependency_graph.h"
#include "ift/dep_graph/node.h"
#include "ift/dep_graph/traversal.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/types.h"

using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::Status;
using absl::StatusOr;
using common::CodepointSet;
using common::GlyphSet;
using common::IntSet;
using common::FontHelper;
using common::SegmentSet;
using common::hb_set_unique_ptr;
using ift::dep_graph::Node;
using ift::dep_graph::DependencyGraph;
using ift::dep_graph::Traversal;

namespace ift::encoder {

DependencyClosure::AnalysisAccuracy DependencyClosure::TraversalAccuracy(const Traversal& traversal) const {
  // TODO(garretrieger): there's several types of depedencies that we do not handle yet and as a result
  // consider inaccurate. Adding support for these will allow the dep graph to be used more widely:
  // - UVS edges. Simple case when all conditions are satisfied, more complex case is generating conjunctive
  //   conditions from them.
  // - Ligatures: at least in simple non-nested cases we should be able to generate the corresponding conditions.
  // - Features: features create conjunctive conditions, we should be able to handle these.

  // TODO XXXX inaccurate if context was not enforced on the traversal.
  // TODO XXXXXX track a set of glyphs which are inscope for an accurate traversal. These are glyphs
  // that are reachable only via non-contextual edges.

  if (traversal.HasPendingEdges()) {
    return AnalysisAccuracy::INACCURATE;
  }

  if (traversal.HasContextGlyphs()) {
    // TODO(garretrieger): it should be possible to support at least liga when all of
    // the liga glyphs have been reached.
    return AnalysisAccuracy::INACCURATE;
  }

  for (hb_tag_t tag : traversal.ContextLayoutFeatures()) {
    if (!init_font_features_.contains(tag)) {
      return AnalysisAccuracy::INACCURATE;
    }
  }

  for (hb_tag_t tag : traversal.ReachedLayoutFeatures()) {
    if (!init_font_features_.contains(tag)) {
      return AnalysisAccuracy::INACCURATE;
    }
  }

  if (traversal.ReachedGlyphs().intersects(context_glyphs_)) {
    return AnalysisAccuracy::INACCURATE;
  }

  return AnalysisAccuracy::ACCURATE;
}

Status DependencyClosure::SegmentsChanged(bool init_font_change, const SegmentSet& segments) {
  VLOG(1) << "DependencyClosure::SegmentsChanged()";

  TRYV(UpdateReachabilityIndex(segments));

  if (!init_font_change && segmentation_info_->SegmentsAreDisjoint()) {
    // If the init font is not changed and all segments are disjoint then there won't be any changes to incoming
    // edge counts as segment modifications will just shift outgoing edges around between segments.
    return absl::OkStatus();
  }

  // TODO(garretrieger): can we do an incremental update of incoming_edge_counts_, and context?
  // not high priority as this does not currently show up as problematic in profiles.
  SegmentSet non_empty_segments;
  for (segment_index_t s = 0; s < segmentation_info_->Segments().size(); s++) {
    if (segmentation_info_->Segments().at(s).Definition().Empty()) {
      continue;
    }
    non_empty_segments.insert(s);
  }

  Traversal traversal = TRY(graph_.ClosureTraversal(non_empty_segments));
  incoming_edge_counts_ = traversal.TraversedIncomingEdgeCounts();

  context_glyphs_ = traversal.ContextGlyphs();

  // The init font may have reachable glyphs which are not in the init font closure,
  // we need to record the context glyphs from these as they are potential interaction
  // points.
  init_font_context_glyphs_.clear();
  init_font_context_features_.clear();
  init_font_features_ = TRY(graph_.InitFontFeatureSet());
  Traversal init_font_traversal = TRY(graph_.TraverseGraph({Node::InitFont()},
    &segmentation_info_->FullClosure(), &segmentation_info_->FullDefinition().codepoints));
  for (const auto& [g, context] : init_font_traversal.ContextPerGlyph()) {
    if (segmentation_info_->NonInitFontGlyphs().contains(g)) {
      context_glyphs_.union_set(context);
      init_font_context_glyphs_.union_set(context);
    }
  }

  for (const auto& [g, context_features] : init_font_traversal.ContextFeaturesPerGlyph()) {
    if (!segmentation_info_->NonInitFontGlyphs().contains(g)) {
      continue;
    }

    for (hb_tag_t f : context_features) {
      if (graph_.FullFeatureSet().contains(f) && !init_font_features_.contains(f)) {
        init_font_context_features_.insert(f);
      }
    }
  }

  init_font_reachable_glyphs_ = init_font_traversal.ReachedGlyphs();
  init_font_reachable_glyphs_.subtract(segmentation_info_->InitFontGlyphs());

  return absl::OkStatus();
}

StatusOr<DependencyClosure::AnalysisAccuracy> DependencyClosure::AnalyzeSegment(
    const common::SegmentSet& segments, GlyphSet& and_gids, GlyphSet& or_gids,
    GlyphSet& exclusive_gids) {

  // This uses a dependency graph (from harfbuzz) to infer how 'segment_id'
  // appears in the activation conditions of any glyphs reachable from it.
  // This aims to have identical output to GlyphClosureCache::AnalyzeSegment()
  // which uses harfbuzz glyph closure to infer conditions.
  //
  // The high level process works like this:
  // 1. Input segment is converted to a list of codepoints, and those to their nominal glyphs.
  // 2. We walk the dependency graph from the nominal glyphs. During traversal edges
  //    are filtered out that are not in the space of all segments (eg. we don't traverse
  //    into the subgraph of the init font)
  // 3. All glyphs encountered during the traversal are categorized into OR, AND, or EXCLUSIVE
  //    based on the details of the traversal.
  //
  // EXCLUSIVE: glyphs that are reachable only from this segment and/or the init font subgraph.
  // OR: Non exclusive glyphs that are reached via disjunctive dependencies, for example glyf components.
  // AND: Non exclusive glyphs that are via via conjunctive dependencies, for example UVS.
  //
  // TODO(garretrieger): This implementation is still early stages and is missing quite a bit,
  // here's a list of some additional things that are needed:
  //
  // - CFF/CFF2 seac components.
  // - preprocess to find the set of VS in the graph, for now disallow segments
  //   that intersect these.
  // - or, just add proper support for UVS handling. These would be treated as conjunctive.
  //   will need to extract the VS codepoints from the graph edges.
  // - Handle simple disjunctive GSUB lookups (may need conjunction with features).
  // - Handle simple conjunctive GSUB lookups (eg. liga)
  // - Handle features in the input segment (once GSUB is supported).
  SegmentSet start_nodes;
  for (segment_index_t segment_id : segments) {
    if (segment_id >= segmentation_info_->Segments().size()) {
      return absl::InvalidArgumentError(absl::StrCat("Segment index ", segment_id, " is out of bounds."));
    }

    const Segment& segment = segmentation_info_->Segments().at(segment_id);
    if (segment.Definition().Empty()) {
      // Empty segments are ignored.
      continue;
    }

    start_nodes.insert(segment_id);
  }

  Traversal traversal = TRY(graph_.ClosureTraversal(start_nodes));
  if (TraversalAccuracy(traversal) == INACCURATE) {
    inaccurate_results_++;
    return INACCURATE;
  }

  btree_set<Node> shared_nodes; // set of nodes which are accessible from outside this subgraph.
  for (const auto& [node, count] : traversal.TraversedIncomingEdgeCounts()) {
    unsigned incoming_edge_count = incoming_edge_counts_.at(node);

    if (node.IsGlyph()) {
      exclusive_gids.insert(node.Id());
    }

    if (count < incoming_edge_count) {
      shared_nodes.insert(node);
    } else if (count != incoming_edge_count) {
      return absl::InternalError(absl::StrCat(
        "Should not happen traversed incoming edge count is greater than "
        "the precomputed incoming edge counts: ", node.ToString(), " = ", count, " > ", incoming_edge_count));
    }
  }

  // We need to find glyphs that are reachable from other segments, which are those
  // glyphs that are reachable from any shared_glyphs found above.
  Traversal all_shared_nodes = TRY(graph_.TraverseGraph(shared_nodes));
  // TODO XXXX this needs fixing with respect to closure which may not have traversed as broadly.
  //      eg. effectively unreachable UVS edges, add a test case to demonstrate the problem
  if (TraversalAccuracy(all_shared_nodes) == INACCURATE) {
    inaccurate_results_++;
    return INACCURATE;
  }

  // Now we can make the glyph condition categorizations
  // any glyphs not in 'shared_glyphs' are only reachable from
  // the iput segment so are exclusive. Everything else is disjunctive.
  for (auto [node, _] : all_shared_nodes.TraversedIncomingEdgeCounts()) {
    if (node.IsGlyph()) {
      or_gids.insert(node.Id());
    }
  }
  exclusive_gids.subtract(or_gids);

  accurate_results_++;
  return ACCURATE;
}

StatusOr<SegmentSet> DependencyClosure::SegmentsThatInteractWith(const GlyphSet& glyphs) {
  TRYV(EnsureReachabilityIndexPopulated());

  // TODO(garretrieger): we can narrow the set by considering context glyphs per activated glyph
  // instead of just the whole set of context glyphs.

  SegmentSet visited_segments;
  GlyphSet visited_glyphs;
  btree_set<hb_tag_t> visited_features;

  GlyphSet to_check = glyphs;
  btree_set<hb_tag_t> features_to_check;

  bool init_font_context_added = false;

  while (!to_check.empty() || !features_to_check.empty()) {

    if (!to_check.empty()) {
      glyph_id_t gid = *to_check.min();
      to_check.erase(gid);
      visited_glyphs.insert(gid);

      // gid may be reachable from the init font.
      if (!init_font_context_added && init_font_reachable_glyphs_.contains(gid)) {
        ReachabilityInitFontAddToCheck(visited_glyphs, visited_features, to_check, features_to_check);
        init_font_context_added = true;
      }

      // now check if any segments can reach it.
      auto segments = segments_that_can_reach_.find(gid);
      if (segments == segments_that_can_reach_.end()) {
        continue;
      }

      TRYV(ReachabilitySegmentsAddToCheck(
        segments->second, visited_segments, visited_glyphs, visited_features, to_check, features_to_check));

    } else if (!features_to_check.empty()) {
      hb_tag_t feature = *features_to_check.begin();
      features_to_check.erase(feature);
      visited_features.insert(feature);

      // now check if any segments can reach it.
      auto segments = segments_that_can_reach_feature_.find(feature);
      if (segments == segments_that_can_reach_feature_.end()) {
        continue;
      }

      TRYV(ReachabilitySegmentsAddToCheck(
        segments->second, visited_segments, visited_glyphs, visited_features, to_check, features_to_check));
    }
  }

  return visited_segments;
}

StatusOr<SegmentSet> DependencyClosure::SegmentInteractionGroup(const SegmentSet& segments) {
  TRYV(EnsureReachabilityIndexPopulated());

  SegmentSet to_check = segments;
  SegmentSet visited;

  SegmentSet init_font_group = InitFontConnections();
  if (segments.intersects(init_font_group)) {
    to_check.union_set(init_font_group);
  }

  while(!to_check.empty()) {
    segment_index_t next = *to_check.begin();
    to_check.erase(next);
    visited.insert(next);

    SegmentSet connected = ConnectedSegments(next);
    connected.subtract(visited);
    to_check.union_set(connected);
  }

  return visited;
}

SegmentSet DependencyClosure::ConnectedSegments(segment_index_t s) {
  // TODO(garretrieger): similar to what we do in SegmentsThatInteractWith we should keep a glyph and features
  // visited sets (we'll need one for context and one for reachable) to avoid unnecessary checks.
  // TODO(garretrieger): a narrower set of connections should be possible if we use context per glyph instead
  // of the full context glyph sets.
  SegmentSet connected;

  for (glyph_id_t gid : glyphs_that_can_be_reached_.at(s)) {
    connected.union_set(segments_that_have_context_glyph_.at(gid));
    connected.union_set(segments_that_can_reach_.at(gid));
  }

  for (glyph_id_t gid : segment_context_glyphs_.at(s)) {
    connected.union_set(segments_that_can_reach_.at(gid));
  }

  for (hb_tag_t tag : features_that_can_be_reached_.at(s)) {
    for (segment_index_t s :segments_that_have_context_feature_.at(tag)) {
      connected.insert(s);
    }
    for (segment_index_t s :segments_that_can_reach_feature_.at(tag)) {
      connected.insert(s);
    }
  }

  for (hb_tag_t tag : segment_context_features_.at(s)) {
    for (segment_index_t s :segments_that_can_reach_feature_.at(tag)) {
      connected.insert(s);
    }
  }

  return connected;
}

SegmentSet DependencyClosure::InitFontConnections() {
  SegmentSet connected;

  for (glyph_id_t gid : init_font_reachable_glyphs_) {
    connected.union_set(segments_that_have_context_glyph_.at(gid));
    connected.union_set(segments_that_can_reach_.at(gid));
  }

  for (glyph_id_t gid : init_font_context_glyphs_) {
    connected.union_set(segments_that_can_reach_.at(gid));
  }

  for (hb_tag_t tag : init_font_features_) {
    for (segment_index_t s :segments_that_have_context_feature_.at(tag)) {
      connected.insert(s);
    }
    for (segment_index_t s :segments_that_can_reach_feature_.at(tag)) {
      connected.insert(s);
    }
  }

  for (hb_tag_t tag : init_font_context_features_) {
    for (segment_index_t s :segments_that_can_reach_feature_.at(tag)) {
      connected.insert(s);
    }
  }

  return connected;
}

void DependencyClosure::ReachabilityInitFontAddToCheck(
  GlyphSet& visited_glyphs, btree_set<hb_tag_t>& visited_features,
  GlyphSet& to_check, btree_set<hb_tag_t>& features_to_check
) const {
  GlyphSet additional = init_font_context_glyphs_;
  additional.subtract(visited_glyphs);
  to_check.union_set(additional);

  for (hb_tag_t f : init_font_context_features_) {
    if (!visited_features.contains(f)) {
      features_to_check.insert(f);
    }
  }
}

Status DependencyClosure::ReachabilitySegmentsAddToCheck(
    const SegmentSet& segments,
    SegmentSet& visited_segments,
    GlyphSet& visited_glyphs,
    btree_set<hb_tag_t>& visited_features,
    GlyphSet& to_check,
    btree_set<hb_tag_t>& features_to_check
) const {
  for (segment_index_t s : segments) {
    if (visited_segments.contains(s)) {
      continue;
    }

    visited_segments.insert(s);
    auto traversal = TRY(graph_.TraverseGraph({Node::Segment(s)}));

    GlyphSet additional = traversal.ContextGlyphs();
    additional.subtract(visited_glyphs);
    to_check.union_set(additional);

    for (hb_tag_t feature : traversal.ContextLayoutFeatures()) {
      if (!visited_features.contains(feature)) {
        features_to_check.insert(feature);
      }
    }
  }
  return absl::OkStatus();
}

Status DependencyClosure::EnsureReachabilityIndexPopulated() {
  if (reachability_index_valid_) {
    return absl::OkStatus();
  }
  return UpdateReachabilityIndex(SegmentSet::all());
}

Status DependencyClosure::UpdateReachabilityIndex(common::SegmentSet segments) {
  if (reachability_index_valid_) {
    // If indices have existing data, then we need to ensure prior entries for the
    // segments to be updated are cleared out.
    for (segment_index_t s : segments) {
      if (s >= segmentation_info_->Segments().size()) {
        break;
      }
      ClearReachabilityIndex(s);
    }
  } else {
    // If the index isn't built yet then all segments need to be updated. Also
    // ensure that records exist for all glyphs and segments. This simplifies
    // code using the index since it can assume records exist.
    segments = SegmentSet::all();
    for (glyph_id_t gid : segmentation_info_->FullClosure()) {
      segments_that_can_reach_.insert(std::pair(gid, SegmentSet()));
      segments_that_have_context_glyph_.insert(std::pair(gid, SegmentSet {}));
    }
    for (hb_tag_t tag : graph_.FullFeatureSet()) {
      segments_that_can_reach_feature_.insert(std::pair(tag, SegmentSet {}));
      segments_that_have_context_feature_.insert(std::pair(tag, SegmentSet {}));
    }
  }

  for (segment_index_t s : segments) {
    if (s >= segmentation_info_->Segments().size()) {
      break;
    }
    TRYV(UpdateReachabilityIndex(s));
  }
  reachability_index_valid_ = true;
  return absl::OkStatus();
}

Status DependencyClosure::UpdateReachabilityIndex(segment_index_t s) {
  glyphs_that_can_be_reached_.insert(std::pair(s, GlyphSet {}));
  segment_context_glyphs_.insert(std::pair(s, GlyphSet {}));
  features_that_can_be_reached_.insert(std::pair(s, btree_set<hb_tag_t> {}));
  segment_context_features_.insert(std::pair(s, btree_set<hb_tag_t> {}));

  auto traversal = TRY(graph_.TraverseGraph(btree_set<Node> {Node::Segment(s)}));

  for (glyph_id_t g : traversal.ReachedGlyphs()) {
    segments_that_can_reach_[g].insert(s);
    glyphs_that_can_be_reached_[s].insert(g);
  }

  for (hb_tag_t f : traversal.ReachedLayoutFeatures()) {
    segments_that_can_reach_feature_[f].insert(s);
    features_that_can_be_reached_[s].insert(f);
  }

  for (glyph_id_t g : traversal.ContextGlyphs()) {
    segments_that_have_context_glyph_[g].insert(s);
    segment_context_glyphs_[s].insert(g);
  }

  for (hb_tag_t f : traversal.ContextLayoutFeatures()) {
    segments_that_have_context_feature_[f].insert(s);
    segment_context_features_[s].insert(f);
  }

  return absl::OkStatus();
}

void DependencyClosure::ClearReachabilityIndex() {
  glyphs_that_can_be_reached_.clear();
  segments_that_can_reach_.clear();
  segments_that_can_reach_feature_.clear();
  features_that_can_be_reached_.clear();

  segments_that_have_context_glyph_.clear();
  segment_context_glyphs_.clear();
  segments_that_have_context_feature_.clear();
  segment_context_features_.clear();
  reachability_index_valid_ = false;
}

void DependencyClosure::ClearReachabilityIndex(segment_index_t segment) {
  auto glyphs = glyphs_that_can_be_reached_.find(segment);
  if (glyphs != glyphs_that_can_be_reached_.end()) {
    for (glyph_id_t gid : glyphs->second) {
      segments_that_can_reach_[gid].erase(segment);
    }
    glyphs->second.clear();
  }

  auto features = features_that_can_be_reached_.find(segment);
  if (features != features_that_can_be_reached_.end()) {
    for (hb_tag_t tag : features->second) {
      segments_that_can_reach_feature_[tag].erase(segment);
    }
    features->second.clear();
  }

  glyphs = segment_context_glyphs_.find(segment);
  if (glyphs != segment_context_glyphs_.end()) {
    for (glyph_id_t gid : glyphs->second) {
      segments_that_have_context_glyph_[gid].erase(segment);
    }
    glyphs->second.clear();
  }

  features = segment_context_features_.find(segment);
  if (features != segment_context_features_.end()) {
    for (hb_tag_t tag : features->second) {
      segments_that_have_context_feature_[tag].erase(segment);
    }
    features->second.clear();
  }
}

}  // namespace ift::encoder