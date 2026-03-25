#include "ift/encoder/dependency_closure.h"

#include "absl/log/log.h"
#include "ift/common/font_helper.h"
#include "ift/common/hb_set_unique_ptr.h"
#include "ift/common/int_set.h"
#include "ift/dep_graph/dependency_graph.h"
#include "ift/dep_graph/node.h"
#include "ift/dep_graph/traversal.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/segment.h"
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
  //
  // This implementation relies on precomputed reachability indices, the
  // approach is this:
  // 1. The isolated_* indices record reachable glyphs from a starting segment
  //    where the traversal should exactly match closure.
  // 2. If an isolated traversal from the starting segments is available, and
  //    each glyph is only reachable from other isolated traversals then we can
  //    form accurate conditions.
  // 3. In that case for each glyph we can distinguish whether it's an
  //    exclusive, or disjunctive condition by checking the index to see if
  //    only the input segments can reach that specific glyph.
  //
  // TODO(garretrieger): This implementation is still early stages and is
  // missing quite a bit, here's a list of some additional things that are
  // needed:
  // - Only handles disjunctive and exclusive conditions, anything conjunctive
  // will report
  //   an inaccurate analysis. We should be able to at least support simple
  //   conjunctive cases without too much trouble (for example UVS, non-nested
  //   liga's, and segments with layout features).

  // If we have more than one segment we need to retraverse because combining
  // two previously interacting segments may result in a new combined accurate
  // traversal. If only one segment is present we can look up the traversal from
  // the reachability index.
  AnalysisResult result;
  result.accuracy = ACCURATE;

  if (segments.size() > 1) {
    SegmentSet start_nodes;
    for (segment_index_t segment_id : segments) {
      if (segment_id >= segmentation_info_->Segments().size()) {
        return absl::InvalidArgumentError(
            absl::StrCat("Segment index ", segment_id, " is out of bounds."));
      }

      const Segment& segment = segmentation_info_->Segments().at(segment_id);
      if (segment.Definition().Empty()) {
        // Empty segments are ignored.
        continue;
      }

      start_nodes.insert(segment_id);
    }

    Traversal traversal = TRY(graph_.ClosureTraversal(start_nodes));
    result.accuracy = TraversalAccuracy(traversal);
    result.reached_glyphs = traversal.ReachedGlyphs();
  } else {
    segment_index_t segment_id = *segments.begin();
    if (segment_id >= segmentation_info_->Segments().size()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Segment index ", segment_id, " is out of bounds."));
    }

    result.reached_glyphs = isolated_reachability_.GlyphsForSegment(segment_id);
    if (!isolated_reachability_is_accurate_.contains(segment_id)) {
      result.accuracy = INACCURATE;
    }
  }

  if (result.accuracy == INACCURATE) {
    return result;
  }

  // Now we need to test each reached glyph to see if we have fully accurate
  // reachability information with which to make an exclusive or disjunctive
  // determination.
  for (glyph_id_t gid : result.reached_glyphs) {
    if (!GlyphHasFullyAccurateReachability(gid, segments)) {
      result.accuracy = INACCURATE;
      return result;
    }

    if (unconstrained_reachability_.SegmentsForGlyph(gid).is_subset_of(
            segments)) {
      result.exclusive_gids.insert(gid);
    } else {
      result.or_gids.insert(gid);
    }
  }

  return result;
}

StatusOr<DependencyClosure::AnalysisAccuracy>
DependencyClosure::ConjunctiveConditionDiscovery(
    // assumes segments has been filtered and bound checked already
    const SegmentSet& start,
    flat_hash_map<glyph_id_t, SegmentSet>& conditions_for_glyph) const {
  flat_hash_map<SegmentSet, GlyphSet> reached_glyphs{{{}, {}}};

  btree_set<std::pair<SegmentSet, SegmentSet>> queue;
  queue.insert(std::make_pair(SegmentSet{}, start));

  AnalysisAccuracy result = ACCURATE;

  while (!queue.empty()) {
    auto next = *queue.begin();
    queue.erase(next);

    SegmentSet source;
    SegmentSet dest;

    const GlyphSet& source_glyphs = reached_glyphs.at(source);

    if (!reached_glyphs.contains(dest)) {
      Traversal traversal = TRY(graph_.ClosureTraversal(dest, true));
      reached_glyphs[dest] = traversal.ReachedGlyphs();
      SegmentSet edges;
      if (TRY(ConjunctiveConditionEdges(dest, traversal, edges)) ==
          INACCURATE) {
        result = INACCURATE;
      }

      for (segment_index_t s : edges) {
        SegmentSet next_dest = dest;
        next_dest.insert(s);
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
          result = INACCURATE;
          continue;
        }
      }
      conditions_for_glyph[g] = dest;
    }
  }

  return result;
}

StatusOr<DependencyClosure::AnalysisAccuracy>
DependencyClosure::ConjunctiveConditionEdges(const SegmentSet& node,
                                             const Traversal& traversal,
                                             SegmentSet& edges) const {
  edges.clear();
  AnalysisAccuracy result = ACCURATE;
  for (const auto& pe : traversal.PendingEdges()) {
    if (pe.required_context_set_index.has_value()) {
      // Context edges are not supported in this analysis.
      result = INACCURATE;
      continue;
    }

    if (pe.required_codepoints.has_value()) {
      // TODO XXXX add codepoints to the reachability index to support this
      // case. Needed to support UVS.
      result = INACCURATE;
      continue;
    }

    if (pe.required_feature.has_value()) {
      edges.union_set(
          isolated_reachability_.SegmentsForFeature(*pe.required_feature));
    }

    if (pe.required_liga_set_index.has_value()) {
      for (glyph_id_t gid : TRY(graph_.RequiredGlyphsFor(pe))) {
        edges.union_set(isolated_reachability_.SegmentsForGlyph(gid));
      }
    }
  }

  return result;
}

bool DependencyClosure::GlyphHasFullyAccurateReachability(
    glyph_id_t gid, const SegmentSet& excluded) const {
  const SegmentSet& segments_ref =
      unconstrained_reachability_.SegmentsForGlyph(gid);

  if (segments_ref.is_subset_of(excluded)) {
    // If the only segments which can possibly reach this gid are in excluded
    // then the new combined segment should be exclusive and hence accurate
    // with respect to the gid.
    return true;
  }

  const SegmentSet& accurate_segments_ref =
      isolated_reachability_.SegmentsForGlyph(gid);
  if (accurate_segments_ref.empty()) {
    return false;
  }

  SegmentSet accurate_segments = accurate_segments_ref;
  SegmentSet segments = segments_ref;
  accurate_segments.subtract(excluded);
  segments.subtract(excluded);
  return accurate_segments == segments;
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
  }

  auto traversal = TRY(graph_.ClosureTraversal({s}, false));
  for (glyph_id_t g : traversal.ReachedGlyphs()) {
    unconstrained_reachability_.AddGlyph(s, g);
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