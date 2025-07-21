#include "ift/encoder/activation_condition.h"

#include "common/int_set.h"
#include "gtest/gtest.h"
#include "ift/encoder/subset_definition.h"
#include "ift/proto/patch_encoding.h"
#include "ift/proto/patch_map.h"

using common::CodepointSet;
using common::IntSet;
using common::SegmentSet;
using ift::proto::PatchEncoding;
using ift::proto::PatchMap;

namespace ift::encoder {

TEST(ActivationConditionTest, ActivationConditionsToEncoderConditions) {
  absl::flat_hash_map<segment_index_t, SubsetDefinition> segments = {
      {1, {'a', 'b'}},
      {2, {'c'}},
      {3, {'d', 'e', 'f'}},
      {4, {'g'}},
  };

  std::vector<ActivationCondition> activation_conditions = {
      ActivationCondition::exclusive_segment(2, 2),
      ActivationCondition::exclusive_segment(3, 4),
      ActivationCondition::or_segments({1, 3}, 5),
      ActivationCondition::composite_condition({{1, 3}, {2, 4}}, 6),
  };

  std::vector<PatchMap::Entry> expected;

  // entry[0] {{2}} -> 2,
  expected.push_back(
      PatchMap::Entry({'c'}, 2, proto::PatchEncoding::GLYPH_KEYED));

  // entry[1] {{3}} -> 4
  expected.push_back(
      PatchMap::Entry({'d', 'e', 'f'}, 4, proto::PatchEncoding::GLYPH_KEYED));

  // entry[2] {{1}} ignored
  {
    PatchMap::Entry condition({'a', 'b'}, 5, PatchEncoding::GLYPH_KEYED);
    condition.ignored = true;
    expected.push_back(condition);
  }

  // entry[3] {{4}} ignored
  {
    PatchMap::Entry condition({'g'}, 6, PatchEncoding::GLYPH_KEYED);
    condition.ignored = true;
    expected.push_back(condition);
  }

  // entry[4] {{1 OR 3}} -> 5
  {
    PatchMap::Entry condition;
    condition.coverage.child_indices = {2, 1};
    condition.patch_indices.push_back(5);
    expected.push_back(condition);
  }

  // entry[5] {{2 OR 4}} ignored
  {
    PatchMap::Entry condition;
    condition.coverage.child_indices = {0, 3};  // entry[0], entry[3]
    condition.patch_indices.push_back(6);
    condition.ignored = true;
    expected.push_back(condition);
  }

  // entry[6] {{1 OR 3} AND {2 OR 4}} -> 6
  {
    PatchMap::Entry condition;
    condition.coverage.child_indices = {4, 5};  // entry[4], entry[5]
    condition.patch_indices.push_back(6);
    condition.coverage.conjunctive = true;
    expected.push_back(condition);
  }

  auto entries = ActivationCondition::ActivationConditionsToPatchMapEntries(
      activation_conditions, segments);
  ASSERT_TRUE(entries.ok()) << entries.status();
  ASSERT_EQ(*entries, expected);
}

TEST(ActivationConditionTest,
     ActivationConditionsToEncoderConditions_WithPrefetches) {
  absl::flat_hash_map<segment_index_t, SubsetDefinition> segments = {
      {1, {'a', 'b'}},
      {2, {'c'}},
      {3, {'d', 'e', 'f'}},
      {4, {'g'}},
  };

  auto a1 = ActivationCondition::exclusive_segment(2, 2);
  auto a2 = ActivationCondition::exclusive_segment(3, 4);
  a2.AddPrefetches({13, 12});
  a2.SetEncoding(PatchEncoding::TABLE_KEYED_FULL);

  auto a3 = ActivationCondition::or_segments({1, 3}, 5);
  auto a4 = ActivationCondition::composite_condition({{1, 3}, {2, 4}}, 6);

  std::vector<ActivationCondition> activation_conditions = {
      a1,
      a2,
      a3,
      a4,
  };

  std::vector<PatchMap::Entry> expected;

  // entry[0] {{2}} -> 2,
  expected.push_back(
      PatchMap::Entry({'c'}, 2, proto::PatchEncoding::GLYPH_KEYED));

  // entry[1] {{3}} -> 4
  {
    PatchMap::Entry condition({'d', 'e', 'f'}, 4,
                              proto::PatchEncoding::TABLE_KEYED_FULL);
    condition.patch_indices.push_back(13);
    condition.patch_indices.push_back(12);
    expected.push_back(condition);
  }

  // entry[2] {{1}} ignored
  {
    PatchMap::Entry condition({'a', 'b'}, 13, PatchEncoding::GLYPH_KEYED);
    condition.ignored = true;
    expected.push_back(condition);
  }

  // entry[3] {{4}} ignored
  {
    PatchMap::Entry condition({'g'}, 14, PatchEncoding::GLYPH_KEYED);
    condition.ignored = true;
    expected.push_back(condition);
  }

  // entry[4] {{1 OR 3}} -> 5
  {
    PatchMap::Entry condition;
    condition.coverage.child_indices = {2, 1};
    condition.patch_indices.push_back(5);
    expected.push_back(condition);
  }

  // entry[5] {{2 OR 4}} ignored
  {
    PatchMap::Entry condition;
    condition.coverage.child_indices = {0, 3};  // entry[0], entry[3]
    condition.patch_indices.push_back(6);
    condition.ignored = true;
    expected.push_back(condition);
  }

  // entry[6] {{1 OR 3} AND {2 OR 4}} -> 6
  {
    PatchMap::Entry condition;
    condition.coverage.child_indices = {4, 5};  // entry[4], entry[5]
    condition.patch_indices.push_back(6);
    condition.coverage.conjunctive = true;
    expected.push_back(condition);
  }

  auto entries = ActivationCondition::ActivationConditionsToPatchMapEntries(
      activation_conditions, segments);
  ASSERT_TRUE(entries.ok()) << entries.status();
  ASSERT_EQ(*entries, expected);
}

TEST(ActivationConditionTest,
     ActivationConditionsToEncoderConditions_WithFeatures) {
  SubsetDefinition smcp;
  smcp.feature_tags = {HB_TAG('s', 'm', 'c', 'p')};

  SubsetDefinition combined{'d', 'e', 'f'};
  combined.feature_tags = {HB_TAG('d', 'l', 'i', 'g')};

  absl::flat_hash_map<segment_index_t, SubsetDefinition> segments = {
      {1, smcp},
      {2, combined},
  };

  std::vector<ActivationCondition> activation_conditions = {
      ActivationCondition::and_segments({1, 2}, 5),
  };

  std::vector<PatchMap::Entry> expected;

  // entry[0] {{smcp}} -> 1,
  {
    PatchMap::Entry condition;
    condition.coverage.features = {HB_TAG('s', 'm', 'c', 'p')};
    condition.ignored = true;
    condition.patch_indices = {1};
    condition.encoding = PatchEncoding::GLYPH_KEYED;
    expected.push_back(condition);
  }

  // entry[1] {{d, e, f}} -> 2,
  {
    PatchMap::Entry condition;
    condition.coverage.codepoints = {'d', 'e', 'f'};
    condition.ignored = true;
    condition.patch_indices = {2};
    condition.encoding = PatchEncoding::GLYPH_KEYED;
    expected.push_back(condition);
  }

  // entry[2] {{dlig}} -> 3,
  {
    PatchMap::Entry condition;
    condition.coverage.features = {HB_TAG('d', 'l', 'i', 'g')};
    condition.ignored = true;
    condition.patch_indices = {3};
    condition.encoding = PatchEncoding::GLYPH_KEYED;
    expected.push_back(condition);
  }

  // entry[3] {e1 OR e2} -> 4,
  {
    PatchMap::Entry condition;
    condition.coverage.child_indices = {1, 2};
    condition.ignored = true;
    condition.patch_indices = {4};
    condition.encoding = PatchEncoding::GLYPH_KEYED;
    expected.push_back(condition);
  }

  // entry[2] s1 AND s2 -> 5,
  {
    PatchMap::Entry condition;
    condition.coverage.child_indices = {0, 3};
    condition.patch_indices = {5};
    condition.coverage.conjunctive = true;
    condition.encoding = PatchEncoding::GLYPH_KEYED;
    expected.push_back(condition);
  }

  auto entries = ActivationCondition::ActivationConditionsToPatchMapEntries(
      activation_conditions, segments);
  ASSERT_TRUE(entries.ok()) << entries.status();
  ASSERT_EQ(*entries, expected);
}

TEST(ActivationConditionTest, ActivationConditionProbabilities) {
  std::vector<Segment> segments = {
      Segment({}, 0.75),
      Segment({}, 0.5),
      Segment({}, 0.25),
  };

  ASSERT_EQ(*ActivationCondition::exclusive_segment(0, 1).Probability(segments),
            0.75);
  ASSERT_EQ(*ActivationCondition::exclusive_segment(2, 1).Probability(segments),
            0.25);

  ASSERT_EQ(*ActivationCondition::and_segments({0, 1}, 1).Probability(segments),
            0.75 * 0.5);
  ASSERT_EQ(*ActivationCondition::and_segments({0, 2}, 1).Probability(segments),
            0.75 * 0.25);
  ASSERT_EQ(
      *ActivationCondition::and_segments({0, 1, 2}, 1).Probability(segments),
      0.75 * 0.5 * 0.25);

  double one_or_two = 0.5 + 0.25 - 0.5 * 0.25;
  ASSERT_EQ(*ActivationCondition::or_segments({1, 2}, 1).Probability(segments),
            one_or_two);

  double zero_or_one = 0.75 + 0.5 - 0.75 * 0.5;
  ASSERT_EQ(*ActivationCondition::or_segments({0, 1}, 1).Probability(segments),
            zero_or_one);

  double zero_or_one_or_two = 0.75 + 0.5 + 0.25 - 0.75 * 0.5 - 0.75 * 0.25 -
                              0.5 * 0.25 + 0.75 * 0.5 * 0.25;
  ASSERT_EQ(
      *ActivationCondition::or_segments({0, 1, 2}, 1).Probability(segments),
      zero_or_one_or_two);

  // Composite conditions aren't currently supported.
  ASSERT_TRUE(absl::IsUnimplemented(
      ActivationCondition::composite_condition({{1, 2}, {0, 1}}, 1)
          .Probability(segments)
          .status()));
}

TEST(ActivationConditionTest, MergedProbability) {
  std::vector<Segment> segments = {
      Segment({}, 0.75),
      Segment({}, 0.5),
      Segment({}, 0.25),
  };

  // Ignores segments that are not present in the condition.
  EXPECT_NEAR(*ActivationCondition::exclusive_segment(0, 1).MergedProbability(
                  segments, {5}, 0.12),
              0.75, 1e-9);

  // Simple one to one replacement
  EXPECT_NEAR(*ActivationCondition::exclusive_segment(0, 1).MergedProbability(
                  segments, {0}, 0.12),
              0.12, 1e-9);

  // Simple two to one replacement
  EXPECT_NEAR(*ActivationCondition::and_segments({0, 1}, 1).MergedProbability(
                  segments, {0, 1}, 0.12),
              0.12, 1e-9);
  EXPECT_NEAR(*ActivationCondition::or_segments({0, 1}, 1).MergedProbability(
                  segments, {0, 1}, 0.12),
              0.12, 1e-9);

  // Conjunctive with partial replacement
  EXPECT_NEAR(*ActivationCondition::and_segments({0, 1, 2}, 1)
                   .MergedProbability(segments, {1, 2}, 0.4),
              // P() = P(0) * P(merged) = 0.75 * 0.4 = 0.3
              0.3, 1e-9);

  // Disjunctive with partial replacement
  EXPECT_NEAR(*ActivationCondition::or_segments({0, 1, 2}, 1)
                   .MergedProbability(segments, {1, 2}, 0.4),
              // P() = P(0) + P(merged) - P(0) * P(merged)
              // = 0.75 + 0.4 - 0.75 * 0.4
              /* = */ 0.85, 1e-9);

  EXPECT_TRUE(absl::IsUnimplemented(
      ActivationCondition::composite_condition({{0, 1}, {2}}, 1)
          .MergedProbability(segments, {1, 2}, 0.4)
          .status()));
}

}  // namespace ift::encoder
