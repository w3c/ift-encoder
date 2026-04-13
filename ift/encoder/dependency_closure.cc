#include "ift/encoder/dependency_closure.h"

#include "absl/log/log.h"
#include "ift/common/font_helper.h"
#include "ift/common/hb_set_unique_ptr.h"
#include "ift/common/int_set.h"
#include "ift/dep_graph/dependency_graph.h"
#include "ift/dep_graph/node.h"
#include "ift/dep_graph/pending_edge.h"
#include "ift/dep_graph/traversal.h"
#include "ift/encoder/activation_condition.h"
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
using ift::dep_graph::EdgeConditonsCnf;
using ift::dep_graph::Node;
using ift::dep_graph::PendingEdge;
using ift::dep_graph::Traversal;
using ift::dep_graph::TraversalContext;

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

Status DependencyClosure::InitFontChanged(const SegmentSet& segments) {
  VLOG(1) << "DependencyClosure::InitFontChanged()";

  // TODO(garretrieger): for now to keep this simple we do a full recalculation
  // of per glyph conditions. However, it's likely possible to do a more incremental
  // update based on changed segments. Re-visit this if this ends up showing up
  // in profiles.
  glyph_condition_cache_ = TRY(ExtractAllGlyphConditions());;

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

  init_font_reachable_glyphs_ = init_font_traversal.ReachedGlyphs();
  init_font_reachable_glyphs_.subtract(segmentation_info_->InitFontGlyphs());

  // Note: reachability index must be updated after context_glyphs_, init_font_*
  // sets are populated. since it utilizes those to assess traversal accuracy.
  TRYV(UpdateReachabilityIndex(segments));

  return absl::OkStatus();
}

Status DependencyClosure::SegmentsMerged(segment_index_t base_segment,
                                         const SegmentSet& segments) {
  if (!segmentation_info_->SegmentsAreDisjoint()) {
    // Non-disjoint segments requires more extensive invalidation provided
    // by init font changed.
    return InitFontChanged(segments);
  }

  // Manually update the glyph condition cache.
  for (auto& [gid, condition] : glyph_condition_cache_) {
    // TODO(garretrieger): avoid looking at the full condition set by using
    // a reachability index to locate just the glyphs that are affected.
    if (condition.Intersects(segments)) {
      condition = condition.ReplaceSegments(base_segment, segments);
    }
  }

  VLOG(1) << "DependencyClosure::SegmentsMerged()";
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

    if (pe.source.IsGlyph() &&
        !segmentation_info_->InitFontGlyphs().contains(pe.source.Id()) &&
        !traversal.ReachedGlyphs().contains(pe.source.Id())) {
      is_unblocked =
          is_unblocked &&
          PickOne(isolated_reachability_.SegmentsForGlyph(pe.source.Id()),
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
    const SubsetDefinition& def) const {
  flat_hash_set<Node> nodes;
  for (hb_codepoint_t u : def.codepoints) {
    nodes.insert(Node::Unicode(u));
  }
  for (glyph_id_t g : def.gids) {
    nodes.insert(Node::Glyph(g));
  }
  for (hb_tag_t f : def.feature_tags) {
    nodes.insert(Node::Feature(f));
  }
  return SegmentsThatInteractWith(nodes);
}

StatusOr<SegmentSet> DependencyClosure::SegmentsThatInteractWith(
    const GlyphSet& glyphs) const {
  flat_hash_set<Node> nodes;
  for (glyph_id_t gid : glyphs) {
    nodes.insert(Node::Glyph(gid));
  }
  return SegmentsThatInteractWith(nodes);
}

StatusOr<SegmentSet> DependencyClosure::SegmentsThatInteractWith(
    const absl::flat_hash_set<dep_graph::Node> nodes) const {
  // TODO(garretrieger): we can narrow the set by considering context glyphs per
  // activated glyph instead of just the whole set of context glyphs.

  flat_hash_set<Node> visited;
  SegmentSet visited_segments;
  GlyphSet visited_glyphs;

  btree_set<Node> to_check;
  to_check.insert(nodes.begin(), nodes.end());

  // Expand to_check to include anything down stream of the input
  // node set.
  auto traversal =
      TRY(graph_.ClosureTraversal(to_check, nullptr, nullptr, false));
  for (glyph_id_t g : traversal.ReachedGlyphs()) {
    to_check.insert(Node::Glyph(g));
  }
  for (hb_codepoint_t u : traversal.ReachedCodepoints()) {
    to_check.insert(Node::Unicode(u));
  }
  for (hb_tag_t f : traversal.ReachedLayoutFeatures()) {
    to_check.insert(Node::Feature(f));
  }

  bool init_font_context_added = false;

  while (!to_check.empty()) {
    Node next = *to_check.begin();
    to_check.erase(next);
    visited.insert(next);
    if (next.IsGlyph()) {
      visited_glyphs.insert(next.Id());
    }

    // node may be reachable from the init font.
    if (!init_font_context_added && next.IsGlyph() &&
        init_font_reachable_glyphs_.contains(next.Id())) {
      ReachabilityInitFontAddToCheck(visited_glyphs, to_check);
      init_font_context_added = true;
    }

    // now check if any segments can reach it.
    SegmentSet segments;
    if (next.IsGlyph()) {
      segments = unconstrained_reachability_.SegmentsForGlyph(next.Id());
    } else if (next.IsUnicode()) {
      segments = unconstrained_reachability_.SegmentsForCodepoint(next.Id());
    } else if (next.IsFeature()) {
      segments = unconstrained_reachability_.SegmentsForFeature(next.Id());
    } else {
      // ignored node type.
      continue;
    }

    if (segments.empty()) {
      continue;
    }

    TRYV(ReachabilitySegmentsAddToCheck(segments, visited_glyphs,
                                        visited_segments, to_check));
  }

  return visited_segments;
}

void DependencyClosure::ReachabilityInitFontAddToCheck(
    const GlyphSet& visited_glyphs, btree_set<Node>& to_check) const {
  GlyphSet additional = init_font_context_glyphs_;
  additional.subtract(visited_glyphs);
  for (glyph_id_t gid : additional) {
    to_check.insert(Node::Glyph(gid));
  }
}

Status DependencyClosure::ReachabilitySegmentsAddToCheck(
    const SegmentSet& segments, const GlyphSet& visited_glyphs,
    SegmentSet& visited_segments, btree_set<Node>& to_check) const {
  for (segment_index_t s : segments) {
    if (visited_segments.contains(s)) {
      continue;
    }

    visited_segments.insert(s);

    GlyphSet additional = context_reachability_.GlyphsForSegment(s);

    additional.subtract(visited_glyphs);

    for (glyph_id_t gid : additional) {
      to_check.insert(Node::Glyph(gid));
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
    if (result.reached_glyphs ==
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

  return absl::OkStatus();
}

void DependencyClosure::ClearReachabilityIndex(segment_index_t segment) {
  unconstrained_reachability_.ClearSegment(segment);
  isolated_reachability_.ClearSegment(segment);
  context_reachability_.ClearSegment(segment);
  isolated_reachability_is_accurate_.erase(segment);
  fully_explorable_segments_.erase(segment);
}

flat_hash_map<Node, ActivationCondition>
DependencyClosure::InitializeConditions() const {
  flat_hash_map<Node, ActivationCondition> conditions;

  conditions.insert({Node::InitFont(), ActivationCondition::True(0)});

  for (glyph_id_t g : segmentation_info_->InitFontGlyphs()) {
    conditions.insert({Node::Glyph(g), ActivationCondition::True(0)});
  }

  for (hb_codepoint_t u : segmentation_info_->InitFontSegment().codepoints) {
    conditions.insert({Node::Unicode(u), ActivationCondition::True(0)});
  }

  auto init_features = graph_.InitFontFeatureSet();
  if (init_features.ok()) {
    for (hb_tag_t f : *init_features) {
      conditions.insert({Node::Feature(f), ActivationCondition::True(0)});
    }
  }

  for (segment_index_t s = 0; s < segmentation_info_->Segments().size(); s++) {
    if (segmentation_info_->Segments().at(s).Definition().Empty()) {
      continue;
    }
    conditions.insert(
        {Node::Segment(s), ActivationCondition::exclusive_segment(s, 0)});
  }

  return conditions;
}

StatusOr<std::optional<ActivationCondition>>
DependencyClosure::EdgeConditionsToActivationCondition(
    const dep_graph::EdgeConditonsCnf& edge_conditions,
    const absl::flat_hash_map<dep_graph::Node, ActivationCondition>&
        node_conditions) {
  if (edge_conditions.empty()) {
    return absl::InternalError("edge_conditions cannot be empty.");
  }

  std::optional<ActivationCondition> out;

  for (const btree_set<Node>& node_group : edge_conditions) {
    std::optional<ActivationCondition> group_condition;
    if (node_group.empty()) {
      return absl::InternalError("Unexpected empty node condition group.");
    }

    for (Node node : node_group) {
      auto it = node_conditions.find(node);
      if (it == node_conditions.end()) {
        // The condition for this node is FALSE. Since it's combined
        // with OR with other nodes in the group we can just skip
        continue;
      }

      if (!group_condition.has_value()) {
        group_condition = it->second;
      } else {
        // edge_conditions is in cnf form, so the inner groups are disjunctive.
        group_condition = ActivationCondition::Or(*group_condition, it->second);
      }
    }

    if (!group_condition.has_value()) {
      // If group_condition is empty that means it's FALSE, and since the group
      // condition combines with AND with other sub-groups, the whole expression
      // is also false.
      return std::nullopt;
    }

    if (!out.has_value()) {
      out = group_condition;
    } else {
      out = ActivationCondition::And(*out, *group_condition);
    }
  }

  return out;
}

Status DependencyClosure::PropagateConditions(
    const flat_hash_map<Node, btree_set<EdgeConditonsCnf>>& incoming_edges,
    const std::vector<std::vector<Node>>& sccs,
    flat_hash_map<Node, ActivationCondition>& node_conditions) const {
  for (const std::vector<Node>& scc : sccs) {
    // strongly connect components have cycles so we need to iteratively
    // propagate conditions through the cycle until they stop changing.
    // Since conditions can only grow this is gauranteed to stop eventually.
    bool changed = true;
    while (changed) {
      changed = false;
      for (Node n : scc) {
        auto it = incoming_edges.find(n);
        if (it == incoming_edges.end()) {
          // we only process nodes that have incoming edges within each phase.
          continue;
        }

        auto node_conditions_it = node_conditions.find(n);
        if (node_conditions_it != node_conditions.end() &&
            node_conditions_it->second.IsAlwaysTrue()) {
          // If node is always true, it's condition cannot change further.
          continue;
        }

        std::optional<ActivationCondition> complete_condition;

        for (const EdgeConditonsCnf& edge : it->second) {
          std::optional<ActivationCondition> condition =
              TRY(EdgeConditionsToActivationCondition(edge, node_conditions));
          if (!condition.has_value()) {
            // std::nullopt means the condition for this edge is false, since
            // it combines with OR with other edges, we can just skip it.
            continue;
          }

          if (complete_condition.has_value()) {
            complete_condition =
                ActivationCondition::Or(*complete_condition, *condition);
          } else {
            complete_condition = condition;
          }
        }

        if (!complete_condition.has_value()) {
          // If no condition has been found, the condition for this node in this
          // phase is FALSE so don't add a entry into the node_conditions map.
          continue;
        }

        auto it_existing = node_conditions.find(n);
        if (it_existing == node_conditions.end()) {
          node_conditions.insert({n, *complete_condition});
          changed = true;
        } else {
          ActivationCondition combined =
              ActivationCondition::Or(it_existing->second, *complete_condition);
          if (combined != it_existing->second) {
            it_existing->second = std::move(combined);
            changed = true;
          }
        }
      }

      if (scc.size() == 1) {
        // If the component has only one node we can assume the condition
        // is already stabilized and shortcut a second check.
        break;
      }
    }
  }

  return absl::OkStatus();
}

StatusOr<flat_hash_map<glyph_id_t, ActivationCondition>>
DependencyClosure::ExtractAllGlyphConditions() const {
  auto conditions = InitializeConditions();

  flat_hash_set<hb_tag_t> table_tags =
      ift::common::FontHelper::GetTags(original_face_.get());

  for (uint32_t phase = 0; phase < DependencyGraph::kNumberOfClosurePhases;
       phase++) {
    hb_tag_t table = DependencyGraph::kClosurePhaseTable[phase];
    if (table_tags.contains(table)) {
      auto sccs = TRY(graph_.StronglyConnectedComponents(
          {table}, DependencyGraph::kClosurePhaseNodeFilter[phase]));
      auto incoming_edges = TRY(graph_.CollectIncomingEdges(
          {table}, DependencyGraph::kClosurePhaseNodeFilter[phase]));
      TRYV(PropagateConditions(incoming_edges, sccs, conditions));
    }
  }

  flat_hash_map<glyph_id_t, ActivationCondition> result;
  for (glyph_id_t gid : segmentation_info_->NonInitFontGlyphs()) {
    auto it = conditions.find(Node::Glyph(gid));
    if (it != conditions.end()) {
      result.insert({gid, it->second});
    } else {
      return absl::InternalError(
          "All glyphs should have generated conditions.");
    }
  }

  return result;
}
}  // namespace ift::encoder