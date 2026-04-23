#include "ift/encoder/entry_graph.h"

#include <algorithm>
#include <cstdint>

#include "absl/strings/str_cat.h"
#include "ift/common/axis_range.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/encoder/activation_condition.h"
#include "ift/proto/format_2_patch_map.h"
#include "ift/proto/patch_encoding.h"
#include "ift/proto/patch_map.h"

using absl::btree_map;
using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::Span;
using absl::Status;
using absl::StatusOr;
using ift::common::AxisRange;
using ift::common::CodepointSet;
using ift::common::IntSet;
using ift::proto::Format2PatchMap;
using ift::proto::PatchEncoding;
using ift::proto::PatchMap;

namespace ift::encoder {

static ActivationCondition Normalize(const ActivationCondition& condition) {
  // Remove patches, and other settings not relevant to the graph. Internally
  // composite condition will simplify and normalize as well.
  return ActivationCondition::composite_condition(condition.conditions(), 0);
}

StatusOr<EntryGraph> EntryGraph::Create(
    Span<const ActivationCondition> conditions,
    const flat_hash_map<segment_index_t, SubsetDefinition>& segments) {
  EntryGraph graph;
  for (const auto& condition : conditions) {
    uint32_t _ = TRY(graph.AddMapping(condition, segments));
    ActivationCondition normalized = Normalize(condition);

    std::vector<uint32_t> patches = {condition.activated()};
    patches.insert(patches.end(), condition.prefetches().begin(),
                   condition.prefetches().end());
    auto [it1, did_insert_1] =
        graph.patch_mappings.insert({normalized, patches});
    if (!did_insert_1) {
      return absl::InternalError("Conflicting patch mappings.");
    }
    auto [it2, did_insert_2] =
        graph.patch_encodings.insert({normalized, condition.Encoding()});
    if (!did_insert_2) {
      return absl::InternalError("Conflicting patch mappings.");
    }
  }

  return graph;
}

StatusOr<std::vector<PatchMap::Entry>> EntryGraph::ToPatchMapEntries(
    proto::PatchEncoding default_encoding) const {
  IntSet reachable = ReachableNodes();
  std::vector<PatchMap::Entry> entries;
  entries.reserve(reachable.size());

  flat_hash_map<uint32_t, ActivationCondition> node_to_condition;
  for (const auto& [condition, _] : patch_mappings) {
    auto it = condition_to_node_id.find(condition);
    if (it != condition_to_node_id.end()) {
      if (!node_to_condition.insert({it->second, condition}).second) {
        return absl::InternalError("Duplicate condtion.");
      }
    } else {
      return absl::InternalError("Unexpected missing node.");
    }
  }

  uint32_t last_patch_id = 0;
  std::vector<uint32_t> sorted_nodes = TRY(TopologicalSort());
  std::reverse(sorted_nodes.begin(), sorted_nodes.end());
  flat_hash_map<uint32_t, uint32_t> node_id_to_entry_index;
  for (uint32_t node_id : sorted_nodes) {
    if (!reachable.contains(node_id)) continue;

    const auto& node = nodes[node_id];

    PatchMap::Entry entry;
    entry.coverage.codepoints = node.and_codepoints;
    entry.coverage.features.insert(node.and_features.begin(),
                                   node.and_features.end());
    entry.coverage.design_space.insert(node.and_design_space.begin(),
                                       node.and_design_space.end());
    entry.coverage.conjunctive = (node.child_mode == AND);
    entry.encoding = default_encoding;

    for (uint32_t child_id : node.children_ids) {
      auto it = node_id_to_entry_index.find(child_id);
      if (it == node_id_to_entry_index.end()) {
        return absl::InternalError(absl::StrCat(
            "Child node ", child_id, " not yet converted to entry."));
      }
      entry.coverage.child_indices.insert(it->second);
    }

    auto condition_it = node_to_condition.find(node_id);
    if (condition_it != node_to_condition.end()) {
      entry.ignored = false;
      entry.patch_indices = patch_mappings.at(condition_it->second);
      entry.encoding = patch_encodings.at(condition_it->second);
      if (!entry.patch_indices.empty()) {
        last_patch_id = entry.patch_indices.back();
      }
    } else {
      entry.ignored = true;
      entry.patch_indices = {++last_patch_id};
    }

    node_id_to_entry_index[node_id] = entries.size();
    entries.push_back(std::move(entry));
  }

  return entries;
}

StatusOr<std::vector<uint32_t>> EntryGraph::TopologicalSort() const {
  std::vector<uint32_t> edge_count = incoming_edge_count;

  // Each top level condition generates an incoming edge we need to remove these
  // prior to sorting. patch_mappings stores a list of them.
  for (const auto& [condition, _] : patch_mappings) {
    auto it = condition_to_node_id.find(condition);
    if (it == condition_to_node_id.end()) {
      continue;
    }
    if (edge_count[it->second] == 0) {
      return absl::InternalError(
          "Edge count underflow for top-level condition.");
    }
    edge_count[it->second]--;
  }

  std::vector<uint32_t> sorted;
  sorted.reserve(nodes.size());
  for (uint32_t i = 0; i < edge_count.size(); i++) {
    if (edge_count[i] == 0) {
      sorted.push_back(i);
    }
  }

  std::sort(sorted.begin(), sorted.end());

  // Kahn's algorithm (ref: https://en.wikipedia.org/wiki/Topological_sorting)
  size_t head = 0;
  while (head < sorted.size()) {
    uint32_t id = sorted[head++];
    for (uint32_t child : nodes[id].children_ids) {
      if (edge_count[child] == 0) {
        return absl::InternalError("Edge count underflow.");
      }
      edge_count[child]--;
      if (edge_count[child] == 0) {
        sorted.push_back(child);
      }
    }
  }

  if (sorted.size() != nodes.size()) {
    return absl::InternalError("Cycle detected in graph.");
  }

  return sorted;
}

StatusOr<uint32_t> EntryGraph::AddMapping(
    const ActivationCondition& condition,
    const flat_hash_map<segment_index_t, SubsetDefinition>& segments) {
  ActivationCondition normalized = Normalize(condition);

  auto existing = condition_to_node_id.find(normalized);
  if (existing != condition_to_node_id.end()) {
    incoming_edge_count[existing->second]++;
    return existing->second;
  }

  if (normalized.IsUnitary()) {
    const auto& segment = segments.at(*normalized.TriggeringSegments().begin());
    if (!segment.codepoints.empty() && segment.feature_tags.empty() &&
        segment.design_space.empty()) {
      uint32_t node_id = TRY(AddCodepointsOnly(segment.codepoints));
      condition_to_node_id[normalized] = node_id;
      return node_id;
    } else if (segment.codepoints.empty() && !segment.feature_tags.empty() &&
               segment.design_space.empty()) {
      uint32_t node_id = TRY(AddFeaturesOnly(segment.feature_tags));
      condition_to_node_id[normalized] = node_id;
      return node_id;
    } else if (segment.codepoints.empty() && segment.feature_tags.empty() &&
               !segment.design_space.empty()) {
      uint32_t node_id = TRY(AddDesignSpaceOnly(segment.design_space));
      condition_to_node_id[normalized] = node_id;
      return node_id;
    }
  }

  uint32_t node_id = TRY(CreateNode());
  incoming_edge_count[node_id]++;
  condition_to_node_id[normalized] = node_id;

  if (normalized.IsUnitary()) {
    // A unitary condition that has not been handled yet has both features and
    // codepoints and needs to be formed from two child nodes. One for the
    // features and one for the codepoints.
    const auto& segment = segments.at(*normalized.TriggeringSegments().begin());
    if (!segment.codepoints.empty()) {
      uint32_t codepoints_id = TRY(AddCodepointsOnly(segment.codepoints));
      nodes[node_id].children_ids.insert(codepoints_id);
    }
    if (!segment.feature_tags.empty()) {
      uint32_t features_id = TRY(AddFeaturesOnly(segment.feature_tags));
      nodes[node_id].children_ids.insert(features_id);
    }
    if (!segment.design_space.empty()) {
      uint32_t design_space_id = TRY(AddDesignSpaceOnly(segment.design_space));
      nodes[node_id].children_ids.insert(design_space_id);
    }

    nodes[node_id].child_mode = OR;
    return node_id;
  }

  if (normalized.IsPurelyDisjunctive()) {
    const auto& or_segments = *normalized.conditions().begin();
    nodes[node_id].child_mode = OR;
    std::vector<uint32_t> children;
    for (const auto& s : or_segments) {
      children.push_back(TRY(
          AddMapping(ActivationCondition::exclusive_segment(s, 0), segments)));
    }
    TRYV(AddChildrenToNode(node_id, OR, children));
    return node_id;
  }

  nodes[node_id].child_mode = AND;
  std::vector<uint32_t> children;
  for (const auto& or_segments : normalized.conditions()) {
    children.push_back(TRY(AddMapping(
        ActivationCondition::or_segments(or_segments, 0), segments)));
  }
  TRYV(AddChildrenToNode(node_id, AND, children));

  return node_id;
}

absl::Status EntryGraph::AddChildrenToNode(uint32_t node_id, ChildMode mode,
                                           Span<const uint32_t> children_ids) {
  while (children_ids.size() > 127) {
    for (size_t i = 0; i < 126; ++i) {
      nodes[node_id].children_ids.insert(children_ids[i]);
    }
    uint32_t next_node_id = TRY(CreateNode());
    nodes[next_node_id].child_mode = mode;
    incoming_edge_count[next_node_id]++;
    nodes[node_id].children_ids.insert(next_node_id);

    node_id = next_node_id;
    children_ids = children_ids.subspan(126);
  }

  for (uint32_t child_id : children_ids) {
    nodes[node_id].children_ids.insert(child_id);
  }
  return absl::OkStatus();
}

StatusOr<uint32_t> EntryGraph::AddCodepointsOnly(
    const common::CodepointSet& codepoints) {
  auto existing = codepoints_to_node_id.find(codepoints);
  if (existing != codepoints_to_node_id.end()) {
    incoming_edge_count[existing->second]++;
    return existing->second;
  }

  uint32_t node_id = TRY(CreateNode());
  incoming_edge_count[node_id]++;
  codepoints_to_node_id[codepoints] = node_id;
  nodes[node_id].and_codepoints = codepoints;
  return node_id;
}

StatusOr<uint32_t> EntryGraph::AddFeaturesOnly(
    const btree_set<hb_tag_t>& features) {
  flat_hash_set<hb_tag_t> features_set;
  features_set.insert(features.begin(), features.end());

  auto existing = features_to_node_id.find(features_set);
  if (existing != features_to_node_id.end()) {
    incoming_edge_count[existing->second]++;
    return existing->second;
  }

  uint32_t node_id = TRY(CreateNode());
  incoming_edge_count[node_id]++;
  features_to_node_id[features_set] = node_id;
  nodes[node_id].and_features = features_set;
  return node_id;
}

// Returns node id. De-dups if possible.
StatusOr<uint32_t> EntryGraph::AddDesignSpaceOnly(
    const flat_hash_map<hb_tag_t, AxisRange>& design_space) {
  auto existing = design_space_to_node_id.find(design_space);
  if (existing != design_space_to_node_id.end()) {
    incoming_edge_count[existing->second]++;
    return existing->second;
  }

  uint32_t node_id = TRY(CreateNode());
  incoming_edge_count[node_id]++;
  design_space_to_node_id[design_space] = node_id;
  nodes[node_id].and_design_space = design_space;
  return node_id;
}

StatusOr<uint32_t> EntryGraph::CreateNode() {
  if (nodes.size() > UINT32_MAX) {
    return absl::InternalError("Node ID integer overflow.");
  }
  uint32_t node_id = nodes.size();
  nodes.push_back({
      .and_codepoints = {},
      .and_features = {},
      .and_design_space = {},
      .child_mode = NONE,
      .children_ids = {},
  });
  incoming_edge_count.push_back(0);
  return node_id;
}

IntSet EntryGraph::ReachableNodes() const {
  IntSet reachable;
  for (uint32_t n = 0; n < nodes.size(); n++) {
    if (incoming_edge_count[n] > 0) {
      reachable.insert(n);
    }
  }
  return reachable;
}

absl::StatusOr<int64_t> EntryNode::EncodingCost() const {
  proto::PatchMap::Entry entry;
  entry.coverage.codepoints = and_codepoints;
  entry.coverage.features.insert(and_features.begin(), and_features.end());
  entry.coverage.design_space.insert(and_design_space.begin(),
                                     and_design_space.end());
  entry.coverage.conjunctive = (child_mode == AND);

  uint32_t fake_id = 1;
  for (size_t i = 0; i < children_ids.size(); ++i) {
    entry.coverage.child_indices.insert(fake_id++);
  }

  return TRY(Format2PatchMap::EstimateEncodingCost(entry));
}

Status EntryGraph::Optimize() {
  bool changed = true;
  while (changed) {
    changed = false;
    std::vector<uint32_t> sorted = TRY(TopologicalSort());
    std::reverse(sorted.begin(), sorted.end());
    // Iterate in reverse topological order so that changes
    // made to the graph don't impact the following nodes
    // in the iteration.
    for (uint32_t node_id : sorted) {
      auto result = TRY(CalculateSubsumptionCostDelta(node_id));
      if (result.cost_delta < 0) {
        TRYV(ActuateSubsumption(node_id, result));
        changed = true;
      }
    }
  }
  return absl::OkStatus();
}

static absl::Status UnionAxisRanges(
    flat_hash_map<hb_tag_t, AxisRange>& result,
    const flat_hash_map<hb_tag_t, AxisRange>& other) {
  for (const auto& [tag, other_range] : other) {
    auto [it, inserted] = result.insert({tag, other_range});
    if (inserted) {
      continue;
    }

    if (it->second == other_range) {
      continue;
    }

    if (!it->second.Intersects(other_range) &&
        it->second.end() != other_range.start() &&
        other_range.end() != it->second.start()) {
      return absl::InvalidArgumentError("Cannot union non-contiguous ranges.");
    }

    float start = std::min(it->second.start(), other_range.start());
    float end = std::max(it->second.end(), other_range.end());
    it->second = TRY(AxisRange::Range(start, end));
  }

  return absl::OkStatus();
}

struct DisjunctiveSummary {
  common::CodepointSet codepoints;
  flat_hash_set<hb_tag_t> features;
  flat_hash_map<hb_tag_t, AxisRange> design_space;
  bool is_purely_disjunctive = true;

  void Clear() {
    codepoints.clear();
    features.clear();
    design_space.clear();
    is_purely_disjunctive = false;
  }
};

static DisjunctiveSummary GetDisjunctiveSummary(
    uint32_t node_id, const std::vector<EntryNode>& nodes) {
  const auto& node = nodes[node_id];
  DisjunctiveSummary summary;
  summary.is_purely_disjunctive = true;

  if (!node.children_ids.empty() &&
      (node.child_mode != OR || !node.and_codepoints.empty() ||
       !node.and_design_space.empty() || !node.and_features.empty())) {
    summary.is_purely_disjunctive = false;
    return summary;
  }

  uint32_t non_empty_sets = !node.and_codepoints.empty() +
                            !node.and_features.empty() +
                            !node.and_design_space.empty();
  if (non_empty_sets > 1) {
    summary.is_purely_disjunctive = false;
    return summary;
  }

  summary.codepoints = node.and_codepoints;
  summary.features = node.and_features;
  summary.design_space = node.and_design_space;

  for (uint32_t child_id : node.children_ids) {
    DisjunctiveSummary child_summary = GetDisjunctiveSummary(child_id, nodes);
    if (!child_summary.is_purely_disjunctive) {
      summary.Clear();
      return summary;
    }
    summary.codepoints.union_set(child_summary.codepoints);
    summary.features.insert(child_summary.features.begin(),
                            child_summary.features.end());
    auto status =
        UnionAxisRanges(summary.design_space, child_summary.design_space);
    if (!status.ok()) {
      summary.Clear();
      return summary;
    }
  }

  return summary;
}

static absl::StatusOr<int64_t> SavedCostIfEdgeToRemoved(
    uint32_t node_id,
    std::vector<uint32_t>&
        edge_counts,
    const std::vector<EntryNode>& nodes) {
  if (edge_counts[node_id] == 0) return 0;
  edge_counts[node_id]--;
  if (edge_counts[node_id] > 0) return 0;

  const auto& node = nodes[node_id];
  int64_t cost = TRY(node.EncodingCost());
  for (uint32_t child_id : node.children_ids) {
    cost += TRY(SavedCostIfEdgeToRemoved(child_id, edge_counts, nodes));
  }
  return cost;
}

StatusOr<SubsumptionResult> EntryGraph::CalculateSubsumptionCostDelta(
    uint32_t node_id) const {
  const auto& node = nodes[node_id];
  if (node.child_mode == OR || node.child_mode == NONE) {
    return CalculateDisjunctiveSubsumption(node_id);
  } else if (node.child_mode == AND) {
    return CalculateConjunctiveSubsumption(node_id);
  }
  SubsumptionResult result;
  result.cost_delta = 0;
  return result;
}

StatusOr<SubsumptionResult> EntryGraph::CalculateDisjunctiveSubsumption(
    uint32_t node_id) const {
  const auto& node = nodes[node_id];
  SubsumptionResult result;
  result.cost_delta = 0;

  int64_t current_node_cost = TRY(node.EncodingCost());

  DisjunctiveSummary summary = GetDisjunctiveSummary(node_id, nodes);
  if (!summary.is_purely_disjunctive || node.children_ids.empty()) {
    return result;
  }

  EntryNode test_node;
  int64_t extra_child_cost = 0;
  int non_empty_sets = (!summary.codepoints.empty()) +
                       (!summary.features.empty()) +
                       (!summary.design_space.empty());

  if (non_empty_sets <= 1) {
    // The single set case can be fully encoded in just the test
    // node
    test_node.and_codepoints = summary.codepoints;
    test_node.and_features = summary.features;
    test_node.and_design_space = summary.design_space;
    test_node.child_mode = NONE;
  } else {
    // Otherwise one child node is needed per non-empty set.
    test_node.child_mode = OR;
    if (!summary.codepoints.empty()) {
      EntryNode child;
      child.and_codepoints = summary.codepoints;
      extra_child_cost += TRY(child.EncodingCost());
      test_node.children_ids.insert(0);
    }
    if (!summary.features.empty()) {
      EntryNode child;
      child.and_features = summary.features;
      extra_child_cost += TRY(child.EncodingCost());
      test_node.children_ids.insert(1);
    }
    if (!summary.design_space.empty()) {
      EntryNode child;
      child.and_design_space = summary.design_space;
      extra_child_cost += TRY(child.EncodingCost());
      test_node.children_ids.insert(2);
    }
  }

  int64_t new_cost = TRY(test_node.EncodingCost()) + extra_child_cost;
  std::vector<uint32_t> edge_counts = incoming_edge_count;
  int64_t saved_cost = 0;
  for (uint32_t child_id : node.children_ids) {
    saved_cost += TRY(SavedCostIfEdgeToRemoved(child_id, edge_counts, nodes));
  }

  result.cost_delta = new_cost - current_node_cost - saved_cost;
  result.subsumed_children.union_set(node.children_ids);
  if (!summary.codepoints.empty()) {
    result.codepoints = summary.codepoints;
  }
  if (!summary.features.empty()) {
    result.features = summary.features;
  }
  if (!summary.design_space.empty()) {
    result.design_space = summary.design_space;
  }

  return result;
}

StatusOr<SubsumptionResult> EntryGraph::CalculateConjunctiveSubsumption(
    uint32_t node_id) const {
  const auto& node = nodes[node_id];
  SubsumptionResult best_result;
  best_result.cost_delta = INT64_MAX;

  if (!node.and_codepoints.empty() && !node.and_features.empty() &&
      !node.and_design_space.empty()) {
    // Must have at least one free set to subsume a child node into.
    return best_result;
  }

  int64_t current_node_cost = TRY(node.EncodingCost());

  struct ChildCandidate {
    uint32_t id;
    CodepointSet codepoints;
    flat_hash_set<hb_tag_t> features;
    flat_hash_map<hb_tag_t, AxisRange> design_space;
  };

  std::vector<ChildCandidate> candidates;
  for (uint32_t child_id : node.children_ids) {
    DisjunctiveSummary s = GetDisjunctiveSummary(child_id, nodes);
    if (!s.is_purely_disjunctive) continue;

    uint32_t non_empty_sets = (!s.codepoints.empty()) + (!s.features.empty()) +
                              (!s.design_space.empty());
    if (non_empty_sets != 1) {
      continue;
    }

    // Filter out candidates where there isn't an empty set to subsume into.
    if (!s.codepoints.empty() && !node.and_codepoints.empty()) {
      continue;
    }
    if (!s.features.empty() && !node.and_features.empty()) {
      continue;
    }
    if (!s.design_space.empty() && !node.and_design_space.empty()) {
      continue;
    }

    candidates.push_back({child_id, s.codepoints, s.features, s.design_space});
  }

  std::vector<const ChildCandidate*> cps = {nullptr};
  std::vector<const ChildCandidate*> feats = {nullptr};
  std::vector<const ChildCandidate*> dss = {nullptr};
  for (const auto& c : candidates) {
    if (!c.codepoints.empty()) {
      cps.push_back(&c);
    }
    if (!c.features.empty()) {
      feats.push_back(&c);
    }
    if (!c.design_space.empty()) {
      dss.push_back(&c);
    }
  }

  auto test_combo = [&](const ChildCandidate* cp, const ChildCandidate* feat,
                        const ChildCandidate* ds) -> absl::Status {
    EntryNode test_node = node;
    IntSet subsumed;
    if (cp) {
      test_node.and_codepoints = cp->codepoints;
      test_node.children_ids.erase(cp->id);
      subsumed.insert(cp->id);
    }
    if (feat) {
      test_node.and_features = feat->features;
      test_node.children_ids.erase(feat->id);
      subsumed.insert(feat->id);
    }
    if (ds) {
      test_node.and_design_space = ds->design_space;
      test_node.children_ids.erase(ds->id);
      subsumed.insert(ds->id);
    }

    if (subsumed.empty()) return absl::OkStatus();

    int64_t new_cost = TRY(test_node.EncodingCost());
    std::vector<uint32_t> edge_counts = incoming_edge_count;
    int64_t saved_cost = 0;
    for (uint32_t id : subsumed) {
      saved_cost += TRY(SavedCostIfEdgeToRemoved(id, edge_counts, nodes));
    }

    int64_t delta = new_cost - current_node_cost - saved_cost;
    if (delta < best_result.cost_delta) {
      best_result.cost_delta = delta;
      best_result.subsumed_children = subsumed;
      best_result.codepoints = test_node.and_codepoints;
      best_result.features = test_node.and_features;
      best_result.design_space = test_node.and_design_space;
    }
    return absl::OkStatus();
  };

  for (auto cp : cps) {
    for (auto feat : feats) {
      for (auto ds : dss) {
        TRYV(test_combo(cp, feat, ds));
      }
    }
  }

  return best_result;
}

static void DecrementIncomingEdges(uint32_t node_id,
                                   std::vector<uint32_t>& edge_counts,
                                   std::vector<EntryNode>& nodes) {
  if (edge_counts[node_id] == 0) {
    return;
  }

  edge_counts[node_id]--;
  if (edge_counts[node_id] == 0) {
    for (uint32_t child_id : nodes[node_id].children_ids) {
      DecrementIncomingEdges(child_id, edge_counts, nodes);
    }
    nodes[node_id] = EntryNode();  // node is removed, clear it's data.
  }
}

Status EntryGraph::ActuateSubsumption(uint32_t node_id,
                                      const SubsumptionResult& result) {
  if (result.subsumed_children.empty()) return absl::OkStatus();

  for (uint32_t child_id : result.subsumed_children) {
    DecrementIncomingEdges(child_id, incoming_edge_count, nodes);
    nodes[node_id].children_ids.erase(child_id);
  }

  auto& node = nodes[node_id];
  if (node.child_mode == OR || node.child_mode == NONE) {
    int non_empty = result.codepoints.has_value() +
                    result.features.has_value() +
                    result.design_space.has_value();
    if (non_empty <= 1) {
      node.and_codepoints = result.codepoints.value_or(common::CodepointSet());
      node.and_features = result.features.value_or(flat_hash_set<hb_tag_t>());
      node.and_design_space =
          result.design_space.value_or(flat_hash_map<hb_tag_t, AxisRange>());
      node.child_mode = NONE;
    } else {
      node.and_codepoints = {};
      node.and_features = {};
      node.and_design_space = {};
      node.child_mode = OR;

      if (result.codepoints) {
        uint32_t id = TRY(CreateNode());
        nodes[id].and_codepoints = *result.codepoints;
        incoming_edge_count[id]++;
        nodes[node_id].children_ids.insert(id);
      }
      if (result.features) {
        uint32_t id = TRY(CreateNode());
        nodes[id].and_features = *result.features;
        incoming_edge_count[id]++;
        nodes[node_id].children_ids.insert(id);
      }
      if (result.design_space) {
        uint32_t id = TRY(CreateNode());
        nodes[id].and_design_space = *result.design_space;
        incoming_edge_count[id]++;
        nodes[node_id].children_ids.insert(id);
      }
    }
  } else if (node.child_mode == AND) {
    if (result.codepoints) node.and_codepoints = *result.codepoints;
    if (result.features) node.and_features = *result.features;
    if (result.design_space) node.and_design_space = *result.design_space;
  }

  return absl::OkStatus();
}

}  // namespace ift::encoder
