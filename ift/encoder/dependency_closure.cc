#include "ift/encoder/dependency_closure.h"

#include "absl/log/log.h"
#include "ift/common/font_helper.h"
#include "ift/common/hb_set_unique_ptr.h"
#include "ift/common/int_set.h"
#include "ift/dep_graph/dependency_graph.h"
#include "ift/dep_graph/node.h"
#include "ift/dep_graph/pending_edge.h"
#include "ift/dep_graph/traversal.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/types.h"

using ift::config::Features;

using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::Status;
using absl::StatusOr;
using ift::common::CodepointSet;
using ift::common::FontHelper;
using ift::common::GlyphSet;
using ift::common::hb_set_unique_ptr;
using ift::common::IntSet;
using ift::common::SegmentSet;
using ift::dep_graph::DependencyGraph;
using ift::dep_graph::Node;
using ift::dep_graph::PendingEdge;
using ift::dep_graph::Traversal;

namespace ift::encoder {

DependencyClosure::AnalysisAccuracy DependencyClosure::TraversalAccuracy(
    const Traversal& traversal) const {
  // TODO(garretrieger): there's several types of dependencies that we do not
  // handle yet and as a result consider inaccurate. Adding support for these
  // will allow the dep graph to be used more widely:
  // - UVS edges: more complex case is generating conjunctive conditions from
  // them.
  // - Ligatures: at least in simple non-nested cases we should be able to
  // generate the corresponding conditions.
  // - Features: features create conjunctive conditions, we should be able to
  // handle these.

  if (traversal.HasPendingEdges()) {
    // pending edges means there is conjunction.
    return AnalysisAccuracy::INACCURATE;
  }

  if (traversal.HasContextGlyphs()) {
    // avoid all contextual edges for accurate analysis, these have complex
    // interactions.
    return AnalysisAccuracy::INACCURATE;
  }

  if (traversal.ReachedGlyphs().intersects(context_glyphs_)) {
    // A glyph which appears in a context may have complicated interactions with
    // other segments that aren't captured by the direct traversal.
    return AnalysisAccuracy::INACCURATE;
  }

  return AnalysisAccuracy::ACCURATE;
}

Status DependencyClosure::SegmentsChanged(bool init_font_change,
                                          const SegmentSet& segments) {
  VLOG(1) << "DependencyClosure::SegmentsChanged()";

  if (!init_font_change && segmentation_info_->SegmentsAreDisjoint()) {
    // If the init font is not changed and all segments are disjoint then there
    // won't be any changes to incoming edge counts as segment modifications
    // will just shift outgoing edges around between segments.
    TRYV(UpdateReachabilityIndex(segments));
    return absl::OkStatus();
  }

  // TODO(garretrieger): can we do an incremental update of
  // incoming_edge_counts_, and context? not high priority as this does not
  // currently show up as problematic in profiles.
  SegmentSet start_segments;
  for (segment_index_t s = 0; s < segmentation_info_->Segments().size(); s++) {
    if (segmentation_info_->Segments().at(s).Definition().Empty()) {
      continue;
    }
    start_segments.insert(s);
  }

  Traversal traversal = TRY(graph_.ClosureTraversal(start_segments, false));
  context_glyphs_ = traversal.ContextGlyphs();

  // The init font may have reachable glyphs which are not in the init font
  // closure, we need to record the context glyphs from these as they are
  // potential interaction points.
  init_font_context_glyphs_.clear();
  init_font_context_features_.clear();
  init_font_features_ = TRY(graph_.InitFontFeatureSet());
  Traversal init_font_traversal = TRY(graph_.ClosureTraversal(
      {Node::InitFont()}, &segmentation_info_->FullClosure(),
      &segmentation_info_->FullDefinition().codepoints, false));
  for (const auto& [g, context] : init_font_traversal.ContextPerGlyph()) {
    if (segmentation_info_->NonInitFontGlyphs().contains(g)) {
      context_glyphs_.union_set(context);
      init_font_context_glyphs_.union_set(context);
    }
  }

  for (const auto& [g, context_features] :
       init_font_traversal.ContextFeaturesPerGlyph()) {
    if (!segmentation_info_->NonInitFontGlyphs().contains(g)) {
      continue;
    }

    for (hb_tag_t f : context_features) {
      if (graph_.FullFeatureSet().contains(f) &&
          !init_font_features_.contains(f)) {
        init_font_context_features_.insert(f);
      }
    }
  }

  init_font_reachable_glyphs_ = init_font_traversal.ReachedGlyphs();
  init_font_reachable_glyphs_.subtract(segmentation_info_->InitFontGlyphs());

  // Note: reachability index must be updated after context_glyphs_, init_font_*
  // sets are populated. since it utilizes those to assess traversal accuracy.
  TRYV(UpdateReachabilityIndex(segments));

  return absl::OkStatus();
}

StatusOr<DependencyClosure::AnalysisAccuracy> DependencyClosure::AnalyzeSegment(
    const SegmentSet& segments, GlyphSet& and_gids, GlyphSet& or_gids,
    GlyphSet& exclusive_gids) {
  AnalysisResult result = TRY(AnalyzeSegmentInternal(segments));
  if (result.accuracy == INACCURATE) {
    inaccurate_results_++;
    return INACCURATE;
  }

  and_gids.union_set(result.and_gids);
  or_gids.union_set(result.or_gids);
  exclusive_gids.union_set(result.exclusive_gids);

  accurate_results_++;
  return ACCURATE;
}

// Filters out invalid and empty segment ids.
StatusOr<SegmentSet> DependencyClosure::FilterSegments(
    const SegmentSet& segments) const {
  SegmentSet out;
  for (segment_index_t segment_id : segments) {
    if (segment_id >= segmentation_info_->Segments().size()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Segment index ", segment_id, " is out of bounds."));
    }

    if (segmentation_info_->Segments().at(segment_id).Definition().Empty()) {
      // Empty segments are ignored.
      continue;
    }

    out.insert(segment_id);
  }
  return out;
}

StatusOr<DependencyClosure::AnalysisResult>
DependencyClosure::AnalyzeSegmentInternal(const SegmentSet& segments) const {
  // This uses a dependency graph (from harfbuzz) to infer how 'segment_id'
  // appears in the activation conditions of any glyphs reachable from it.
  // This aims to have identical output to GlyphClosureCache::AnalyzeSegment()
  // which uses harfbuzz glyph closure to infer conditions.
  //
  // Condition analysis using the dep graph is not always able to accurately
  // reproduce the closure based conditions, so this method returns an
  // indication of the accuracy of the analysis. AnalysisAccuracy::INACCURATE is
  // returned when something in the dep graph is encountered which may cause
  // results to diverge from the closure approach.

  flat_hash_map<glyph_id_t, SegmentSet> conditions_for_glyph;
  AnalysisResult result;
  result.accuracy = ACCURATE;

  SegmentSet start_nodes = TRY(FilterSegments(segments));

  /* ### Phase 1: Figure out what glyphs we can reach and what segments are
   * needed to reach them. ### */
  if (start_nodes.size() == 1 &&
      isolated_reachability_is_accurate_.contains(*start_nodes.begin())) {
    // Special case, there's only one segment and it doesn't contain any
    // conjunction, we can skip a more complicated analysis and get everything
    // we need from the isolated reachability index.
    segment_index_t segment_id = *start_nodes.begin();
    result.reached_glyphs = isolated_reachability_.GlyphsForSegment(segment_id);
    for (glyph_id_t g : result.reached_glyphs) {
      conditions_for_glyph[g] = {segment_id};
    }
  } else {
    Traversal traversal = TRY(graph_.ClosureTraversal(start_nodes, false));

    result.accuracy = TRY(ConjunctiveConditionDiscovery(
        start_nodes, traversal.ReachedGlyphs(), conditions_for_glyph));
    for (const auto& [g, _] : conditions_for_glyph) {
      result.reached_glyphs.insert(g);
    }

    if (TraversalAccuracy(traversal) == INACCURATE ||
        traversal.ReachedGlyphs() != result.reached_glyphs) {
      result.accuracy = INACCURATE;
    }
  }

  if (result.accuracy == INACCURATE) {
    return result;
  }

  /* ### Phase 2: classify glyphs into exc, or, or and ### */

  for (const auto& [g, segments] : conditions_for_glyph) {
    if (segments == start_nodes) {
      // Disjunctive case, may be either exclusive or "OR"
      // First we need to see if we can make an accurate assessment. This is
      // only possible if this glyph is only reachable from segments that are
      // fully explorable
      const SegmentSet& parent_segments =
          unconstrained_reachability_.SegmentsForGlyph(g);
      SegmentSet other_parent_segments = parent_segments;
      other_parent_segments.subtract(start_nodes);
      // We can exclude start nodes from this check since we verified above that
      // the combination of start nodes is fully explorable.
      if (!other_parent_segments.is_subset_of(fully_explorable_segments_)) {
        result.accuracy = INACCURATE;
        return result;
      }

      // Exclusive glyphs are those that are reachable only from segments in
      // start_nodes here we use isolated reachability since we're only looking
      // for things that can get to g through disjunction only.
      SegmentSet other_isolated_parent_segments =
          isolated_reachability_.SegmentsForGlyph(g);
      other_isolated_parent_segments.subtract(start_nodes);
      if (other_parent_segments != other_isolated_parent_segments) {
        // Possibly reachable via non-disjunctive pathways
        result.accuracy = INACCURATE;
        return result;
      }

      if (other_isolated_parent_segments.empty()) {
        result.exclusive_gids.insert(g);
      } else {
        result.or_gids.insert(g);
      }
      continue;
    }

    // Conjunctive case: three possibilities:
    // 1. Another segment can disjunctively reach this glyph (tested with
    // isolated reachability).
    //    In this case the conjunctive condition found here is superseded, so we
    //    don't classify the glyph.
    SegmentSet isolated_parent_segments =
        isolated_reachability_.SegmentsForGlyph(g);
    if (!isolated_parent_segments.empty()) {
      continue;
    }

    // 2. There is possible reachability to this glyph involving segments other
    // than those in
    //    the condition we found. Here we can't make an accurate assessment.
    SegmentSet parent_segments =
        unconstrained_reachability_.SegmentsForGlyph(g);
    if (!parent_segments.is_subset_of(segments)) {
      result.accuracy = INACCURATE;
      return result;
    }

    // 3. Otherwise this glyph can be safely marked as conjunctive.
    result.and_gids.insert(g);
  }

  return result;
}

btree_set<Node> DependencyClosure::CollectIsolatedReachability(
    const SegmentSet& dest, const SegmentSet& excluded, GlyphSet& out) const {
  btree_set<dep_graph::Node> dest_nodes;
  for (segment_index_t s : dest) {
    dest_nodes.insert(dep_graph::Node::Segment(s));
    if (excluded.contains(s)) {
      continue;
    }
    out.union_set(isolated_reachability_.GlyphsForSegment(s));
  }
  return dest_nodes;
}

StatusOr<DependencyClosure::AnalysisAccuracy>
DependencyClosure::ConjunctiveConditionDiscovery(
    // assumes segments has been filtered and bound checked already
    const SegmentSet& start, const common::GlyphSet& glyph_filter,
    flat_hash_map<glyph_id_t, SegmentSet>& conditions_for_glyph) const {
  flat_hash_map<SegmentSet, GlyphSet> reached_glyphs{{{}, {}}};

  flat_hash_set<PendingEdge> processed_pending_edges;
  btree_set<std::pair<SegmentSet, SegmentSet>> queue;
  queue.insert(std::make_pair(SegmentSet{}, start));

  while (!queue.empty()) {
    std::pair<SegmentSet, SegmentSet> next = *queue.begin();
    const SegmentSet& source = next.first;
    const SegmentSet& dest = next.second;
    queue.erase(next);

    GlyphSet source_glyphs = reached_glyphs.at(source);

    if (!reached_glyphs.contains(dest)) {
      GlyphSet traversal_glyph_filter = glyph_filter;
      btree_set<dep_graph::Node> dest_nodes =
          CollectIsolatedReachability(dest, start, traversal_glyph_filter);

      Traversal traversal = TRY(graph_.ClosureTraversal(
          dest_nodes, &traversal_glyph_filter, nullptr, true));
      reached_glyphs[dest] = traversal.ReachedGlyphs();
      reached_glyphs[dest].intersect(glyph_filter);
      btree_set<SegmentSet> edges;
      if (TRY(ConjunctiveConditionEdges(
              dest, traversal, processed_pending_edges, edges)) == INACCURATE) {
        return INACCURATE;
      }

      for (const SegmentSet& segments : edges) {
        SegmentSet next_dest = dest;
        next_dest.union_set(segments);
        queue.insert(std::make_pair(dest, next_dest));
      }
    }

    GlyphSet delta = reached_glyphs.at(dest);
    delta.subtract(source_glyphs);
    for (glyph_id_t g : delta) {
      auto existing = conditions_for_glyph.find(g);
      if (existing != conditions_for_glyph.end()) {
        if (existing->second.is_subset_of(dest)) {
          // current condition is less granular then this one, keep current.
          continue;
        } else if (!dest.is_subset_of(existing->second)) {
          // Conflicting conditions for a glyph, only assigning a compatible
          // less granular condition is allowed.
          return INACCURATE;
        }
      }
      conditions_for_glyph[g] = dest;
    }
  }

  return ACCURATE;
}

static bool PickOne(const SegmentSet& options, SegmentSet& edges) {
  // TODO XXXX should we consider cases where more than one unblocking segment
  // exists as inaccurate (these naturally lead to (a or b) and ... type
  // conditions)
  auto min = options.min();
  if (min.has_value()) {
    edges.insert(*min);
    return true;
  }
  return false;
}

StatusOr<DependencyClosure::AnalysisAccuracy>
DependencyClosure::ConjunctiveConditionEdges(
    const SegmentSet& node, const Traversal& traversal,
    flat_hash_set<PendingEdge>& excluded_edges,
    btree_set<SegmentSet>& outgoing_edges) const {
  outgoing_edges.clear();
  AnalysisAccuracy result = ACCURATE;
  for (const auto& pe : traversal.PendingEdges()) {
    if (excluded_edges.contains(pe)) {
      continue;
    }

    SegmentSet segments{};
    bool is_unblocked = true;

    if (pe.required_context_set_index.has_value()) {
      // Context edges are not supported in this analysis.
      result = INACCURATE;
      continue;
    }

    if (pe.required_codepoints.has_value()) {
      auto [cp1, cp2] = *pe.required_codepoints;
      if (!segmentation_info_->InitFontSegment().codepoints.contains(cp1) &&
          !traversal.ReachedCodepoints().contains(cp1)) {
        is_unblocked =
            is_unblocked &&
            PickOne(isolated_reachability_.SegmentsForCodepoint(cp1), segments);
      }
      if (!segmentation_info_->InitFontSegment().codepoints.contains(cp2) &&
          !traversal.ReachedCodepoints().contains(cp2)) {
        is_unblocked =
            is_unblocked &&
            PickOne(isolated_reachability_.SegmentsForCodepoint(cp2), segments);
      }
    }

    if (pe.required_feature.has_value() &&
        !segmentation_info_->InitFontSegment().feature_tags.contains(
            *pe.required_feature) &&
        !traversal.ReachedLayoutFeatures().contains(*pe.required_feature)) {
      is_unblocked =
          is_unblocked && PickOne(isolated_reachability_.SegmentsForFeature(
                                      *pe.required_feature),
                                  segments);
    }

    if (pe.required_glyph.has_value() &&
        !segmentation_info_->InitFontGlyphs().contains(*pe.required_glyph) &&
        !traversal.ReachedGlyphs().contains(*pe.required_glyph)) {
      is_unblocked =
          is_unblocked &&
          PickOne(isolated_reachability_.SegmentsForGlyph(*pe.required_glyph),
                  segments);
    }

    if (pe.required_liga_set_index.has_value()) {
      for (glyph_id_t gid : TRY(graph_.RequiredGlyphsFor(pe))) {
        if (!segmentation_info_->InitFontGlyphs().contains(gid) &&
            !traversal.ReachedGlyphs().contains(gid)) {
          is_unblocked =
              is_unblocked &&
              PickOne(isolated_reachability_.SegmentsForGlyph(gid), segments);
        }
      }
    }

    if (is_unblocked) {
      // We were able to find a set of segments that should unblock this edge,
      // the pending edge can now be excluded from future exploration.
      excluded_edges.insert(pe);
      outgoing_edges.insert(segments);
    }
  }

  return result;
}

StatusOr<SegmentSet> DependencyClosure::SegmentsThatInteractWith(
    const GlyphSet& glyphs) {
  // TODO(garretrieger): we can narrow the set by considering context glyphs per
  // activated glyph instead of just the whole set of context glyphs.

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
      if (!init_font_context_added &&
          init_font_reachable_glyphs_.contains(gid)) {
        ReachabilityInitFontAddToCheck(visited_glyphs, visited_features,
                                       to_check, features_to_check);
        init_font_context_added = true;
      }

      // now check if any segments can reach it.
      TRYV(ReachabilitySegmentsAddToCheck(
          unconstrained_reachability_.SegmentsForGlyph(gid), visited_segments,
          visited_glyphs, visited_features, to_check, features_to_check));

    } else if (!features_to_check.empty()) {
      hb_tag_t feature = *features_to_check.begin();
      features_to_check.erase(feature);
      visited_features.insert(feature);

      // now check if any segments can reach it.
      TRYV(ReachabilitySegmentsAddToCheck(
          unconstrained_reachability_.SegmentsForFeature(feature),
          visited_segments, visited_glyphs, visited_features, to_check,
          features_to_check));
    }
  }

  return visited_segments;
}

StatusOr<SegmentSet> DependencyClosure::SegmentInteractionGroup(
    const SegmentSet& segments) {
  SegmentSet to_check = segments;
  SegmentSet visited;

  SegmentSet init_font_group = InitFontConnections();
  if (segments.intersects(init_font_group)) {
    to_check.union_set(init_font_group);
  }

  while (!to_check.empty()) {
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
  // TODO(garretrieger): similar to what we do in SegmentsThatInteractWith we
  // should keep a glyph and features visited sets (we'll need one for context
  // and one for reachable) to avoid unnecessary checks.
  // TODO(garretrieger): a narrower set of connections should be possible if we
  // use context per glyph instead of the full context glyph sets.
  SegmentSet connected;

  for (glyph_id_t gid : unconstrained_reachability_.GlyphsForSegment(s)) {
    connected.union_set(context_reachability_.SegmentsForGlyph(gid));
    connected.union_set(unconstrained_reachability_.SegmentsForGlyph(gid));
  }

  for (glyph_id_t gid : context_reachability_.GlyphsForSegment(s)) {
    connected.union_set(unconstrained_reachability_.SegmentsForGlyph(gid));
  }

  for (hb_tag_t tag : unconstrained_reachability_.FeaturesForSegment(s)) {
    connected.union_set(context_reachability_.SegmentsForFeature(tag));
    connected.union_set(unconstrained_reachability_.SegmentsForFeature(tag));
  }

  for (hb_tag_t tag : context_reachability_.FeaturesForSegment(s)) {
    connected.union_set(unconstrained_reachability_.SegmentsForFeature(tag));
  }

  return connected;
}

SegmentSet DependencyClosure::InitFontConnections() {
  SegmentSet connected;

  for (glyph_id_t gid : init_font_reachable_glyphs_) {
    connected.union_set(context_reachability_.SegmentsForGlyph(gid));
    connected.union_set(unconstrained_reachability_.SegmentsForGlyph(gid));
  }

  for (glyph_id_t gid : init_font_context_glyphs_) {
    connected.union_set(unconstrained_reachability_.SegmentsForGlyph(gid));
  }

  for (hb_tag_t tag : init_font_features_) {
    connected.union_set(context_reachability_.SegmentsForFeature(tag));
    connected.union_set(unconstrained_reachability_.SegmentsForFeature(tag));
  }

  for (hb_tag_t tag : init_font_context_features_) {
    connected.union_set(unconstrained_reachability_.SegmentsForFeature(tag));
  }

  return connected;
}

void DependencyClosure::ReachabilityInitFontAddToCheck(
    GlyphSet& visited_glyphs, btree_set<hb_tag_t>& visited_features,
    GlyphSet& to_check, btree_set<hb_tag_t>& features_to_check) const {
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
    const SegmentSet& segments, SegmentSet& visited_segments,
    GlyphSet& visited_glyphs, btree_set<hb_tag_t>& visited_features,
    GlyphSet& to_check, btree_set<hb_tag_t>& features_to_check) const {
  for (segment_index_t s : segments) {
    if (visited_segments.contains(s)) {
      continue;
    }

    visited_segments.insert(s);
    auto traversal = TRY(graph_.ClosureTraversal({s}, false));

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

Status DependencyClosure::UpdateReachabilityIndex(SegmentSet segments) {
  if (reachability_index_valid_) {
    // If indices have existing data, then we need to ensure prior entries for
    // the segments to be updated are cleared out.
    for (segment_index_t s : segments) {
      if (s >= segmentation_info_->Segments().size()) {
        break;
      }
      ClearReachabilityIndex(s);
    }
  } else {
    // If the index isn't built yet then all segments need to be updated.
    segments = SegmentSet::all();
  }

  for (segment_index_t s : segments) {
    if (s >= segmentation_info_->Segments().size()) {
      break;
    }
    TRYV(UpdateReachabilityIndex(s));
  }

  for (segment_index_t s : segments) {
    if (s >= segmentation_info_->Segments().size()) {
      break;
    }
    const AnalysisResult result = TRY(AnalyzeSegmentInternal({s}));
    if (/* TODO XXXXX we probably want this: result.accuracy == ACCURATE && */
        result.reached_glyphs ==
        unconstrained_reachability_.GlyphsForSegment(s)) {
      fully_explorable_segments_.insert(s);
    }
  }

  reachability_index_valid_ = true;
  return absl::OkStatus();
}

Status DependencyClosure::UpdateReachabilityIndex(segment_index_t s) {
  {
    auto constrained_traversal = TRY(graph_.ClosureTraversal({s}, true));
    if (TraversalAccuracy(constrained_traversal) == ACCURATE) {
      isolated_reachability_is_accurate_.insert(s);
    }
    for (glyph_id_t g : constrained_traversal.ReachedGlyphs()) {
      isolated_reachability_.AddGlyph(s, g);
    }
    for (hb_codepoint_t cp : constrained_traversal.ReachedCodepoints()) {
      isolated_reachability_.AddCodepoint(s, cp);
    }
    for (hb_tag_t f : constrained_traversal.ReachedLayoutFeatures()) {
      isolated_reachability_.AddFeature(s, f);
    }
  }

  auto traversal = TRY(graph_.ClosureTraversal({s}, false));
  for (glyph_id_t g : traversal.ReachedGlyphs()) {
    unconstrained_reachability_.AddGlyph(s, g);
  }

  for (hb_codepoint_t cp : traversal.ReachedCodepoints()) {
    unconstrained_reachability_.AddCodepoint(s, cp);
  }

  for (hb_tag_t f : traversal.ReachedLayoutFeatures()) {
    unconstrained_reachability_.AddFeature(s, f);
  }

  for (glyph_id_t g : traversal.ContextGlyphs()) {
    context_reachability_.AddGlyph(s, g);
  }

  for (hb_tag_t f : traversal.ContextLayoutFeatures()) {
    context_reachability_.AddFeature(s, f);
  }

  return absl::OkStatus();
}

void DependencyClosure::ClearReachabilityIndex(segment_index_t segment) {
  unconstrained_reachability_.ClearSegment(segment);
  isolated_reachability_.ClearSegment(segment);
  context_reachability_.ClearSegment(segment);
  isolated_reachability_is_accurate_.erase(segment);
  fully_explorable_segments_.erase(segment);
}

}  // namespace ift::encoder