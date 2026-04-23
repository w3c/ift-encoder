#include "ift/encoder/entry_graph.h"

#include <vector>

#include "absl/container/flat_hash_map.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "ift/common/int_set.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/subset_definition.h"
#include "ift/proto/patch_encoding.h"

using ::absl::flat_hash_map;
using ::ift::common::CodepointSet;
using ::ift::common::SegmentSet;
using ::ift::proto::PatchEncoding;
using ::testing::UnorderedElementsAre;

namespace ift::encoder {

TEST(EntryGraphTest, ToPatchMapEntries) {
  flat_hash_map<segment_index_t, SubsetDefinition> segments = {
      {1, {'a'}},
      {2, {'b'}},
      {3, {'c'}},
  };

  std::vector<ActivationCondition> activation_conditions = {
      ActivationCondition::or_segments({1, 2}, 10),
      ActivationCondition::or_segments({2, 3}, 11),
      ActivationCondition::and_segments({1, 3}, 12),
      ActivationCondition::exclusive_segment(2, 13),
  };

  auto r1 = EntryGraph::Create(activation_conditions, segments);
  ASSERT_TRUE(r1.ok()) << r1.status();
  EntryGraph graph = std::move(r1).value();

  auto r2 = graph.ToPatchMapEntries(proto::TABLE_KEYED_FULL);
  ASSERT_TRUE(r2.ok()) << r2.status();
  std::vector<proto::PatchMap::Entry> entries = *r2;

  // Expected 6 nodes:
  // (s1 OR s2) -> 10
  // (s2 OR s3) -> 11
  // (s1 AND s3) -> 12
  // (s1) -> ignored
  // (s2) -> 13
  // (s3) -> ignored
  EXPECT_EQ(entries.size(), 6);

  flat_hash_map<uint32_t, size_t> codepoint_to_index;
  flat_hash_map<uint32_t, size_t> patch_to_index;

  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];

    // Check that child indices refer to earlier entries
    for (uint32_t child_idx : entry.coverage.child_indices) {
      EXPECT_LT(child_idx, i);
    }

    if (entry.ignored) {
      EXPECT_TRUE(entry.coverage.child_indices.empty());
      EXPECT_EQ(entry.coverage.codepoints.size(), 1);
      codepoint_to_index[*entry.coverage.codepoints.begin()] = i;
      EXPECT_EQ(entry.encoding, proto::TABLE_KEYED_FULL);
    } else {
      EXPECT_EQ(entry.encoding, proto::GLYPH_KEYED);
      EXPECT_EQ(entry.patch_indices.size(), 1);
      uint32_t patch_id = entry.patch_indices[0];
      patch_to_index[patch_id] = i;

      if (patch_id == 13) {
        EXPECT_TRUE(entry.coverage.child_indices.empty());
        EXPECT_EQ(entry.coverage.codepoints.size(), 1);
        codepoint_to_index[*entry.coverage.codepoints.begin()] = i;
      } else if (patch_id == 10) {
        EXPECT_FALSE(entry.coverage.conjunctive);
        EXPECT_EQ(entry.coverage.child_indices.size(), 2);
      } else if (patch_id == 11) {
        EXPECT_FALSE(entry.coverage.conjunctive);
        EXPECT_EQ(entry.coverage.child_indices.size(), 2);
      } else if (patch_id == 12) {
        EXPECT_TRUE(entry.coverage.conjunctive);
        EXPECT_EQ(entry.coverage.child_indices.size(), 2);
      }
    }
  }

  ASSERT_TRUE(codepoint_to_index.contains('a'));
  ASSERT_TRUE(codepoint_to_index.contains('b'));
  ASSERT_TRUE(codepoint_to_index.contains('c'));
  ASSERT_TRUE(patch_to_index.contains(10));
  ASSERT_TRUE(patch_to_index.contains(11));
  ASSERT_TRUE(patch_to_index.contains(12));
  ASSERT_TRUE(patch_to_index.contains(13));

  // s1 OR s2
  EXPECT_TRUE(entries[patch_to_index[10]].coverage.child_indices.contains(
      codepoint_to_index['a']));
  EXPECT_TRUE(entries[patch_to_index[10]].coverage.child_indices.contains(
      codepoint_to_index['b']));

  // s2 OR s3
  EXPECT_TRUE(entries[patch_to_index[11]].coverage.child_indices.contains(
      codepoint_to_index['b']));
  EXPECT_TRUE(entries[patch_to_index[11]].coverage.child_indices.contains(
      codepoint_to_index['c']));

  // s1 AND s3
  EXPECT_TRUE(entries[patch_to_index[12]].coverage.child_indices.contains(
      codepoint_to_index['a']));
  EXPECT_TRUE(entries[patch_to_index[12]].coverage.child_indices.contains(
      codepoint_to_index['c']));
}

TEST(EntryGraphTest, SplitSegment) {
  SubsetDefinition s1;
  s1.codepoints = {'a'};
  s1.feature_tags = {HB_TAG('f', '1', ' ', ' ')};
  flat_hash_map<segment_index_t, SubsetDefinition> segments = {
      {1, s1},
  };

  std::vector<ActivationCondition> activation_conditions = {
      ActivationCondition::exclusive_segment(1, 10),
  };

  auto r1 = EntryGraph::Create(activation_conditions, segments);
  ASSERT_TRUE(r1.ok()) << r1.status();
  EntryGraph graph = std::move(*r1);

  auto r2 = graph.ToPatchMapEntries(proto::GLYPH_KEYED);
  ASSERT_TRUE(r2.ok()) << r2.status();
  std::vector<proto::PatchMap::Entry> entries = *r2;

  // Expected 3 nodes:
  // (a OR f1) -> 10
  // (a) -> ignored
  // (f1) -> ignored
  EXPECT_EQ(entries.size(), 3);

  size_t a_id = -1;
  size_t f1_id = -1;
  size_t root_id = -1;

  for (size_t i = 0; i < entries.size(); i++) {
    const auto& entry = entries[i];
    if (entry.ignored) {
      EXPECT_TRUE(entry.coverage.child_indices.empty());
      if (!entry.coverage.codepoints.empty()) {
        EXPECT_EQ(*entry.coverage.codepoints.begin(), 'a');
        a_id = i;
      } else {
        EXPECT_EQ(*entry.coverage.features.begin(), HB_TAG('f', '1', ' ', ' '));
        f1_id = i;
      }
    } else {
      EXPECT_EQ(entry.patch_indices[0], 10);
      EXPECT_FALSE(entry.coverage.conjunctive);
      EXPECT_EQ(entry.coverage.child_indices.size(), 2);
      root_id = i;
    }
  }

  ASSERT_NE(a_id, -1);
  ASSERT_NE(f1_id, -1);
  ASSERT_NE(root_id, -1);

  EXPECT_TRUE(entries[root_id].coverage.child_indices.contains(a_id));
  EXPECT_TRUE(entries[root_id].coverage.child_indices.contains(f1_id));
  EXPECT_LT(a_id, root_id);
  EXPECT_LT(f1_id, root_id);
}

TEST(EntryGraphTest, TopologicalSort_SharedChildren) {
  flat_hash_map<segment_index_t, SubsetDefinition> segments = {
      {1, {'a'}},
      {2, {'b'}},
      {3, {'c'}},
  };

  std::vector<ActivationCondition> activation_conditions = {
      ActivationCondition::or_segments({1, 2}, 10),
      ActivationCondition::or_segments({2, 3}, 11),
  };

  auto graph = EntryGraph::Create(activation_conditions, segments);
  ASSERT_TRUE(graph.ok()) << graph.status();

  auto sorted = graph->TopologicalSort();
  ASSERT_TRUE(sorted.ok()) << sorted.status();

  // The graph should have 5 nodes:
  // 0: (s1 OR s2)
  // 1: (s1)
  // 2: (s2) shared
  // 3: (s2 or s3)
  // 4: (s3)
  EXPECT_THAT(*sorted, UnorderedElementsAre(0, 1, 2, 3, 4));

  auto n0 = std::find(sorted->begin(), sorted->end(), 0);
  auto n1 = std::find(sorted->begin(), sorted->end(), 1);
  auto n2 = std::find(sorted->begin(), sorted->end(), 2);
  auto n3 = std::find(sorted->begin(), sorted->end(), 3);
  auto n4 = std::find(sorted->begin(), sorted->end(), 4);

  // n0 comes before it's children
  EXPECT_LT(n0, n1);
  EXPECT_LT(n0, n2);

  // n3 comes before it's children
  EXPECT_LT(n3, n2);
  EXPECT_LT(n3, n4);
}

TEST(EntryGraphTest, SplitLargeNode) {
  flat_hash_map<segment_index_t, SubsetDefinition> segments;
  SegmentSet segment_ids;
  for (int i = 1; i <= 130; ++i) {
    segments[i].codepoints = {static_cast<hb_codepoint_t>(i)};
    segment_ids.insert(i);
  }

  std::vector<ActivationCondition> activation_conditions = {
      ActivationCondition::or_segments(segment_ids, 10),
  };

  auto graph = EntryGraph::Create(activation_conditions, segments);
  ASSERT_TRUE(graph.ok()) << graph.status();

  auto entries = graph->ToPatchMapEntries(proto::GLYPH_KEYED);
  ASSERT_TRUE(entries.ok()) << entries.status();

  // Expected 130 codepoint nodes, 1 root node, and 1 intermediate split node.
  EXPECT_EQ(entries->size(), 132);

  size_t root_id = -1;
  size_t split_node_id = -1;

  for (size_t i = 0; i < entries->size(); i++) {
    const auto& entry = (*entries)[i];
    if (!entry.ignored && !entry.patch_indices.empty() &&
        entry.patch_indices[0] == 10) {
      root_id = i;
    } else if (entry.ignored && entry.coverage.codepoints.empty() &&
               !entry.coverage.child_indices.empty()) {
      split_node_id = i;
    }
  }

  ASSERT_NE(root_id, -1);
  ASSERT_NE(split_node_id, -1);

  // The root node gets the first 126 children + the split node (total 127).
  EXPECT_EQ((*entries)[root_id].coverage.child_indices.size(), 127);
  EXPECT_TRUE(
      (*entries)[root_id].coverage.child_indices.contains(split_node_id));

  // The split node gets the remaining 4 children.
  EXPECT_EQ((*entries)[split_node_id].coverage.child_indices.size(), 4);
}

TEST(EntryGraphTest, NodeEncodingCost) {
  EntryNode node;
  node.and_codepoints.insert(1);
  node.and_codepoints.insert(2);
  node.child_mode = NONE;

  auto cost1 = node.EncodingCost();
  ASSERT_TRUE(cost1.ok()) << cost1.status();
  EXPECT_GT(*cost1, 0);

  node.children_ids.insert(1);
  node.children_ids.insert(2);

  auto cost2 = node.EncodingCost();
  ASSERT_TRUE(cost2.ok()) << cost2.status();
  EXPECT_GT(*cost2, *cost1);
}

TEST(EntryGraphTest, CalculateSubsumptionCostDelta_Disjunctive) {
  // s0 = {a}, s1 = {b}, s2 = {c}
  // (s0 or s1 or s2) -> {g1}
  flat_hash_map<segment_index_t, SubsetDefinition> segments = {
      {0, {'a'}},
      {1, {'b'}},
      {2, {'c'}},
  };

  std::vector<ActivationCondition> activation_conditions = {
      ActivationCondition::or_segments({0, 1, 2}, 10),
  };

  auto graph = EntryGraph::Create(activation_conditions, segments);
  ASSERT_TRUE(graph.ok()) << graph.status();

  // Find the root OR node.
  auto sorted = graph->TopologicalSort();
  ASSERT_TRUE(sorted.ok());
  uint32_t root_id = sorted->at(0);

  auto result = graph->CalculateSubsumptionCostDelta(root_id);
  ASSERT_TRUE(result.ok()) << result.status();

  // Combining 4 entries into one should lower cost by eliminating
  // per entry overhead.
  EXPECT_LT(result->cost_delta, 0);
  EXPECT_EQ(result->subsumed_children.size(), 3);
  EXPECT_EQ(result->codepoints, (CodepointSet{'a', 'b', 'c'}));
}

TEST(EntryGraphTest, CalculateSubsumptionCostDelta_Disjunctive_PositiveDelta) {
  // Children are shared and large. Subsuming them into the parent will duplicate
  // them. Cost of duplication outweights entry cost savings.

  SubsetDefinition s0, s1;
  // Insert non-continous codepoints to ensure the sparse bit sets take up
  // a sizable amount of bytes
  for (int i = 0; i < 100; i++) s0.codepoints.insert(i * 2);
  for (int i = 100; i < 200; i++) s1.codepoints.insert(i * 2);

  flat_hash_map<segment_index_t, SubsetDefinition> segments = {
      {0, s0},
      {1, s1},
  };

  std::vector<ActivationCondition> activation_conditions = {
      ActivationCondition::or_segments({0, 1}, 100),
      ActivationCondition::exclusive_segment(0, 101),
      ActivationCondition::exclusive_segment(1, 102),
  };

  auto graph = EntryGraph::Create(activation_conditions, segments);
  ASSERT_TRUE(graph.ok()) << graph.status();

  auto sorted = graph->TopologicalSort();
  ASSERT_TRUE(sorted.ok()) << sorted.status();

  // all nodes should have a positive/zero cost since the current configuration
  // is optimal
  bool found_children = false;
  for (uint32_t id : *sorted) {
    auto res = graph->CalculateSubsumptionCostDelta(id);
    ASSERT_TRUE(res.ok());
    ASSERT_GE(res->cost_delta, 0);
    if (res->subsumed_children.size() == 2) {
      found_children = true;
      ASSERT_GT(res->cost_delta, 0);
    }
  }
  ASSERT_TRUE(found_children);
}

TEST(EntryGraphTest, ActuateSubsumption_Disjunctive) {
  flat_hash_map<segment_index_t, SubsetDefinition> segments = {
      {0, {'a'}},
      {1, {'b'}},
      {2, {'c'}},
  };
  std::vector<ActivationCondition> activation_conditions = {
      ActivationCondition::or_segments({0, 1, 2}, 10),
  };
  auto graph = EntryGraph::Create(activation_conditions, segments);
  uint32_t root_id = graph->TopologicalSort()->at(0);

  auto result = graph->CalculateSubsumptionCostDelta(root_id);
  ASSERT_TRUE(result->cost_delta < 0);

  ASSERT_TRUE(graph->ActuateSubsumption(root_id, *result).ok());

  auto entries = graph->ToPatchMapEntries(proto::GLYPH_KEYED);
  ASSERT_TRUE(entries.ok()) << entries.status();
  // Should now have only 1 entry with all codepoints
  EXPECT_EQ(entries->size(), 1);
  EXPECT_THAT(entries->at(0).coverage.codepoints,
              UnorderedElementsAre('a', 'b', 'c'));
  EXPECT_TRUE(entries->at(0).coverage.child_indices.empty());
}

TEST(EntryGraphTest, ActuateSubsumption_Disjunctive_Mixed) {
  // s0, s1 = codepoints
  // s2, s3 = feature tags
  // (s0 or s1 or s2 or s3) -> {g1}
  SubsetDefinition s0, s1, s2, s3;
  s0.codepoints = {'a'};
  s1.codepoints = {'b'};
  s2.feature_tags = {HB_TAG('f', 'o', 'o', ' ')};
  s3.feature_tags = {HB_TAG('b', 'a', 'r', ' ')};

  flat_hash_map<segment_index_t, SubsetDefinition> segments = {
      {0, s0},
      {1, s1},
      {2, s2},
      {3, s3},
  };

  std::vector<ActivationCondition> activation_conditions = {
      ActivationCondition::or_segments({0, 1, 2, 3}, 10),
  };

  auto graph = EntryGraph::Create(activation_conditions, segments);
  ASSERT_TRUE(graph.ok()) << graph.status();

  auto sorted = graph->TopologicalSort();
  ASSERT_TRUE(sorted.ok()) << sorted.status();
  uint32_t root_id = sorted->at(0);

  auto result = graph->CalculateSubsumptionCostDelta(root_id);
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_LT(result->cost_delta, 0);

  ASSERT_TRUE(graph->ActuateSubsumption(root_id, *result).ok());

  auto entries = graph->ToPatchMapEntries(proto::GLYPH_KEYED);
  ASSERT_TRUE(entries.ok()) << entries.status();

  // Result should have:
  // E0: {a, b}
  // E1: features {'foo ', 'bar '}
  // E2: OR children {E0, E1}
  EXPECT_EQ(entries->size(), 3);

  const auto& root_entry = entries->back();
  EXPECT_TRUE(root_entry.coverage.codepoints.empty());
  EXPECT_TRUE(root_entry.coverage.features.empty());
  EXPECT_EQ(root_entry.coverage.child_indices.size(), 2);

  bool found_cp = false;
  bool found_feat = false;
  for (uint32_t child_idx : root_entry.coverage.child_indices) {
    const auto& child = entries->at(child_idx);
    if (!child.coverage.codepoints.empty()) {
      EXPECT_THAT(child.coverage.codepoints, UnorderedElementsAre('a', 'b'));
      EXPECT_TRUE(child.coverage.features.empty());
      found_cp = true;
    }
    if (!child.coverage.features.empty()) {
      EXPECT_THAT(child.coverage.features,
                  UnorderedElementsAre(HB_TAG('f', 'o', 'o', ' '),
                                       HB_TAG('b', 'a', 'r', ' ')));
      EXPECT_TRUE(child.coverage.codepoints.empty());
      found_feat = true;
    }
  }
  EXPECT_TRUE(found_cp);
  EXPECT_TRUE(found_feat);
}

TEST(EntryGraphTest, CalculateSubsumptionCostDelta_Conjunctive) {
  flat_hash_map<segment_index_t, SubsetDefinition> segments = {
      {0, {'a'}},
      {1, {'b'}},
      {2, {'x'}},
  };
  // (s0 OR s1) AND (s2)
  std::vector<ActivationCondition> activation_conditions = {
      ActivationCondition::composite_condition({{0, 1}, {2}}, 10),
  };
  auto graph = EntryGraph::Create(activation_conditions, segments);
  ASSERT_TRUE(graph.ok()) << graph.status();

  uint32_t root_id = graph->TopologicalSort()->at(0);

  auto result = graph->CalculateSubsumptionCostDelta(root_id);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_LT(result->cost_delta, 0);
  EXPECT_EQ(result->codepoints, (CodepointSet{'a', 'b'}));
  EXPECT_EQ(result->subsumed_children.size(), 1);
}

TEST(EntryGraphTest, ActuateSubsumption_Conjunctive) {
  flat_hash_map<segment_index_t, SubsetDefinition> segments = {
      {0, {'a'}},
      {1, {'b'}},
      {2, {'x'}},
  };
  // (s0 OR s1) AND (s2)
  std::vector<ActivationCondition> activation_conditions = {
      ActivationCondition::composite_condition({{0, 1}, {2}}, 10),
  };
  auto graph = EntryGraph::Create(activation_conditions, segments);
  ASSERT_TRUE(graph.ok()) << graph.status();
  uint32_t root_id = graph->TopologicalSort()->at(0);

  auto result = graph->CalculateSubsumptionCostDelta(root_id);
  ASSERT_TRUE(result->cost_delta < 0);

  ASSERT_TRUE(graph->ActuateSubsumption(root_id, *result).ok());

  auto entries = graph->ToPatchMapEntries(proto::GLYPH_KEYED);
  ASSERT_TRUE(entries.ok()) << entries.status();

  // One of the codepoint sets should be pulled up into the root node.
  EXPECT_EQ(entries->size(), 2);
  const auto& root_entry = entries->back();
  EXPECT_FALSE(root_entry.coverage.codepoints.empty());
  EXPECT_EQ(root_entry.coverage.child_indices.size(), 1);
}

TEST(EntryGraphTest, ActuateSubsumption_Conjunctive_Multi) {
  // Conjunctive node with one purely disjunctive codepoint child and one
  // purely disjunctive feature child. Both should be subsumed.
  SubsetDefinition s0, s1, s2;
  s0.codepoints = {'a', 'b'};
  s1.feature_tags = {HB_TAG('f', 'o', 'o', ' ')};
  s2.codepoints = {'x'};

  flat_hash_map<segment_index_t, SubsetDefinition> segments = {
      {0, s0},
      {1, s1},
      {2, s2},
  };

  // (s0 AND s1 AND s2) -> 10
  std::vector<ActivationCondition> activation_conditions = {
      ActivationCondition::and_segments({0, 1, 2}, 10),
  };

  auto graph = EntryGraph::Create(activation_conditions, segments);
  ASSERT_TRUE(graph.ok()) << graph.status();

  auto sorted = graph->TopologicalSort();
  ASSERT_TRUE(sorted.ok());
  uint32_t root_id = sorted->at(0);

  auto result = graph->CalculateSubsumptionCostDelta(root_id);
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_LT(result->cost_delta, 0);
  EXPECT_EQ(result->subsumed_children.size(), 2);

  ASSERT_TRUE(graph->ActuateSubsumption(root_id, *result).ok());

  auto entries = graph->ToPatchMapEntries(proto::GLYPH_KEYED);
  ASSERT_TRUE(entries.ok()) << entries.status();

  // Should have 2 entries now: root (AND node) and the remaining child s2.
  EXPECT_EQ(entries->size(), 2);
  const auto& root_entry = entries->back();
  EXPECT_THAT(root_entry.coverage.codepoints, UnorderedElementsAre('a', 'b'));
  EXPECT_THAT(root_entry.coverage.features,
              UnorderedElementsAre(HB_TAG('f', 'o', 'o', ' ')));
  EXPECT_EQ(root_entry.coverage.child_indices.size(), 1);

  // Verify the remaining child is s2 ({'x'})
  uint32_t s2_idx = *root_entry.coverage.child_indices.begin();
  EXPECT_THAT(entries->at(s2_idx).coverage.codepoints,
              UnorderedElementsAre('x'));
}

TEST(EntryGraphTest,
     CalculateSubsumptionCostDelta_Conjunctive_MixedChild_NoSubsume) {
  // Conjunctive node has a children which are mixed (codepoints + feature).
  // Effectively forming conditions like (a or foo) AND (b or  bar)
  // These mixed conditions can't be pulled up into the parent condition.

  SubsetDefinition s0{'a'};

  SubsetDefinition s1;
  s1.feature_tags = {HB_TAG('f', 'o', 'o', ' ')};

  SubsetDefinition s2{'b'};

  SubsetDefinition s3;
  s3.feature_tags = {HB_TAG('b', 'a', 'r', ' ')};

  flat_hash_map<segment_index_t, SubsetDefinition> segments = {
      {0, s0},
      {1, s1},
      {2, s2},
      {3, s3},
  };

  // Condition: (s0 OR s1) AND (s2 OR s3) => 10
  std::vector<ActivationCondition> activation_conditions = {
      ActivationCondition::composite_condition({{
                                                    0,
                                                    1,
                                                },
                                                {2, 3}},
                                               10)};

  auto graph = EntryGraph::Create(activation_conditions, segments);
  ASSERT_TRUE(graph.ok()) << graph.status();

  uint32_t root_id = graph->TopologicalSort()->at(0);

  auto result = graph->CalculateSubsumptionCostDelta(root_id);
  ASSERT_TRUE(result.ok());

  // No subsuming should happen, nothing is eligible
  EXPECT_GE(result->cost_delta, 0);
  EXPECT_TRUE(result->subsumed_children.empty());
  EXPECT_FALSE(result->codepoints.has_value());
  EXPECT_FALSE(result->features.has_value());
}

TEST(EntryGraphTest, CalculateSubsumptionCostDelta_Conjunctive_PositiveDelta) {
  SubsetDefinition s0, s1;
  for (int i = 0; i < 100; i++) s0.codepoints.insert(i * 2);
  for (int i = 100; i < 200; i++) s1.codepoints.insert(i * 2);

  flat_hash_map<segment_index_t, SubsetDefinition> segments = {
      {0, s0},
      {1, s1},
  };

  std::vector<ActivationCondition> activation_conditions = {
      ActivationCondition::and_segments({0, 1}, 100),
      ActivationCondition::exclusive_segment(0, 101),
      ActivationCondition::exclusive_segment(1, 102),
  };

  auto graph = EntryGraph::Create(activation_conditions, segments);
  ASSERT_TRUE(graph.ok()) << graph.status();

  auto sorted = graph->TopologicalSort();
  ASSERT_TRUE(sorted.ok());

  // all nodes should have a positive/zero cost since the current configuration
  // is optimal
  bool found_children = false;
  for (uint32_t id : *sorted) {
    auto res = graph->CalculateSubsumptionCostDelta(id);
    ASSERT_TRUE(res.ok());
    ASSERT_GE(res->cost_delta, 0);
    if (res->subsumed_children.size() == 1) {
      found_children = true;
      ASSERT_GT(res->cost_delta, 0);
    }
  }
  ASSERT_TRUE(found_children);
}

TEST(EntryGraphTest, Optimize_Disjunctive) {
  flat_hash_map<segment_index_t, SubsetDefinition> segments = {
      {0, {'a'}},
      {1, {'b'}},
      {2, {'c'}},
  };
  std::vector<ActivationCondition> activation_conditions = {
      ActivationCondition::or_segments({0, 1, 2}, 10),
  };
  auto graph_or = EntryGraph::Create(activation_conditions, segments);
  ASSERT_TRUE(graph_or.ok());
  EntryGraph graph = std::move(*graph_or);

  ASSERT_TRUE(graph.Optimize().ok());

  auto entries = graph.ToPatchMapEntries(proto::GLYPH_KEYED);
  ASSERT_TRUE(entries.ok()) << entries.status();
  // Should now have only 1 entry with all codepoints
  EXPECT_EQ(entries->size(), 1);
  EXPECT_THAT(entries->at(0).coverage.codepoints,
              UnorderedElementsAre('a', 'b', 'c'));
  EXPECT_TRUE(entries->at(0).coverage.child_indices.empty());
}

TEST(EntryGraphTest, Optimize_Conjunctive) {
  flat_hash_map<segment_index_t, SubsetDefinition> segments = {
      {0, {'a'}},
      {1, {'b'}},
      {2, {'x'}},
  };
  // (s0 OR s1) AND (s2)
  std::vector<ActivationCondition> activation_conditions = {
      ActivationCondition::composite_condition({{0, 1}, {2}}, 10),
  };
  auto graph = EntryGraph::Create(activation_conditions, segments);
  ASSERT_TRUE(graph.ok()) << graph.status();

  ASSERT_TRUE(graph->Optimize().ok());

  auto entries = graph->ToPatchMapEntries(proto::GLYPH_KEYED);
  ASSERT_TRUE(entries.ok()) << entries.status();

  EXPECT_EQ(entries->size(), 2);
  const auto& root_entry = entries->back();
  EXPECT_THAT(root_entry.coverage.codepoints, UnorderedElementsAre('a', 'b'));
  EXPECT_EQ(root_entry.coverage.child_indices.size(), 1);
}

}  // namespace ift::encoder
