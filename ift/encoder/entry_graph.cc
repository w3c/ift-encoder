#include "ift/encoder/entry_graph.h"

#include <algorithm>
#include <cstdint>

#include "absl/strings/str_cat.h"
#include "ift/common/axis_range.h"
#include "ift/common/try.h"
#include "ift/encoder/activation_condition.h"
#include "ift/proto/patch_encoding.h"
#include "ift/proto/patch_map.h"

using absl::btree_map;
using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::Span;
using absl::StatusOr;
using ift::common::AxisRange;
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
  std::vector<PatchMap::Entry> entries;
  entries.reserve(nodes.size());

  flat_hash_map<uint32_t, uint32_t> node_id_to_entry_index;
  flat_hash_map<uint32_t, ActivationCondition> node_to_condition;

  for (const auto& [condition, patches] : patch_mappings) {
    auto it = condition_to_node_id.find(condition);
    if (it != condition_to_node_id.end()) {
      node_to_condition.insert({it->second, condition});
    } else {
      return absl::InternalError("Unexpected missing node.");
    }
  }

  uint32_t last_patch_id = 0;
  std::vector<uint32_t> sorted_nodes = TRY(TopologicalSort());
  std::reverse(sorted_nodes.begin(), sorted_nodes.end());
  for (uint32_t node_id : sorted_nodes) {
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
  std::vector<uint32_t> sorted;
  sorted.reserve(nodes.size());

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
    if (edge_count[it->second] == 0) {
      sorted.push_back(it->second);
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

  // TODO XXXX handle case with more then 127 child ids
  if (normalized.IsPurelyDisjunctive()) {
    const auto& or_segments = *normalized.conditions().begin();
    nodes[node_id].child_mode = OR;
    for (const auto& s : or_segments) {
      uint32_t child_id = TRY(
          AddMapping(ActivationCondition::exclusive_segment(s, 0), segments));
      nodes[node_id].children_ids.insert(child_id);
    }
    return node_id;
  }

  nodes[node_id].child_mode = AND;
  for (const auto& or_segments : normalized.conditions()) {
    uint32_t child_id = TRY(
        AddMapping(ActivationCondition::or_segments(or_segments, 0), segments));
    nodes[node_id].children_ids.insert(child_id);
  }

  return node_id;
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
      .id = node_id,
      .and_codepoints = {},
      .and_features = {},
      .child_mode = NONE,
      .children_ids = {},
  });
  incoming_edge_count.push_back(0);
  return node_id;
}

int64_t EntryNode::EncodingCost() const {
  // TODO XXXX
  return 0;
}

void EntryGraph::Optimize() {
  // TODO XXXXX
}

}  // namespace ift::encoder
