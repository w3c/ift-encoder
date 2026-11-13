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

  return InitNodeConditionsCache(segments);
}

Status DependencyClosure::SegmentsMerged(segment_index_t base_segment,
                                         const SegmentSet& segments) {
  if (!segmentation_info_->SegmentsAreDisjoint()) {
    // Non-disjoint segments requires more extensive invalidation provided
    // by init font changed.
    return InitFontChanged(segments);
  }

  VLOG(1) << "DependencyClosure::SegmentsMerged()";

  // Manually update the glyph condition cache.
  UpdateNodeConditionsCache(base_segment, segments);
  return absl::OkStatus();
}

Status DependencyClosure::SegmentsReset() {
  VLOG(1) << "DependencyClosure::SegmentsReset()";
  incoming_edges_cache_[0].reset();
  init_font_nodes_.clear();
  node_condition_cache_.clear();
  for (auto& cache : phase_node_condition_cache_) {
    cache.clear();
  }
  return InitNodeConditionsCache(SegmentSet::all());
}

Status DependencyClosure::InitNodeConditionsCache(
    const common::SegmentSet& changed_segments) {
  TRYV(UpdateAllNodeConditions(changed_segments));

  node_conditions_with_segment_.clear();
  glyph_condition_cache_.clear();
  for (const auto& [n, condition] : node_condition_cache_) {
    for (segment_index_t s : condition.TriggeringSegments()) {
      node_conditions_with_segment_[s].insert(n);
    }
    if (n.IsGlyph() &&
        segmentation_info_->NonInitFontGlyphs().contains(n.Id())) {
      glyph_condition_cache_.insert({n.Id(), condition});
    }
  }

  inert_segments_ = ComputeInertSegments(glyph_condition_cache_);

  return absl::OkStatus();
}

void DependencyClosure::UpdateNodeConditionsCache(
    segment_index_t base_segment, const common::SegmentSet& segments) {
  auto affected_nodes = SegmentsToAffectedNodeConditions(segments);

  bool base_is_inert = true;

  for (Node node : affected_nodes) {
    auto it = node_condition_cache_.find(node);
    if (it == node_condition_cache_.end()) {
      continue;
    }

    for (segment_index_t s : it->second.TriggeringSegments()) {
      auto it = node_conditions_with_segment_.find(s);
      if (it != node_conditions_with_segment_.end()) {
        it->second.erase(node);
      }
    }

    ActivationCondition new_condition =
        it->second.ReplaceSegments(base_segment, segments);
    it->second = new_condition;
    if (node.IsGlyph() &&
        segmentation_info_->NonInitFontGlyphs().contains(node.Id())) {
      glyph_condition_cache_.erase(node.Id());
      glyph_condition_cache_.insert({node.Id(), new_condition});
    }

    if (!new_condition.IsUnitary()) {
      base_is_inert = false;
    }

    for (segment_index_t s : it->second.TriggeringSegments()) {
      node_conditions_with_segment_[s].insert(node);
    }
  }

  inert_segments_.subtract(segments);
  if (base_is_inert) {
    inert_segments_.insert(base_segment);
  } else {
    inert_segments_.erase(base_segment);
  }
}

GlyphSet DependencyClosure::SegmentsToAffectedGlyphs(
    const SegmentSet& segments) const {
  auto nodes = SegmentsToAffectedNodeConditions(segments);
  GlyphSet out;
  for (auto node : nodes) {
    if (node.IsGlyph()) {
      out.insert(node.Id());
    }
  }
  return out;
}

flat_hash_set<Node> DependencyClosure::SegmentsToAffectedNodeConditions(
    const SegmentSet& segments) const {
  flat_hash_set<Node> nodes;
  for (segment_index_t s : segments) {
    auto it = node_conditions_with_segment_.find(s);
    if (it != node_conditions_with_segment_.end()) {
      nodes.insert(it->second.begin(), it->second.end());
    }
  }
  return nodes;
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
  btree_set<Node> to_check;
  to_check.insert(nodes.begin(), nodes.end());

  // Expand to_check to include anything down stream of the input
  // node set.
  auto traversal =
      TRY(graph_.ClosureTraversal(to_check, nullptr, nullptr, DependencyGraph::NO_CONTEXT));
  for (glyph_id_t g : traversal.ReachedGlyphs()) {
    to_check.insert(Node::Glyph(g));
  }
  for (hb_codepoint_t u : traversal.ReachedCodepoints()) {
    to_check.insert(Node::Unicode(u));
  }
  for (hb_tag_t f : traversal.ReachedLayoutFeatures()) {
    to_check.insert(Node::Feature(f));
  }

  SegmentSet out;
  for (Node node : to_check) {
    auto it = node_condition_cache_.find(node);
    if (it == node_condition_cache_.end()) {
      continue;
    }
    out.union_set(it->second.TriggeringSegments());
  }
  return out;
}

Status DependencyClosure::InitializeConditions(
    flat_hash_map<dep_graph::Node, ActivationCondition>& conditions,
    const SegmentSet& changed_segments,
    const flat_hash_set<Node>& new_init_font_nodes) const {
  for (const auto& n : new_init_font_nodes) {
    conditions.insert_or_assign(n, ActivationCondition::True(0));
  }

  for (segment_index_t s : changed_segments) {
    if (s >= segmentation_info_->Segments().size()) {
      break;
    }

    Node n = Node::Segment(s);
    if (!segmentation_info_->Segments().at(s).Definition().Empty()) {
      conditions.insert_or_assign(Node::Segment(s),
                                  ActivationCondition::exclusive_segment(s, 0));
    } else {
      conditions.erase(n);
    }
  }

  return absl::OkStatus();
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
    const flat_hash_map<Node, std::vector<EdgeConditionsCnf>>& incoming_edges,
    const std::vector<std::vector<Node>>& sccs,
    flat_hash_map<Node, ActivationCondition>& node_conditions,
    flat_hash_set<Node>& modified) const {
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
          modified.insert(n);
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

StatusOr<flat_hash_set<Node>> DependencyClosure::ReachableNonInitFontNodes(
    const SegmentSet& changed_segments,
    const flat_hash_set<Node>& new_init_nodes) const {
  btree_set<Node> start_nodes;
  start_nodes.insert(new_init_nodes.begin(), new_init_nodes.end());

  for (segment_index_t s : changed_segments) {
    if (s >= segmentation_info_->Segments().size()) {
      break;
    }
    if (segmentation_info_->Segments().at(s).Definition().Empty()) {
      continue;
    }
    start_nodes.insert(Node::Segment(s));
  }

  Traversal traversal = TRY(graph_.ClosureTraversal(
      start_nodes, &segmentation_info_->FullClosure(),
      &segmentation_info_->FullCodepointClosure(), DependencyGraph::NO_CONTEXT));

  flat_hash_set<Node> reachable = traversal.ReachedNodes();
  reachable.insert(start_nodes.begin(), start_nodes.end());

  // While we want to traverse through init font nodes for reachability, we
  // don't actually want init font nodes in the final output, so remove them
  // before returning.
  reachable.erase(Node::InitFont());

  for (glyph_id_t g : segmentation_info_->InitFontGlyphs()) {
    reachable.erase(Node::Glyph(g));
  }

  for (hb_codepoint_t c : segmentation_info_->InitFontSegment().codepoints) {
    reachable.erase(Node::Unicode(c));
  }

  for (hb_tag_t f : segmentation_info_->InitFontSegment().feature_tags) {
    reachable.erase(Node::Feature(f));
  }

  return reachable;
}

StatusOr<flat_hash_set<Node>> DependencyClosure::InitFontNodes(
    const flat_hash_set<Node>& previous_init_font_nodes,
    flat_hash_set<Node>& new_nodes) const {
  flat_hash_set<Node> init_font_nodes;

  init_font_nodes.insert(Node::InitFont());
  for (glyph_id_t g : segmentation_info_->InitFontGlyphs()) {
    init_font_nodes.insert(Node::Glyph(g));
  }
  for (hb_codepoint_t c : segmentation_info_->InitFontSegment().codepoints) {
    init_font_nodes.insert(Node::Unicode(c));
  }
  auto init_features = TRY(graph_.InitFontFeatureSet());
  for (hb_tag_t f : init_features) {
    init_font_nodes.insert(Node::Feature(f));
  }

  new_nodes = init_font_nodes;
  for (const auto& n : previous_init_font_nodes) {
    new_nodes.erase(n);
  }
  return init_font_nodes;
}

static IntSet ActiveClosurePhases(hb_face_t* face) {
  IntSet out;
  flat_hash_set<hb_tag_t> table_tags = FontHelper::GetTags(face);
  for (uint32_t phase = 0; phase < DependencyGraph::kNumberOfClosurePhases;
       phase++) {
    hb_tag_t table = DependencyGraph::kClosurePhaseTable[phase];
    if (table_tags.contains(table)) {
      out.insert(phase);
    }
  }
  return out;
}

StatusOr<const flat_hash_map<Node, std::vector<EdgeConditionsCnf>>&>
DependencyClosure::IncomingEdgesForClosurePhase(
    uint32_t phase, bool full_recalc, const flat_hash_set<Node>* node_filter) {
  enum UpdateType { None, Partial, Full };

  hb_tag_t table = DependencyGraph::kClosurePhaseTable[phase];
  uint32_t source_type_filter = DependencyGraph::kClosurePhaseNodeFilter[phase];
  uint32_t dest_type_filter = DependencyGraph::kClosurePhaseNodeFilter[phase];
  auto& prev_incoming_edges = incoming_edges_cache_[phase];

  UpdateType update_type = None;
  if (full_recalc || !prev_incoming_edges.has_value()) {
    update_type = Full;
    node_filter = nullptr;
  } else if (source_type_filter & Node::SEGMENT) {
    // Init font changes change the outgoing links on SEGMENT and/or INIT_FONT
    // nodes so if a phase includes these then a partial update is needed. Only
    // needs to consider link types in the cmap phase (segment/init font ->
    // unicode/feature, unicode -> unicode, unicode -> glyph)
    update_type = Partial;
    source_type_filter =
        (Node::SEGMENT | Node::UNICODE | Node::INIT_FONT) & source_type_filter;
    dest_type_filter =
        (Node::UNICODE | Node::FEATURE | Node::GLYPH) & dest_type_filter;
  }

  if (update_type == None) {
    return *prev_incoming_edges;
  }

  auto new_incoming_edges = TRY(graph_.CollectIncomingEdges(
      {table}, source_type_filter, dest_type_filter, node_filter));

  if (update_type == Full) {
    prev_incoming_edges = std::move(new_incoming_edges);
  } else {
    for (const auto& [n, edges] : new_incoming_edges) {
      prev_incoming_edges->insert_or_assign(n, std::move(edges));
    }
  }

  return *prev_incoming_edges;
}

Status DependencyClosure::UpdateAllNodeConditions(
    const SegmentSet& changed_segments) {
  // In rare cases the full closure can grow after an init font definition
  // change, if that happens we need to recompute all incoming edges to capture
  // new edges from the change.
  bool full_closure_changed =
      last_seen_full_closure_size_.has_value() &&
      *last_seen_full_closure_size_ != segmentation_info_->FullClosure().size();
  last_seen_full_closure_size_ = segmentation_info_->FullClosure().size();

  IntSet phases = ActiveClosurePhases(original_face_.get());

  flat_hash_set<Node> new_init_nodes;
  init_font_nodes_ = TRY(InitFontNodes(init_font_nodes_, new_init_nodes));

  // Find all possibly affected nodes, reset their conditions.
  flat_hash_set<Node> reachable =
      TRY(ReachableNonInitFontNodes(changed_segments, new_init_nodes));
  for (const auto& n : reachable) {
    node_condition_cache_.erase(n);
    for (uint32_t phase : phases) {
      phase_node_condition_cache_[phase].erase(n);
    }
  }

  flat_hash_set<Node> reachable_with_changed_init_nodes = reachable;
  reachable_with_changed_init_nodes.insert(Node::InitFont());
  reachable_with_changed_init_nodes.insert(new_init_nodes.begin(),
                                           new_init_nodes.end());

  // This sets up the conditions for all init font and segment nodes, works with
  // either existing or new condition calculations.
  TRYV(InitializeConditions(node_condition_cache_, changed_segments,
                            new_init_nodes));
  for (uint32_t phase : phases) {
    TRYV(InitializeConditions(phase_node_condition_cache_[phase],
                              changed_segments, new_init_nodes));
  }

  flat_hash_set<Node> modified;
  for (uint32_t phase : phases) {
    hb_tag_t table = DependencyGraph::kClosurePhaseTable[phase];
    auto next_phase = phases.lower_bound(phase);
    next_phase++;

    auto sccs = TRY(graph_.StronglyConnectedComponents(
        {table}, DependencyGraph::kClosurePhaseNodeFilter[phase], &reachable));

    const auto& incoming_edges = TRY(IncomingEdgesForClosurePhase(
        phase, full_closure_changed, &reachable_with_changed_init_nodes));

    TRYV(PropagateConditions(incoming_edges, sccs,
                             phase_node_condition_cache_[phase], modified));

    // Transfer the modified conditions forward to the next phase (or final
    // output)
    flat_hash_map<dep_graph::Node, ActivationCondition>* next_phase_conditions =
        &node_condition_cache_;
    if (next_phase != phases.end()) {
      next_phase_conditions = &phase_node_condition_cache_[*next_phase];
    }
    for (const auto& n : modified) {
      next_phase_conditions->insert_or_assign<>(
          n, phase_node_condition_cache_[phase].at(n));
    }
  }

  return absl::OkStatus();
}

SegmentSet DependencyClosure::ComputeInertSegments(
    const absl::flat_hash_map<glyph_id_t, ActivationCondition>& conditions)
    const {
  SegmentSet candidates = segmentation_info_->NonEmptySegments();
  for (const auto& [_, condition] : conditions) {
    if (!condition.IsUnitary()) {
      candidates.subtract(condition.TriggeringSegments());
    }
  }
  return candidates;
}

}  // namespace ift::encoder