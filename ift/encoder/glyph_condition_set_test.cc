#include "ift/encoder/glyph_condition_set.h"

#include <gtest/gtest.h>

#include "ift/common/int_set.h"
#include "ift/encoder/activation_condition.h"

namespace ift::encoder {

using ift::common::GlyphSet;
using ift::common::SegmentSet;

TEST(GlyphConditionSetTest, Invalidate_GlyphAndSegments_Simplification) {
  // Checks a case where simplification causes additional segments to
  // be dropped.
  GlyphConditionSet condition_set(1);

  // if (s1 OR s2) AND (s3 OR s1) => g0
  ActivationCondition cond = ActivationCondition::composite_condition(
      {SegmentSet{1, 2}, SegmentSet{3, 1}}, 0);
  condition_set.SetCondition(0, cond);

  auto s = condition_set.Validate();
  ASSERT_TRUE(s.ok()) << s;

  condition_set.InvalidateGlyphInformation(GlyphSet{0}, SegmentSet{3});
  EXPECT_EQ(condition_set.ConditionsFor(0).activation(),
            ActivationCondition::or_segments({1}, 0));

  EXPECT_TRUE(condition_set.GlyphsWithSegment(2).empty());
  EXPECT_TRUE(condition_set.GlyphsWithSegment(3).empty());
  EXPECT_EQ(condition_set.GlyphsWithSegment(1), (GlyphSet{0}));

  s = condition_set.Validate();
  ASSERT_TRUE(s.ok()) << s;
}

TEST(GlyphConditionSetTest, DefaultAlwaysTrue) {
  GlyphConditionSet condition_set(5);
  for (uint32_t i = 0; i < 5; ++i) {
    EXPECT_TRUE(condition_set.ConditionsFor(i).activation().IsAlwaysTrue());
  }
}

TEST(GlyphConditionSetTest, AddAndCondition) {
  GlyphConditionSet condition_set(2);

  condition_set.AddAndCondition(0, 10);
  EXPECT_EQ(condition_set.ConditionsFor(0).activation(),
            ActivationCondition::exclusive_segment(10, 0));

  EXPECT_EQ(condition_set.GlyphsWithSegment(10), (GlyphSet{0}));
  EXPECT_EQ(condition_set.GlyphsWithSegment(20), (GlyphSet{}));

  condition_set.AddAndCondition(0, 20);
  EXPECT_EQ(condition_set.ConditionsFor(0).activation(),
            ActivationCondition::and_segments({10, 20}, 0));
  EXPECT_EQ(condition_set.GlyphsWithSegment(10), (GlyphSet{0}));
  EXPECT_EQ(condition_set.GlyphsWithSegment(20), (GlyphSet{0}));

  auto s = condition_set.Validate();
  ASSERT_TRUE(s.ok()) << s;
}

TEST(GlyphConditionSetTest, AddOrCondition) {
  GlyphConditionSet condition_set(2);

  condition_set.AddOrCondition(0, 10);
  EXPECT_EQ(condition_set.ConditionsFor(0).activation(),
            ActivationCondition::or_segments({10}, 0));
  EXPECT_EQ(condition_set.GlyphsWithSegment(10), (GlyphSet{0}));
  EXPECT_EQ(condition_set.GlyphsWithSegment(20), (GlyphSet{}));

  // Add another OR condition
  condition_set.AddOrCondition(0, 20);
  EXPECT_EQ(condition_set.ConditionsFor(0).activation(),
            ActivationCondition::or_segments({10, 20}, 0));
  EXPECT_EQ(condition_set.GlyphsWithSegment(10), (GlyphSet{0}));
  EXPECT_EQ(condition_set.GlyphsWithSegment(20), (GlyphSet{0}));

  auto s = condition_set.Validate();
  ASSERT_TRUE(s.ok()) << s;
}

TEST(GlyphConditionSetTest, SetCondition) {
  GlyphConditionSet condition_set(2);
  ActivationCondition cond = ActivationCondition::and_segments({10, 20}, 0);

  condition_set.SetCondition(0, cond);
  EXPECT_EQ(condition_set.ConditionsFor(0).activation(), cond);
  EXPECT_EQ(condition_set.GlyphsWithSegment(10), (GlyphSet{0}));
  EXPECT_EQ(condition_set.GlyphsWithSegment(20), (GlyphSet{0}));

  // Update condition, removing s10 and adding s30
  ActivationCondition cond2 = ActivationCondition::and_segments({20, 30}, 0);
  condition_set.SetCondition(0, cond2);
  EXPECT_EQ(condition_set.ConditionsFor(0).activation(), cond2);
  EXPECT_EQ(condition_set.GlyphsWithSegment(10), (GlyphSet{}));
  EXPECT_EQ(condition_set.GlyphsWithSegment(20), (GlyphSet{0}));
  EXPECT_EQ(condition_set.GlyphsWithSegment(30), (GlyphSet{0}));

  auto s = condition_set.Validate();
  ASSERT_TRUE(s.ok()) << s;
}

TEST(GlyphConditionSetTest, GlyphsWithSegmentEmpty) {
  GlyphConditionSet condition_set(2);
  EXPECT_TRUE(condition_set.GlyphsWithSegment(0).empty());
  EXPECT_TRUE(condition_set.GlyphsWithSegment(99).empty());
}

TEST(GlyphConditionSetTest, InvalidateGlyphInformation_Glyphs) {
  GlyphConditionSet condition_set(3);
  condition_set.AddAndCondition(0, 10);
  condition_set.AddAndCondition(1, 20);
  condition_set.AddAndCondition(2, 10);

  condition_set.InvalidateGlyphInformation(GlyphSet{0, 1});

  EXPECT_TRUE(condition_set.ConditionsFor(0).activation().IsAlwaysTrue());
  EXPECT_TRUE(condition_set.ConditionsFor(1).activation().IsAlwaysTrue());
  EXPECT_EQ(condition_set.ConditionsFor(2).activation(),
            ActivationCondition::exclusive_segment(10, 0));

  EXPECT_EQ(condition_set.GlyphsWithSegment(10), (GlyphSet{2}));
  EXPECT_EQ(condition_set.GlyphsWithSegment(20), (GlyphSet{}));
}

TEST(GlyphConditionSetTest, InvalidateGlyphInformation_Segments) {
  GlyphConditionSet condition_set(5);
  condition_set.SetCondition(0, ActivationCondition::and_segments({10, 20}, 0));
  condition_set.SetCondition(1, ActivationCondition::or_segments({20, 30}, 0));
  condition_set.AddAndCondition(2, 10);
  condition_set.SetCondition(
      3, ActivationCondition::and_segments({10, 20, 30}, 0));
  condition_set.SetCondition(4,
                             ActivationCondition::or_segments({10, 20, 30}, 0));

  EXPECT_EQ(condition_set.GlyphsWithSegment(10), (GlyphSet{0, 2, 3, 4}));
  EXPECT_EQ(condition_set.GlyphsWithSegment(20), (GlyphSet{0, 1, 3, 4}));
  EXPECT_EQ(condition_set.GlyphsWithSegment(30), (GlyphSet{1, 3, 4}));

  condition_set.InvalidateGlyphInformation(SegmentSet{20});

  EXPECT_EQ(condition_set.ConditionsFor(0).activation(),
            ActivationCondition::exclusive_segment(10, 0));
  EXPECT_EQ(condition_set.ConditionsFor(1).activation(),
            ActivationCondition::or_segments({30}, 0));
  EXPECT_EQ(condition_set.ConditionsFor(2).activation(),
            ActivationCondition::exclusive_segment(10, 0));
  EXPECT_EQ(condition_set.ConditionsFor(3).activation(),
            ActivationCondition::and_segments({10, 30}, 0));
  EXPECT_EQ(condition_set.ConditionsFor(4).activation(),
            ActivationCondition::or_segments({10, 30}, 0));

  EXPECT_EQ(condition_set.GlyphsWithSegment(10), (GlyphSet{0, 2, 3, 4}));
  EXPECT_EQ(condition_set.GlyphsWithSegment(20), (GlyphSet{}));
  EXPECT_EQ(condition_set.GlyphsWithSegment(30), (GlyphSet{1, 3, 4}));
}

TEST(GlyphConditionSetTest, InvalidateGlyphInformation_GlyphsAndSegments) {
  GlyphConditionSet condition_set(3);
  condition_set.SetCondition(0, ActivationCondition::and_segments({10, 20}, 0));
  condition_set.SetCondition(1, ActivationCondition::and_segments({10, 20}, 0));

  // Invalidate s10 for g0 only
  condition_set.InvalidateGlyphInformation(GlyphSet{0}, SegmentSet{10});

  // g0 should lose s10
  EXPECT_EQ(condition_set.ConditionsFor(0).activation(),
            ActivationCondition::exclusive_segment(20, 0));
  // g1 should be untouched
  EXPECT_EQ(condition_set.ConditionsFor(1).activation(),
            ActivationCondition::and_segments({10, 20}, 0));

  EXPECT_EQ(condition_set.GlyphsWithSegment(10), (GlyphSet{1}));
  EXPECT_EQ(condition_set.GlyphsWithSegment(20), (GlyphSet{0, 1}));
}

TEST(GlyphConditionSetTest, EqualityOperators) {
  GlyphConditionSet set1(3);
  GlyphConditionSet set2(3);

  EXPECT_TRUE(set1 == set2);
  EXPECT_FALSE(set1 != set2);

  set1.AddAndCondition(0, 10);
  EXPECT_FALSE(set1 == set2);
  EXPECT_TRUE(set1 != set2);

  set2.AddAndCondition(0, 10);
  EXPECT_TRUE(set1 == set2);
  EXPECT_FALSE(set1 != set2);

  GlyphConditionSet set3(4);
  EXPECT_FALSE(set1 == set3);
  EXPECT_TRUE(set1 != set3);
}

}  // namespace ift::encoder
