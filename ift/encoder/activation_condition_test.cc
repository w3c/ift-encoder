#include "ift/encoder/activation_condition.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "absl/status/status.h"
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
  // where possible. Entry order tries to match input order where possible
  std::vector<PatchMap::Entry> expected;

  // Entry 0: s2 -> 2
  {
    PatchMap::Entry entry({'c'}, 2, PatchEncoding::GLYPH_KEYED);
    expected.push_back(entry);
  }

  // Entry 1: s3 -> 4
  {
    PatchMap::Entry entry({'d', 'e', 'f'}, 4, PatchEncoding::GLYPH_KEYED);
    expected.push_back(entry);
  }

  // Entry 2: s1 OR s3 -> 5
  {
    PatchMap::Entry entry({'a', 'b', 'd', 'e', 'f'}, 5,
                          PatchEncoding::GLYPH_KEYED);
    expected.push_back(entry);
  }

  // Entry 3: (s1 OR s3) AND (s2 OR s4) -> 6
  {
    PatchMap::Entry entry;
    entry.coverage.codepoints = {'c', 'g'};
    entry.coverage.child_indices = {2};
    entry.coverage.conjunctive = true;
    entry.patch_indices = {6};
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

  // Entry 0: s2 -> 2
  {
    PatchMap::Entry entry({'c'}, 2, PatchEncoding::GLYPH_KEYED);
    expected.push_back(entry);
  }

  // Entry 1: s3 -> 4
  {
    PatchMap::Entry entry({'d', 'e', 'f'}, {4, 13, 12},
                          proto::PatchEncoding::TABLE_KEYED_FULL);
    expected.push_back(entry);
  }

  // Entry 2: s1 OR s3 -> 5
  {
    PatchMap::Entry entry({'a', 'b', 'd', 'e', 'f'}, 5,
                          PatchEncoding::GLYPH_KEYED);
    expected.push_back(entry);
  }

  // Entry 1: (s1 OR s3) AND (s2 OR s4) -> 6
  {
    PatchMap::Entry entry;
    entry.coverage.codepoints = {'c', 'g'};
    entry.coverage.child_indices = {2};
    entry.coverage.conjunctive = true;
    entry.patch_indices = {6};
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



  // Entry 0: {d, e, f} (ignored)
  {
    PatchMap::Entry condition;
    condition.coverage.codepoints = {'d', 'e', 'f'};
    condition.ignored = true;
    condition.patch_indices = {1};
    condition.encoding = PatchEncoding::GLYPH_KEYED;
    expected.push_back(condition);
  }

  // Entry 1: dlig (ignored)
  {
    PatchMap::Entry condition;
    condition.coverage.features = {HB_TAG('d', 'l', 'i', 'g')};
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
      {{'a'}},
      {{'b'}},
      {{'c'}},
  };
  std::vector<std::pair<Segment, double>> segment_probabilities = {
      {{{'a'}}, 0.75},
      {{{'b'}}, 0.5},
      {{{'c'}}, 0.25},

      // 0 OR 1
      {{{'a', 'b'}}, 0.77},

      // 1 OR 2
      {{{'b', 'c'}}, 0.66},
  };
  MockProbabilityCalculator probability_calculator(segment_probabilities);

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
      {{'a'}},
      {{'b'}},
  };

  EXPECT_FALSE(
      invalid_condition.Probability(segments, probability_calculator).ok());

  SegmentSet merged_segments{0};
  EXPECT_FALSE(invalid_condition
                   .MergedProbability(segments, {}, merged_segments,
                                      probability_calculator)
                   .ok());
}

TEST(ActivationConditionTest, Probability_EmptyCondition) {
  ActivationCondition empty_condition = ActivationCondition::True(10);
  MockProbabilityCalculator probability_calculator({});
  std::vector<Segment> segments;

  EXPECT_EQ(*empty_condition.Probability(segments, probability_calculator),
            1.0);

  SegmentSet merged_segments{0};
  EXPECT_EQ(*empty_condition.MergedProbability(segments, {}, merged_segments,
                                               probability_calculator),
            1.0);
}

TEST(ActivationConditionTest, MergedProbability) {
  std::vector<Segment> segments = {
      /* 0 */ {{'a'}},
      /* 1 */ {{'b'}},
      /* 2 */ {{'c'}},
  };

  double Pa = 0.75;
  double Pb = 0.50;
  double Pc = 0.25;
  double Pab = 0.90;
  double Pabc = 0.95;
  double Pac = 0.80;
  std::vector<std::pair<Segment, double>> segment_probabilities = {
      {{{'a'}}, Pa},
      {{{'b'}}, Pb},
      {{{'c'}}, Pc},

      // 0 + 1
      {{{'a', 'b'}}, Pab},

      // 0 + 2
      {{{'a', 'c'}}, Pac},

      // 0 + 1 + 2
      {{{'a', 'b', 'c'}}, Pabc},

  };
  MockProbabilityCalculator probability_calculator(segment_probabilities);

  SegmentSet merged_segments{0, 1};

  // Ignores segments that are not present in the condition.
  EXPECT_NEAR(*ActivationCondition::exclusive_segment(0, 1).MergedProbability(
                  segments, 5, merged_segments, probability_calculator),
              Pa, 1e-9);
  EXPECT_NEAR(*ActivationCondition::or_segments({0, 2}, 1).MergedProbability(
                  segments, 5, merged_segments, probability_calculator),
              Pac, 1e-9);
  EXPECT_NEAR(*ActivationCondition::and_segments({0, 2}, 1).MergedProbability(
                  segments, 5, merged_segments, probability_calculator),
              Pa * Pc, 1e-9);

  // Conjunctive with merge intersection
  // {0} get's replaced with {0 U 1}
  EXPECT_NEAR(*ActivationCondition::and_segments({0, 2}, 1)
              // compute P((a OR b) AND c)
              .ReplaceSegments(0, {0, 1})
              .MergedProbability(segments, 0, merged_segments,
                                 probability_calculator),
              Pab * Pc, 1e-9);

  // Disjunctive with merge intersection
  // {0} get's replaced with {0 U 1}
  EXPECT_NEAR(*ActivationCondition::or_segments({0}, 1)
                   .ReplaceSegments(0, {0, 1})
                   .MergedProbability(segments, 0, merged_segments,
                                      probability_calculator),
              Pab, 1e-9);
  EXPECT_NEAR(*ActivationCondition::or_segments({1}, 1)
                   .ReplaceSegments(0, {0, 1})
                   .MergedProbability(segments, 0, merged_segments,
                                      probability_calculator),
              Pab, 1e-9);
  EXPECT_NEAR(*ActivationCondition::or_segments({0, 1}, 1)
                   .ReplaceSegments(0, {0, 1})
                   .MergedProbability(segments, 0, merged_segments,
                                      probability_calculator),
              Pab, 1e-9);

  // Disjunctive with partial merge intersection
  EXPECT_NEAR(*ActivationCondition::or_segments({0, 2}, 1)
                   .ReplaceSegments(0, {0, 1})
                   .MergedProbability(segments, 0, merged_segments,
                                      probability_calculator),
              0.95, 1e-9);

  // Conjunctive with partial merge intersection
  EXPECT_NEAR(*ActivationCondition::and_segments({0, 1, 2}, 1)
                   .ReplaceSegments(2, {1, 2}) // -> a AND c
                   // compute a AND (a OR b)
                   .MergedProbability(segments, 2, merged_segments,
                                      probability_calculator),
              Pa * Pab, 1e-9);

  // Composite condition
  // (0 or 1) AND 2 =merge {1, 2}=> (0 or 1)
  EXPECT_NEAR(*ActivationCondition::composite_condition({{0, 1}, {2}}, 1)
                   .ReplaceSegments(1, {1, 2})
                   .MergedProbability(segments, 1, merged_segments,
                                      probability_calculator),
              Pab, 1e-9);
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

  ASSERT_EQ(c, ActivationCondition::True(11));
  ASSERT_EQ(d, ActivationCondition::True(11));
}

TEST(ActivationConditionTest, And) {
  auto a = ActivationCondition::or_segments({1, 2}, 10);
  auto b = ActivationCondition::or_segments({3}, 11);

  auto combined_ab = ActivationCondition::And(a, b);
  EXPECT_EQ(combined_ab.ToString(), "if ((s1 OR s2) AND s3) then p10");
  EXPECT_EQ(combined_ab.activated(), 10);
  EXPECT_FALSE(combined_ab.IsExclusive());

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
            "then p11");
  EXPECT_EQ(combined_ab.activated(), 11);
  EXPECT_FALSE(combined_ab.IsExclusive());

  auto combined_ba = ActivationCondition::Or(b, a);
  EXPECT_EQ(combined_ab, combined_ba);
}

TEST(ActivationConditionTest, Or_IsOrderIndependent) {
  auto a = ActivationCondition::or_segments({1, 2}, 10);
  a.SetEncoding(ift::proto::DEFAULT_ENCODING);
  auto b = ActivationCondition::or_segments({3}, 11);
  b.SetEncoding(ift::proto::DEFAULT_ENCODING);
  auto c = ActivationCondition::and_segments({4, 5}, 9);
  c.SetEncoding(ift::proto::TABLE_KEYED_PARTIAL);

  auto ab = ActivationCondition::Or(a, b);
  EXPECT_EQ(ab, ActivationCondition::Or(b, a));
  EXPECT_EQ(ab.activated(), 11);

  auto ac = ActivationCondition::Or(a, c);
  EXPECT_EQ(ac, ActivationCondition::Or(c, a));
  EXPECT_EQ(ac.Encoding(), ift::proto::TABLE_KEYED_PARTIAL);

  // Folding over all three must give the same result no matter which order the
  // conditions are visited in.
  ActivationCondition expected =
      ActivationCondition::Or(ActivationCondition::Or(a, b), c);
  EXPECT_EQ(ActivationCondition::Or(ActivationCondition::Or(a, c), b),
            expected);
  EXPECT_EQ(ActivationCondition::Or(ActivationCondition::Or(b, a), c),
            expected);
  EXPECT_EQ(ActivationCondition::Or(ActivationCondition::Or(b, c), a),
            expected);
  EXPECT_EQ(ActivationCondition::Or(ActivationCondition::Or(c, a), b),
            expected);
  EXPECT_EQ(ActivationCondition::Or(ActivationCondition::Or(c, b), a),
            expected);
  EXPECT_EQ(ActivationCondition::Or(a, ActivationCondition::Or(b, c)),
            expected);
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

TEST(ActivationConditionTest, Or_CommonClauses) {
  auto a = ActivationCondition::composite_condition({{1, 2}, {3}, {4}}, 10);
  auto b = ActivationCondition::composite_condition({{1, 2}, {3}, {5}}, 10);

  auto combined = ActivationCondition::Or(a, b);
  EXPECT_EQ(combined.ToString(),
            "if ((s1 OR s2) AND s3 AND (s4 OR s5)) then p10");
}

TEST(ActivationConditionTest, Or_Subsumption) {
  auto a = ActivationCondition::and_segments({1, 2}, 10);
  auto b = ActivationCondition::and_segments({1}, 10);

  auto combined = ActivationCondition::Or(a, b);
  EXPECT_EQ(combined.ToString(), "if (s1) then p10");
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

  auto b = ActivationCondition::and_segments({{1, 2, 3, 5}}, 10);
  EXPECT_EQ(b.ReplaceSegments(2, {2, 5}).ToString(),
            "if (s1 AND s2 AND s3) then p10");
  EXPECT_EQ(b.ReplaceSegments(4, {2, 5}).ToString(),
            "if (s1 AND s3 AND s4) then p10");
  EXPECT_EQ(b.ReplaceSegments(3, {2, 5}).ToString(), "if (s1 AND s3) then p10");


  EXPECT_EQ(ActivationCondition::and_segments({0, 1, 2}, 0)
    .ReplaceSegments(2, {1, 2}).ToString(), "if (s0 AND s2) then p0");
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

TEST(ActivationConditionTest, EffectiveGroupSize) {
  EXPECT_EQ(ActivationCondition::exclusive_segment(5, 0).EffectiveGroupSize(),
            1);
  EXPECT_EQ(
      ActivationCondition::and_segments({1, 2, 3}, 0).EffectiveGroupSize(), 1);
  EXPECT_EQ(ActivationCondition::or_segments({1, 2, 3}, 0).EffectiveGroupSize(),
            3);
  EXPECT_EQ(ActivationCondition::composite_condition(
                {{1, 2, 3}, {4, 5, 6, 7}, {7, 8}}, 0)
                .EffectiveGroupSize(),
            2);
}

TEST(ActivationConditionTest, NonCompositeSuperset) {
  EXPECT_EQ(ActivationCondition::True(12).NonCompositeSuperset(),
            ActivationCondition::True(12));

  EXPECT_EQ(
      ActivationCondition::exclusive_segment(12, 14).NonCompositeSuperset(),
      ActivationCondition::exclusive_segment(12, 14));

  EXPECT_EQ(
      ActivationCondition::or_segments({2, 8, 21}, 12).NonCompositeSuperset(),
      ActivationCondition::or_segments({2, 8, 21}, 12));

  EXPECT_EQ(
      ActivationCondition::and_segments({2, 8, 21}, 12).NonCompositeSuperset(),
      ActivationCondition::and_segments({2, 8, 21}, 12));

  // Picks the single segment subgroups
  EXPECT_EQ(ActivationCondition::composite_condition({{0, 1}, {1, 7}, {3}}, 12)
                .NonCompositeSuperset(),
            ActivationCondition::exclusive_segment(3, 12));
  EXPECT_EQ(
      ActivationCondition::composite_condition({{0, 1}, {1, 7}, {3}, {8}}, 12)
          .NonCompositeSuperset(),
      ActivationCondition::and_segments({3, 8}, 12));

  // Picks one subgroup (largest min segment and smallest size)
  EXPECT_EQ(ActivationCondition::composite_condition(
                {{0, 1}, {3, 100}, {8, 9}, {2, 1}}, 12)
                .NonCompositeSuperset(),
            ActivationCondition::or_segments({8, 9}, 12));

  // if min segment is tied then the smaller group is prefered
  EXPECT_EQ(ActivationCondition::composite_condition(
                {{0, 1}, {3, 4, 5}, {3, 100}}, 12)
                .NonCompositeSuperset(),
            ActivationCondition::or_segments({3, 100}, 12));
  EXPECT_EQ(ActivationCondition::composite_condition(
                {{3, 100}, {0, 1}, {3, 4, 5}}, 12)
                .NonCompositeSuperset(),
            ActivationCondition::or_segments({3, 100}, 12));
}

TEST(ActivationConditionTest, HashCaching) {
  auto cond = ActivationCondition::or_segments({1, 3}, 5);
  size_t h1 = cond.Hash();
  EXPECT_EQ(cond.Hash(), h1);

  // Mutation resets hash
  cond.SetEncoding(PatchEncoding::TABLE_KEYED_FULL);
  size_t h2 = cond.Hash();
  EXPECT_NE(h1, h2);

  // Copy preserves hash
  auto cond_copy = cond;
  EXPECT_EQ(cond_copy.Hash(), h2);
  EXPECT_EQ(cond, cond_copy);
}

TEST(ActivationConditionTest, IsUnchangedByMerge) {
  // Empty or singleton merge sets never change any condition.
  EXPECT_TRUE(ActivationCondition::or_segments({1, 2}, 0).IsUnchangedByMerge({}));
  EXPECT_TRUE(ActivationCondition::or_segments({1, 2}, 0).IsUnchangedByMerge({1}));
  EXPECT_TRUE(ActivationCondition::True(0).IsUnchangedByMerge({1, 2}));

  // Non-intersecting conditions are unchanged.
  EXPECT_TRUE(ActivationCondition::or_segments({3, 4}, 0).IsUnchangedByMerge({1, 2}));
  EXPECT_TRUE(ActivationCondition::and_segments({3, 4}, 0).IsUnchangedByMerge({1, 2}));

  // Disjunctive condition that is a superset of the merge group is unchanged.
  EXPECT_TRUE(ActivationCondition::or_segments({1, 2, 3}, 0).IsUnchangedByMerge({1, 2}));
  EXPECT_TRUE(ActivationCondition::or_segments({1, 2}, 0).IsUnchangedByMerge({1, 2}));

  // Disjunctive condition that partially intersects the merge group is changed.
  EXPECT_FALSE(ActivationCondition::or_segments({1, 3}, 0).IsUnchangedByMerge({1, 2}));

  // Conjunctive condition whose clauses are singletons can never have a
  // 2+-element merge group as a subset.
  EXPECT_FALSE(ActivationCondition::exclusive_segment(1, 0).IsUnchangedByMerge({1, 2}));
  EXPECT_FALSE(ActivationCondition::and_segments({1, 2}, 0).IsUnchangedByMerge({1, 2}));
  EXPECT_FALSE(ActivationCondition::and_segments({1, 3}, 0).IsUnchangedByMerge({1, 2}));

  // Composite condition: unchanged if all intersecting subgroups contain the
  // merge group as a subset.
  EXPECT_TRUE(ActivationCondition::composite_condition({{1, 2, 3}, {4, 5}}, 0)
                  .IsUnchangedByMerge({1, 2}));
  EXPECT_TRUE(ActivationCondition::composite_condition({{1, 2}, {1, 2, 4}}, 0)
                  .IsUnchangedByMerge({1, 2}));

  // Composite condition: changed if any intersecting subgroup does not contain
  // the entire merge group.
  EXPECT_FALSE(ActivationCondition::composite_condition({{1, 3}, {4, 5}}, 0)
                   .IsUnchangedByMerge({1, 2}));
  EXPECT_FALSE(ActivationCondition::composite_condition({{1, 2, 3}, {2, 4}}, 0)
                   .IsUnchangedByMerge({1, 2}));
}

TEST(ActivationConditionTest, OrAll_Validation) {
  std::vector<ActivationCondition> empty_values;
  auto empty_res = ActivationCondition::OrAll(empty_values);
  ASSERT_FALSE(empty_res.ok());
  EXPECT_EQ(empty_res.status().code(), absl::StatusCode::kInvalidArgument);

  std::vector<const ActivationCondition*> empty_ptrs;
  auto empty_ptr_res = ActivationCondition::OrAll(empty_ptrs);
  ASSERT_FALSE(empty_ptr_res.ok());
  EXPECT_EQ(empty_ptr_res.status().code(), absl::StatusCode::kInvalidArgument);

  auto cond = ActivationCondition::exclusive_segment(1, 0);
  std::vector<const ActivationCondition*> with_null = {&cond, nullptr};
  auto null_res = ActivationCondition::OrAll(with_null);
  ASSERT_FALSE(null_res.ok());
  EXPECT_EQ(null_res.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(ActivationConditionTest,
     OrAll_SingleElementPreservesExclusivenessAndPrefetches) {
  auto single = ActivationCondition::exclusive_segment(7, 10);
  single.SetEncoding(PatchEncoding::TABLE_KEYED_PARTIAL);
  single.AddPrefetches({20, 30});

  std::vector<ActivationCondition> values = {single};
  auto res = ActivationCondition::OrAll(values);
  ASSERT_TRUE(res.ok()) << res.status();
  EXPECT_EQ(*res, single);
  EXPECT_TRUE(res->IsExclusive());
  EXPECT_EQ(res->activated(), 10);
  EXPECT_EQ(res->Encoding(), PatchEncoding::TABLE_KEYED_PARTIAL);
  EXPECT_EQ(res->prefetches(), (std::vector<patch_id_t>{20, 30}));

  std::vector<const ActivationCondition*> ptrs = {&single};
  auto ptr_res = ActivationCondition::OrAll(ptrs);
  ASSERT_TRUE(ptr_res.ok()) << ptr_res.status();
  EXPECT_EQ(*ptr_res, single);
  EXPECT_TRUE(ptr_res->IsExclusive());
}

TEST(ActivationConditionTest, OrAll_AlwaysTrueAndOrderIndependence) {
  auto a = ActivationCondition::or_segments({1, 2}, 10);
  a.SetEncoding(PatchEncoding::TABLE_KEYED_FULL);
  auto b = ActivationCondition::and_segments({3, 4}, 11);
  b.SetEncoding(PatchEncoding::TABLE_KEYED_PARTIAL);
  auto t = ActivationCondition::True(5);
  t.SetEncoding(PatchEncoding::DEFAULT_ENCODING);

  for (const auto& perm : std::vector<std::vector<ActivationCondition>>{
           {t, a, b},
           {a, t, b},
           {a, b, t},
       }) {
    auto res = ActivationCondition::OrAll(perm);
    ASSERT_TRUE(res.ok()) << res.status();
    EXPECT_TRUE(res->IsAlwaysTrue());
    EXPECT_FALSE(res->IsExclusive());
    EXPECT_EQ(res->activated(), 11);
    EXPECT_EQ(res->Encoding(), PatchEncoding::TABLE_KEYED_PARTIAL);
  }

  auto c = ActivationCondition::or_segments({5}, 9);
  auto d = ActivationCondition::composite_condition({{6, 7}, {8}}, 12);
  d.SetEncoding(PatchEncoding::TABLE_KEYED_FULL);

  std::vector<ActivationCondition> items = {a, b, c, d};
  ActivationCondition expected = ActivationCondition::Or(
      ActivationCondition::Or(ActivationCondition::Or(a, b), c), d);
  std::vector<size_t> order = {0, 1, 2, 3};
  do {
    std::vector<const ActivationCondition*> ptrs;
    for (size_t idx : order) {
      ptrs.push_back(&items[idx]);
    }
    auto res = ActivationCondition::OrAll(ptrs);
    ASSERT_TRUE(res.ok()) << res.status();
    EXPECT_EQ(*res, expected);
  } while (std::next_permutation(order.begin(), order.end()));
}

TEST(ActivationConditionTest,
     OrAll_CascadingCofactorCollapseAndPartialOverlap) {
  {
    auto b = ActivationCondition::exclusive_segment(1, 0);
    auto m1 = ActivationCondition::composite_condition({{1, 2}, {2, 3}}, 0);
    auto m2 = ActivationCondition::and_segments({2, 4}, 0);

    std::vector<ActivationCondition> items = {b, m1, m2};
    ActivationCondition expected =
        ActivationCondition::Or(ActivationCondition::Or(b, m1), m2);
    EXPECT_EQ(expected, ActivationCondition::or_segments({1, 2}, 0));

    std::vector<size_t> order = {0, 1, 2};
    do {
      std::vector<const ActivationCondition*> ptrs = {
          &items[order[0]], &items[order[1]], &items[order[2]]};
      auto res = ActivationCondition::OrAll(ptrs);
      ASSERT_TRUE(res.ok()) << res.status();
      EXPECT_EQ(*res, expected);
    } while (std::next_permutation(order.begin(), order.end()));
  }

  {
    auto b = ActivationCondition::exclusive_segment(1, 0);
    auto m1 = ActivationCondition::composite_condition({{1, 2}, {3, 4}}, 0);
    auto m2 = ActivationCondition::composite_condition({{2, 5}, {3}}, 0);

    std::vector<ActivationCondition> items = {b, m1, m2};
    ActivationCondition expected =
        ActivationCondition::Or(ActivationCondition::Or(b, m1), m2);

    std::vector<size_t> order = {0, 1, 2};
    do {
      std::vector<const ActivationCondition*> ptrs = {
          &items[order[0]], &items[order[1]], &items[order[2]]};
      auto res = ActivationCondition::OrAll(ptrs);
      ASSERT_TRUE(res.ok()) << res.status();
      EXPECT_EQ(*res, expected);
    } while (std::next_permutation(order.begin(), order.end()));
  }
}

TEST(ActivationConditionTest,
     OrAll_DelayedUnitaryAbsorptionAvoidsIntermediateBlowup) {
  SegmentSet base_segments;
  base_segments.insert_range(0, 999);

  std::vector<ActivationCondition> conditions;
  conditions.reserve(311);
  conditions.push_back(ActivationCondition::or_segments(base_segments, 0));

  for (segment_index_t i = 0; i < 20; ++i) {
    for (segment_index_t j = 0; j < 10; ++j) {
      segment_index_t l = 1000 + i;
      segment_index_t v = 1020 + j;
      segment_index_t t = 1030 + ((i + j) % 10);
      conditions.push_back(ActivationCondition::and_segments({l, v, t}, 0));
    }
  }
  for (segment_index_t i = 0; i < 10; ++i) {
    for (segment_index_t j = 0; j < 10; ++j) {
      segment_index_t l = 1000 + i;
      segment_index_t v = 1020 + j;
      conditions.push_back(ActivationCondition::and_segments({l, v}, 0));
    }
  }
  for (segment_index_t j = 0; j < 10; ++j) {
    conditions.push_back(ActivationCondition::exclusive_segment(1020 + j, 0));
    base_segments.insert(1020 + j);
  }

  auto res = ActivationCondition::OrAll(conditions);
  ASSERT_TRUE(res.ok()) << res.status();
  EXPECT_EQ(*res, ActivationCondition::or_segments(base_segments, 0));
}

TEST(ActivationConditionTest, OrAll_TwoElementsMatchesOr) {
  auto excl = ActivationCondition::exclusive_segment(1, 10);
  excl.SetEncoding(PatchEncoding::TABLE_KEYED_FULL);
  excl.AddPrefetches({20});

  auto comp = ActivationCondition::composite_condition({{2, 3}, {4}}, 12);
  comp.SetEncoding(PatchEncoding::TABLE_KEYED_PARTIAL);

  auto always_true = ActivationCondition::True(5);

  for (const auto& [lhs, rhs] :
       std::vector<std::pair<ActivationCondition, ActivationCondition>>{
           {excl, comp},
           {comp, excl},
           {excl, always_true},
           {always_true, excl},
       }) {
    std::vector<const ActivationCondition*> ptrs = {&lhs, &rhs};
    auto res = ActivationCondition::OrAll(ptrs);
    ASSERT_TRUE(res.ok()) << res.status();
    ActivationCondition expected = ActivationCondition::Or(lhs, rhs);
    EXPECT_EQ(*res, expected);
    EXPECT_FALSE(res->IsExclusive());
  }
}

TEST(ActivationConditionTest,
     OrAll_SubsumedMultiClauseAndMidFoldSingleClauseCollapse) {
  // Multi-clause subsumption (A ⊆ B => B implies A => A OR B == A), with larger
  // extensions appearing before smaller terms.
  {
    auto a = ActivationCondition::composite_condition({{0, 1}, {2}}, 0);
    auto b1 =
        ActivationCondition::composite_condition({{0, 1}, {2}, {3}, {4}}, 0);
    auto b2 = ActivationCondition::composite_condition(
        {{0, 1}, {2}, {5, 6}, {7, 8}, {9}}, 0);

    std::vector<ActivationCondition> inputs = {b2, b1, a};
    auto res = ActivationCondition::OrAll(inputs);
    ASSERT_TRUE(res.ok()) << res.status();
    EXPECT_EQ(*res, a);
  }

  // Zero single-clause inputs with large multi-clause chains before small
  // 2-clause terms (models WorkSans/Biryani pattern where balanced-tree folding
  // blows up intermediate cross products before reaching small absorbing
  // terms).
  {
    std::vector<ActivationCondition> inputs;
    for (segment_index_t k = 0; k < 40; ++k) {
      inputs.push_back(ActivationCondition::composite_condition(
          {{0, 1},
           {10 + (k % 20), 10 + ((k + 1) % 20)},
           {200 + k},
           {300 + k},
           {400 + k}},
          0));
    }
    for (segment_index_t k = 0; k < 20; ++k) {
      inputs.push_back(
          ActivationCondition::composite_condition({{0, 1}, {10 + k}}, 0));
    }

    auto res = ActivationCondition::OrAll(inputs);
    ASSERT_TRUE(res.ok()) << res.status();

    ActivationCondition expected = inputs.back();
    for (size_t i = inputs.size() - 1; i-- > 0;) {
      expected = ActivationCondition::Or(expected, inputs[i]);
    }
    EXPECT_EQ(*res, expected);
  }

  // Mid-fold collapse: (t1 OR t2) collapses to 1 clause (s0 OR s1), which must
  // fold into base_disjunction and distribute across remaining multi-clause t3.
  {
    auto t1 = ActivationCondition::composite_condition({{0, 1}, {0, 2}}, 0);
    auto t2 = ActivationCondition::composite_condition({{0, 1}, {1, 3}}, 0);
    auto t3 = ActivationCondition::composite_condition({{0, 4}, {5, 6}}, 0);

    ActivationCondition expected =
        ActivationCondition::composite_condition({{0, 1, 4}, {0, 1, 5, 6}}, 0);
    EXPECT_EQ(ActivationCondition::Or(ActivationCondition::Or(t1, t2), t3),
              expected);

    std::vector<ActivationCondition> items = {t1, t2, t3};
    std::vector<size_t> order = {0, 1, 2};
    do {
      std::vector<const ActivationCondition*> ptrs = {
          &items[order[0]], &items[order[1]], &items[order[2]]};
      auto res = ActivationCondition::OrAll(ptrs);
      ASSERT_TRUE(res.ok()) << res.status();
      EXPECT_EQ(*res, expected);
    } while (std::next_permutation(order.begin(), order.end()));
  }
}

TEST(ActivationConditionTest,
     OrAll_DeduplicatesManyIdenticalLargeMultiClauseConditions) {
  SegmentSet s0;
  s0.insert(0);
  s0.insert_range(1000, 1064);
  SegmentSet s1;
  s1.insert(1);
  s1.insert_range(2000, 2064);
  SegmentSet s2;
  s2.insert(2);
  s2.insert_range(3000, 3064);
  SegmentSet s3;
  s3.insert(3);
  s3.insert_range(4000, 4064);
  SegmentSet s4;
  s4.insert(4);
  s4.insert_range(5000, 5064);

  auto c1 = ActivationCondition::composite_condition({s0, s1}, 0);
  auto c2 = ActivationCondition::composite_condition({s3, s4}, 0);
  // Subsumed by `c1` (tests <= 8-clause subsumption scan).
  auto c1_subsumed = ActivationCondition::composite_condition({s0, s1, s2}, 0);

  ActivationCondition expected = ActivationCondition::Or(c1, c2);

  std::vector<ActivationCondition> conditions;
  conditions.reserve(201);
  for (int i = 0; i < 100; ++i) {
    conditions.push_back(c1);
    conditions.push_back(c2);
  }
  conditions.push_back(c1_subsumed);

  std::vector<const ActivationCondition*> ptrs;
  ptrs.reserve(conditions.size());
  for (const auto& c : conditions) {
    ptrs.push_back(&c);
  }
  auto res = ActivationCondition::OrAll(ptrs);
  ASSERT_TRUE(res.ok()) << res.status();
  EXPECT_EQ(*res, expected);

  // Also cover > 8-clause terms so `ContainsAllClauses` exercises its
  // linear-merge branch for both subsumed and non-subsumed pairs.
  std::vector<SegmentSet> nine_clauses;
  for (unsigned i = 0; i < 9; ++i) {
    nine_clauses.push_back({i, 500});
  }
  auto large_base = ActivationCondition::composite_condition(nine_clauses, 0);
  std::vector<SegmentSet> ten_clauses = nine_clauses;
  ten_clauses.push_back({9, 500});
  auto large_subsumed =
      ActivationCondition::composite_condition(ten_clauses, 0);
  auto small_other =
      ActivationCondition::composite_condition({{50, 501}, {51, 502}}, 0);

  ActivationCondition large_expected =
      ActivationCondition::Or(large_base, small_other);
  auto large_res = ActivationCondition::OrAll(
      {&large_base, &large_base, &large_subsumed, &small_other});
  ASSERT_TRUE(large_res.ok()) << large_res.status();
  EXPECT_EQ(*large_res, large_expected);
}

TEST(ActivationConditionTest,
     OrAll_FastComparatorAndSubsumptionOrderIndependence) {
  auto t_eq_a = ActivationCondition::composite_condition({{0, 1}, {2, 3}}, 0);
  auto t_eq_b = ActivationCondition::composite_condition({{0, 4}, {5, 6}}, 0);
  auto t_diff_size =
      ActivationCondition::composite_condition({{7}, {8, 9, 10}}, 0);
  auto t_subsumed =
      ActivationCondition::composite_condition({{0, 1}, {2, 3}, {11, 12}}, 0);

  ActivationCondition expected = ActivationCondition::Or(
      ActivationCondition::Or(t_eq_a, t_eq_b), t_diff_size);

  std::vector<ActivationCondition> items = {t_eq_a, t_eq_b, t_diff_size,
                                            t_subsumed};
  std::vector<size_t> order = {0, 1, 2, 3};
  do {
    std::vector<const ActivationCondition*> ptrs = {
        &items[order[0]], &items[order[1]], &items[order[2]], &items[order[3]]};
    auto res = ActivationCondition::OrAll(ptrs);
    ASSERT_TRUE(res.ok()) << res.status();
    EXPECT_EQ(*res, expected);
  } while (std::next_permutation(order.begin(), order.end()));
}

}  // namespace ift::encoder
