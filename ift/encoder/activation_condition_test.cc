#include "ift/encoder/activation_condition.h"

#include "gtest/gtest.h"
#include "ift/common/int_set.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/mock_probability_calculator.h"
#include "ift/freq/probability_bound.h"
#include "ift/proto/patch_encoding.h"
#include "ift/proto/patch_map.h"

using ift::common::CodepointSet;
using ift::common::IntSet;
using ift::common::SegmentSet;
using ift::freq::MockProbabilityCalculator;
using ift::freq::ProbabilityBound;
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

  // Entry generation optimizes the entries, by combining segments
  // where possible.
  std::vector<PatchMap::Entry> expected;

  // Entry 0: s1 OR s3 -> 5
  {
    PatchMap::Entry entry({'a', 'b', 'd', 'e', 'f'}, 5,
                          PatchEncoding::GLYPH_KEYED);
    expected.push_back(entry);
  }

  // Entry 1: (s1 OR s3) AND (s2 OR s4) -> 6
  {
    PatchMap::Entry entry;
    entry.coverage.codepoints = {'c', 'g'};
    entry.coverage.child_indices = {0};
    entry.coverage.conjunctive = true;
    entry.patch_indices = {6};
    expected.push_back(entry);
  }

  // Entry 2: s3 -> 4
  {
    PatchMap::Entry entry({'d', 'e', 'f'}, 4, PatchEncoding::GLYPH_KEYED);
    expected.push_back(entry);
  }

  // Entry 3: s2 -> 2
  {
    PatchMap::Entry entry({'c'}, 2, PatchEncoding::GLYPH_KEYED);
    expected.push_back(entry);
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

  // Entry 0: s1 OR s3 -> 5
  {
    PatchMap::Entry entry({'a', 'b', 'd', 'e', 'f'}, 5,
                          PatchEncoding::GLYPH_KEYED);
    expected.push_back(entry);
  }

  // Entry 1: (s1 OR s3) AND (s2 OR s4) -> 6
  {
    PatchMap::Entry entry;
    entry.coverage.codepoints = {'c', 'g'};
    entry.coverage.child_indices = {0};
    entry.coverage.conjunctive = true;
    entry.patch_indices = {6};
    expected.push_back(entry);
  }

  // Entry 2: s3 -> 4
  {
    PatchMap::Entry entry({'d', 'e', 'f'}, {4, 13, 12},
                          proto::PatchEncoding::TABLE_KEYED_FULL);
    expected.push_back(entry);
  }

  // Entry 3: s2 -> 2
  {
    PatchMap::Entry entry({'c'}, 2, PatchEncoding::GLYPH_KEYED);
    expected.push_back(entry);
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

  // Entry 0: dlig (ignored)
  {
    PatchMap::Entry condition;
    condition.coverage.features = {HB_TAG('d', 'l', 'i', 'g')};
    condition.ignored = true;
    condition.patch_indices = {1};
    condition.encoding = PatchEncoding::GLYPH_KEYED;
    expected.push_back(condition);
  }

  // Entry 1: {d, e, f} (ignored)
  {
    PatchMap::Entry condition;
    condition.coverage.codepoints = {'d', 'e', 'f'};
    condition.ignored = true;
    condition.patch_indices = {2};
    condition.encoding = PatchEncoding::GLYPH_KEYED;
    expected.push_back(condition);
  }

  // Entry 2: dlig OR {d, e, f} (ignored)
  {
    PatchMap::Entry condition;
    condition.coverage.child_indices = {0, 1};
    condition.ignored = true;
    condition.patch_indices = {3};
    condition.encoding = PatchEncoding::GLYPH_KEYED;
    expected.push_back(condition);
  }

  // Entry 3: smcp AND Entry 2 -> 5
  {
    PatchMap::Entry condition;
    condition.coverage.features = {HB_TAG('s', 'm', 'c', 'p')};
    condition.coverage.child_indices = {2};
    condition.coverage.conjunctive = true;
    condition.patch_indices = {5};
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
      Segment({'a'}, ProbabilityBound{0.75, 0.75}),
      Segment({'b'}, ProbabilityBound{0.5, 0.5}),
      Segment({'c'}, ProbabilityBound{0.25, 0.25}),
  };
  std::vector<Segment> merged_segments = {
      Segment({'a'}, ProbabilityBound{0.75, 0.75}),
      Segment({'b'}, ProbabilityBound{0.5, 0.5}),
      Segment({'c'}, ProbabilityBound{0.25, 0.25}),

      // 0 OR 1
      Segment({'a', 'b'}, ProbabilityBound{0.77, 0.77}),

      // 1 OR 2
      Segment({'b', 'c'}, ProbabilityBound{0.66, 0.66}),
  };
  MockProbabilityCalculator probability_calculator(merged_segments);

  ASSERT_EQ(*ActivationCondition::exclusive_segment(0, 1).Probability(
                segments, probability_calculator),
            0.75);
  ASSERT_EQ(*ActivationCondition::exclusive_segment(2, 1).Probability(
                segments, probability_calculator),
            0.25);

  ASSERT_EQ(*ActivationCondition::and_segments({0, 1}, 1).Probability(
                segments, probability_calculator),
            0.75 * 0.5);
  ASSERT_EQ(*ActivationCondition::and_segments({0, 2}, 1).Probability(
                segments, probability_calculator),
            0.75 * 0.25);
  ASSERT_EQ(*ActivationCondition::and_segments({0, 1, 2}, 1)
                 .Probability(segments, probability_calculator),
            0.75 * 0.5 * 0.25);

  ASSERT_EQ(*ActivationCondition::or_segments({1, 2}, 1).Probability(
                segments, probability_calculator),
            0.66);

  ASSERT_EQ(*ActivationCondition::or_segments({0, 1}, 1).Probability(
                segments, probability_calculator),
            0.77);

  ASSERT_EQ(*ActivationCondition::composite_condition({{1, 2}, {0, 1}}, 1)
                 .Probability(segments, probability_calculator),
            // {1, 2} 0.66 * {0, 1} 0.77
            0.66 * 0.77);
}

TEST(ActivationConditionTest, Probability_RejectsEmptySegmentSet) {
  ActivationCondition invalid_condition =
      ActivationCondition::composite_condition({{}, {1}}, 10);
  MockProbabilityCalculator probability_calculator({});
  std::vector<Segment> segments = {
      Segment({'a'}, ProbabilityBound{0.75, 0.75}),
      Segment({'b'}, ProbabilityBound{0.5, 0.5}),
  };

  EXPECT_FALSE(
      invalid_condition.Probability(segments, probability_calculator).ok());

  Segment merged_segment({'a'}, ProbabilityBound{0.5, 0.5});
  EXPECT_FALSE(invalid_condition
                   .MergedProbability(segments, {}, merged_segment,
                                      probability_calculator)
                   .ok());
}

TEST(ActivationConditionTest, Probability_EmptyCondition) {
  ActivationCondition empty_condition = ActivationCondition::True(10);
  MockProbabilityCalculator probability_calculator({});
  std::vector<Segment> segments;

  EXPECT_EQ(*empty_condition.Probability(segments, probability_calculator),
            1.0);

  Segment merged_segment({'a'}, ProbabilityBound{0.5, 0.5});
  EXPECT_EQ(*empty_condition.MergedProbability(segments, {}, merged_segment,
                                               probability_calculator),
            1.0);
}

TEST(ActivationConditionTest, MergedProbability) {
  std::vector<Segment> segments = {
      Segment({'a'}, ProbabilityBound{0.75, 0.75}),
      Segment({'b'}, ProbabilityBound{0.50, 0.50}),
      Segment({'c'}, ProbabilityBound{0.25, 0.25}),
  };
  std::vector<Segment> merged_segments = {
      Segment({'a'}, ProbabilityBound{0.75, 0.75}),
      Segment({'b'}, ProbabilityBound{0.50, 0.50}),
      Segment({'c'}, ProbabilityBound{0.25, 0.25}),

      // 0 + 1
      Segment({'a', 'b'}, ProbabilityBound{0.90, 0.90}),

      // 0 + 2
      Segment({'a', 'c'}, ProbabilityBound{0.80, 0.80}),

      // 0 + 1 + 2
      Segment({'a', 'b', 'c'}, ProbabilityBound{0.95, 0.95}),

  };
  MockProbabilityCalculator probability_calculator(merged_segments);

  Segment merged_segment({'a', 'b'}, ProbabilityBound{0.85, 0.85});

  // Ignores segments that are not present in the condition.
  EXPECT_NEAR(*ActivationCondition::exclusive_segment(0, 1).MergedProbability(
                  segments, 5, merged_segment, probability_calculator),
              0.75, 1e-9);
  EXPECT_NEAR(*ActivationCondition::or_segments({0, 2}, 1).MergedProbability(
                  segments, 5, merged_segment, probability_calculator),
              0.80, 1e-9);
  EXPECT_NEAR(*ActivationCondition::and_segments({0, 2}, 1).MergedProbability(
                  segments, 5, merged_segment, probability_calculator),
              0.75 * 0.25, 1e-9);

  // Conjunctive with merge intersection
  // {0} get's replaced with {0 U 1}

  EXPECT_NEAR(*ActivationCondition::and_segments({0, 2}, 1)
            .ReplaceSegments(0, {0, 1})
            .MergedProbability(
                  segments, 0, merged_segment, probability_calculator),
              0.85 * 0.25, 1e-9);

  // Disjunctive with merge intersection
  // {0} get's replaced with {0 U 1}
  EXPECT_NEAR(*ActivationCondition::or_segments({0}, 1)
              .ReplaceSegments(0, {0, 1})
              .MergedProbability(
                  segments, 0, merged_segment, probability_calculator),
              0.85, 1e-9);
  EXPECT_NEAR(*ActivationCondition::or_segments({1}, 1)
    .ReplaceSegments(0, {0, 1})
    .MergedProbability(
                  segments, 0, merged_segment, probability_calculator),
              0.85, 1e-9);
  EXPECT_NEAR(*ActivationCondition::or_segments({0, 1}, 1)
    .ReplaceSegments(0, {0, 1})
    .MergedProbability(
                  segments, 0, merged_segment, probability_calculator),
              0.85, 1e-9);

  // Disjunctive with partial merge intersection
  EXPECT_NEAR(*ActivationCondition::or_segments({0, 2}, 1)
    .ReplaceSegments(0, {0, 1})
    .MergedProbability(
                  segments, 0, merged_segment, probability_calculator),
              0.95, 1e-9);

  // Conjunctive with partial merge intersection
  EXPECT_NEAR(*ActivationCondition::and_segments({0, 1, 2}, 1)
                    .ReplaceSegments(2, {1, 2})
                   .MergedProbability(segments, 2, merged_segment,
                                      probability_calculator),
              0.75 * 0.85, 1e-9); // .. AND 1 AND 2 becomes .. AND 1

  // Composite condition
  // (0 or 1) AND 2 =merge {1, 2}=> (0 or 1)
  EXPECT_NEAR(*ActivationCondition::composite_condition({{0, 1}, {2}}, 1)
  .ReplaceSegments(1, {1, 2})
                   .MergedProbability(segments, 1, merged_segment,
                                      probability_calculator),
              0.85, 1e-9);
}

TEST(ActivationConditionTest, True) {
  ActivationCondition true_condition = ActivationCondition::True(10);
  ActivationCondition condition = ActivationCondition::and_segments({1}, 10);
  MockProbabilityCalculator probability_calculator({});

  ASSERT_EQ(true_condition.ToString(), "if (true) then p10");
  ASSERT_TRUE(true_condition.IsAlwaysTrue());
  ASSERT_FALSE(condition.IsAlwaysTrue());
  ASSERT_FALSE(true_condition.IsPurelyConjunctive());
  ASSERT_FALSE(true_condition.IsPurelyDisjunctive());
  ASSERT_EQ(*true_condition.Probability({}, probability_calculator), 1.0);
}

TEST(ActivationConditionTest, AndOr_True) {
  auto condition = ActivationCondition::or_segments({1, 2}, 10);
  auto true_con = ActivationCondition::True(11);

  auto a = ActivationCondition::And(condition, true_con);
  auto b = ActivationCondition::And(true_con, condition);

  auto c = ActivationCondition::Or(condition, true_con);
  auto d = ActivationCondition::Or(true_con, condition);

  ASSERT_EQ(a, condition);
  ASSERT_EQ(b, ActivationCondition::or_segments({1, 2}, 11));

  ASSERT_EQ(c, ActivationCondition::True(10));
  ASSERT_EQ(d, true_con);
}

TEST(ActivationConditionTest, And) {
  auto a = ActivationCondition::or_segments({1, 2}, 10);
  auto b = ActivationCondition::or_segments({3}, 11);

  auto combined_ab = ActivationCondition::And(a, b);
  EXPECT_EQ(combined_ab.ToString(), "if ((s1 OR s2) AND s3) then p10");
  EXPECT_EQ(combined_ab.activated(), 10);
  EXPECT_FALSE(combined_ab.IsExclusive());
  EXPECT_FALSE(combined_ab.IsFallback());

  auto combined_ba = ActivationCondition::And(b, a);
  EXPECT_EQ(combined_ab.conditions(), combined_ba.conditions());
  EXPECT_EQ(combined_ba.activated(), 11);
}

TEST(ActivationConditionTest, And_Exclusiveness) {
  auto tru = ActivationCondition::True(0);
  auto exc_a = ActivationCondition::exclusive_segment(10, 0);
  auto exc_b = ActivationCondition::exclusive_segment(20, 0);
  auto non_exc_b = ActivationCondition::and_segments({20}, 0);

  // For and And combination to be exclusive the result must
  // be unitary and both inputs are exclusive (or one of the inputs
  // is always true).

  ASSERT_TRUE(ActivationCondition::And(tru, exc_a).IsExclusive());
  ASSERT_TRUE(ActivationCondition::And(exc_a, tru).IsExclusive());

  ASSERT_FALSE(ActivationCondition::And(exc_a, exc_b).IsExclusive());
  ASSERT_FALSE(ActivationCondition::And(exc_b, exc_a).IsExclusive());
  ASSERT_TRUE(ActivationCondition::And(exc_a, exc_a).IsExclusive());

  ASSERT_FALSE(ActivationCondition::And(exc_b, non_exc_b).IsExclusive());
}

TEST(ActivationConditionTest, And_Simplification) {
  auto a = ActivationCondition::or_segments({1, 2}, 10);
  auto b = ActivationCondition::or_segments({1}, 10);

  auto combined_ab = ActivationCondition::And(a, b);
  EXPECT_EQ(combined_ab.ToString(), "if (s1) then p10");

  // common elements but not subsets, no simplification
  auto c = ActivationCondition::or_segments({1, 2}, 10);
  auto d = ActivationCondition::or_segments({1, 3}, 10);
  auto combined_cd = ActivationCondition::And(c, d);
  EXPECT_EQ(combined_cd.ToString(), "if ((s1 OR s2) AND (s1 OR s3)) then p10");
}

TEST(ActivationConditionTest, Or) {
  auto a = ActivationCondition::and_segments({1, 2}, 10);
  auto b = ActivationCondition::and_segments({3, 4}, 11);

  auto combined_ab = ActivationCondition::Or(a, b);
  EXPECT_EQ(combined_ab.ToString(),
            "if ((s1 OR s3) AND (s1 OR s4) AND (s2 OR s3) AND (s2 OR s4)) "
            "then p10");
  EXPECT_EQ(combined_ab.activated(), 10);
  EXPECT_FALSE(combined_ab.IsExclusive());
  EXPECT_FALSE(combined_ab.IsFallback());

  auto combined_ba = ActivationCondition::Or(b, a);
  EXPECT_EQ(combined_ba.activated(), 11);
  EXPECT_EQ(combined_ab.conditions(), combined_ba.conditions());
}

TEST(ActivationConditionTest, Or_Simplification) {
  auto a = ActivationCondition::and_segments({1, 2}, 10);
  auto b = ActivationCondition::and_segments({2, 3}, 10);

  auto combined_ab = ActivationCondition::Or(a, b);
  EXPECT_EQ(combined_ab.ToString(), "if ((s1 OR s3) AND s2) then p10");
  EXPECT_EQ(combined_ab.activated(), 10);

  auto combined_ba = ActivationCondition::Or(b, a);
  EXPECT_EQ(combined_ab.conditions(), combined_ba.conditions());
}

TEST(ActivationConditionTest, ReplaceSegments) {
  // (s1 OR s2) AND (s3 OR s4) AND s5
  auto a = ActivationCondition::composite_condition({{1, 2}, {3, 4}, {5}}, 10);

  auto replaced = a.ReplaceSegments(100, {1, 2});
  EXPECT_EQ(replaced.ToString(), "if ((s3 OR s4) AND s5 AND s100) then p10");

  replaced = a.ReplaceSegments(200, {3, 5});
  // (s4 OR s200) AND s200 simplifies to just s200
  EXPECT_EQ(replaced.ToString(), "if ((s1 OR s2) AND s200) then p10");

  replaced = a.ReplaceSegments(300, {1, 3});
  EXPECT_EQ(replaced.ToString(),
            "if ((s2 OR s300) AND (s4 OR s300) AND s5) then p10");
}

TEST(ActivationConditionTest, ReplaceSegments_True) {
  auto a = ActivationCondition::True(10);
  // Replace segments is a noop on a True condition.
  ASSERT_EQ(a.ReplaceSegments(1, {2, 3}), a);
}

TEST(ActivationConditionTest, Intersects) {
  // (s1 OR s2) AND (s3 OR s4) AND s5
  auto a = ActivationCondition::composite_condition({{1, 2}, {3, 4}, {5}}, 10);

  EXPECT_TRUE(a.Intersects({1}));
  EXPECT_TRUE(a.Intersects({2}));
  EXPECT_TRUE(a.Intersects({5}));
  EXPECT_TRUE(a.Intersects({1, 6}));

  EXPECT_FALSE(a.Intersects({6}));
  EXPECT_FALSE(a.Intersects({0, 6, 7}));
  EXPECT_FALSE(a.Intersects({}));

  auto b = ActivationCondition::True(10);
  EXPECT_FALSE(b.Intersects({1}));
}

}  // namespace ift::encoder
