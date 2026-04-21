#include "ift/encoder/entry_graph.h"

#include <vector>

#include "absl/container/flat_hash_map.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/subset_definition.h"
#include "ift/proto/patch_encoding.h"

using ::ift::proto::PatchEncoding;
using ::testing::UnorderedElementsAre;

namespace ift::encoder {

TEST(EntryGraphTest, ToPatchMapEntries) {
  absl::flat_hash_map<segment_index_t, SubsetDefinition> segments = {
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

  absl::flat_hash_map<uint32_t, size_t> codepoint_to_index;
  absl::flat_hash_map<uint32_t, size_t> patch_to_index;

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
  absl::flat_hash_map<segment_index_t, SubsetDefinition> segments = {
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
  absl::flat_hash_map<segment_index_t, SubsetDefinition> segments = {
      {1, {'a'}},
      {2, {'b'}},
      {3, {'c'}},
  };

  std::vector<ActivationCondition> activation_conditions = {
      ActivationCondition::or_segments({1, 2}, 10),
      ActivationCondition::or_segments({2, 3}, 11),
  };

  auto graph_or_status = EntryGraph::Create(activation_conditions, segments);
  ASSERT_TRUE(graph_or_status.ok()) << graph_or_status.status();
  EntryGraph graph = std::move(graph_or_status).value();

  auto r = graph.TopologicalSort();
  ASSERT_TRUE(r.ok()) << r.status();
  std::vector<uint32_t> sorted = *r;

  // The graph should have 5 nodes:
  // 0: (s1 OR s2)
  // 1: (s1)
  // 2: (s2) shared
  // 3: (s2 or s3)
  // 4: (s3)
  EXPECT_THAT(sorted, UnorderedElementsAre(0, 1, 2, 3, 4));

  auto n0 = std::find(sorted.begin(), sorted.end(), 0);
  auto n1 = std::find(sorted.begin(), sorted.end(), 1);
  auto n2 = std::find(sorted.begin(), sorted.end(), 2);
  auto n3 = std::find(sorted.begin(), sorted.end(), 3);
  auto n4 = std::find(sorted.begin(), sorted.end(), 4);

  // n0 comes before it's children
  EXPECT_LT(n0, n1);
  EXPECT_LT(n0, n2);

  // n3 comes before it's children
  EXPECT_LT(n3, n2);
  EXPECT_LT(n3, n4);
}

TEST(EntryGraphTest, SplitLargeNode) {
  absl::flat_hash_map<segment_index_t, SubsetDefinition> segments;
  ift::common::SegmentSet segment_ids;
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
    if (!entry.ignored && !entry.patch_indices.empty() && entry.patch_indices[0] == 10) {
      root_id = i;
    } else if (entry.ignored && entry.coverage.codepoints.empty() && !entry.coverage.child_indices.empty()) {
      split_node_id = i;
    }
  }

  ASSERT_NE(root_id, -1);
  ASSERT_NE(split_node_id, -1);

  // The root node gets the first 126 children + the split node (total 127).
  EXPECT_EQ((*entries)[root_id].coverage.child_indices.size(), 127);
  EXPECT_TRUE((*entries)[root_id].coverage.child_indices.contains(split_node_id));

  // The split node gets the remaining 4 children.
  EXPECT_EQ((*entries)[split_node_id].coverage.child_indices.size(), 4);
}

}  // namespace ift::encoder