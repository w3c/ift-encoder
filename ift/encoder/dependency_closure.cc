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
using ift::dep_graph::EdgeConditionsCnf;
using ift::dep_graph::Node;
using ift::dep_graph::PendingEdge;
using ift::dep_graph::Traversal;
using ift::dep_graph::TraversalContext;

namespace ift::encoder {

Status DependencyClosure::InitFontChanged(const SegmentSet& segments) {
  VLOG(1) << "DependencyClosure::InitFontChanged()";

  // TODO(garretrieger): for now to keep this simple we do a full recalculation
  // of per glyph conditions. However, it's likely possible to do a more
  // incremental update based on changed segments. Re-visit this if this ends up
  // showing up in profiles.
  TRYV(InitGlyphConditionsCache());

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
  UpdateGlyphConditionsCache(base_segment, segments);

  VLOG(1) << "DependencyClosure::SegmentsMerged()";
  TRYV(UpdateReachabilityIndex(segments));
  return absl::OkStatus();
}

Status DependencyClosure::InitGlyphConditionsCache() {
  glyph_conditions_with_segment_.clear();
  glyph_condition_cache_ = TRY(ExtractAllGlyphConditions());
  for (const auto& [g, condition] : glyph_condition_cache_) {
    for (segment_index_t s : condition.TriggeringSegments()) {
      glyph_conditions_with_segment_[s].insert(g);
    }
  }
  return absl::OkStatus();
}

void DependencyClosure::UpdateGlyphConditionsCache(
    segment_index_t base_segment, const common::SegmentSet& segments) {
  GlyphSet effected_glyphs = SegmentsToEffectedGlyphConditions(segments);

  for (glyph_id_t gid : effected_glyphs) {
    auto it = glyph_condition_cache_.find(gid);
    if (it == glyph_condition_cache_.end()) {
      continue;
    }

    for (segment_index_t s : it->second.TriggeringSegments()) {
      auto it = glyph_conditions_with_segment_.find(s);
      if (it != glyph_conditions_with_segment_.end()) {
        it->second.erase(gid);
      }
    }

    it->second = it->second.ReplaceSegments(base_segment, segments);
    for (segment_index_t s : it->second.TriggeringSegments()) {
      glyph_conditions_with_segment_[s].insert(gid);
    }
  }
}

GlyphSet DependencyClosure::SegmentsToEffectedGlyphConditions(
    const SegmentSet& segments) const {
  GlyphSet glyphs;
  for (segment_index_t s : segments) {
    auto it = glyph_conditions_with_segment_.find(s);
    if (it != glyph_conditions_with_segment_.end()) {
      glyphs.union_set(it->second);
    }
  }
  return glyphs;
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
DependencyClosure::AnalyzeSegmentInternal(
    const SegmentSet& segments_input) const {
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

  AnalysisResult result;
  result.accuracy = ACCURATE;

  SegmentSet segments = TRY(FilterSegments(segments_input));
  if (segments.empty()) {
    return result;
  }

  GlyphSet inscope_glyphs = SegmentsToEffectedGlyphConditions(segments);
  result.reached_glyphs = inscope_glyphs;

  if (inscope_glyphs.intersects(context_glyphs_) ||
      inscope_glyphs.intersects(init_font_context_glyphs_)) {
    // For now don't return results for anything that involves contextual lookup
    // glyphs.
    result.accuracy = INACCURATE;
    return result;
  }

  // Classify each glyph based on it's conditions
  for (glyph_id_t gid : inscope_glyphs) {
    auto it = glyph_condition_cache_.find(gid);
    if (it == glyph_condition_cache_.end()) {
      continue;
    }
    ActivationCondition condition = it->second;
    segment_index_t segment = *segments.min();
    if (segments.size() > 1) {
      // Pretend the input segments are merged into one for this analysis.
      condition = condition.ReplaceSegments(segment, segments);
    }

    if (!condition.TriggeringSegments().contains(segment)) {
      // Shouldn't happen, something is wrong with the glyph condition cache.
      return absl::InternalError(absl::StrCat("condition in cache for g", gid,
                                              " does not include s", segment));
    }

    // Possible cases:
    // 1. condition is unitary (only one segment), then glyph is exclusive to
    // the input.
    if (condition.IsUnitary()) {
      result.exclusive_gids.insert(gid);
      continue;
    }

    // 2. condition is purely disjunctive, glyph is disjunctive to the input.
    if (condition.IsPurelyDisjunctive()) {
      result.or_gids.insert(gid);
    }

    if (condition.conditions().size() > 1) {
      bool in_all = true;
      bool mixed = false;
      for (const auto& disjunctive_group : condition.conditions()) {
        if (!disjunctive_group.contains(segment)) {
          in_all = false;
          continue;
        }

        if (disjunctive_group.size() > 1) {
          // input segment is involved in a mix of conjunction and disjunction.
          mixed = true;
        }
      }

      // 2. condition has conjunction (two or more sub-groups), and input
      // segment appears in every disjunctive sub group, glyph is disjunctive to
      // the input.
      if (in_all) {
        result.or_gids.insert(gid);
      }

      // 3. condition has conjunction (two or more sub-groups), and the
      // sub-group that contains the input segment(s) equals the input
      // segment(s), then glyph is conjunctive to the input.
      if (!mixed) {
        result.and_gids.insert(gid);
      }

      // 4. otherwise gid does not appear in the analysis results due to more
      // complicated conditions.
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

  reachability_index_valid_ = true;
  return absl::OkStatus();
}

Status DependencyClosure::UpdateReachabilityIndex(segment_index_t s) {
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
  context_reachability_.ClearSegment(segment);
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
    const dep_graph::EdgeConditionsCnf& edge_conditions,
    const absl::flat_hash_map<dep_graph::Node, ActivationCondition>&
        node_conditions) {
  if (edge_conditions.empty()) {
    return absl::InternalError("edge_conditions cannot be empty.");
  }

  std::optional<ActivationCondition> out;

  for (const std::vector<Node>& node_group : edge_conditions) {
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
    const flat_hash_map<Node, btree_set<EdgeConditionsCnf>>& incoming_edges,
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

        for (const EdgeConditionsCnf& edge : it->second) {
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