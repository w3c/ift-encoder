#include "ift/encoder/glyph_partition.h"

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "common/int_set.h"
#include "gtest/gtest.h"

using common::GlyphSet;

namespace ift::encoder {

class GlyphPartitionTest : public ::testing::Test {};

TEST_F(GlyphPartitionTest, SingleGid) {
  GlyphPartition gu(1);
  ASSERT_TRUE(gu.Union(GlyphSet{0}).ok());
  ASSERT_EQ(*gu.Find(0), 0);
}

TEST_F(GlyphPartitionTest, BasicOperation) {
  GlyphPartition gu(10);

  // Initially, all glyphs are in their own set.
  ASSERT_EQ(*gu.Find(0), 0);
  ASSERT_EQ(*gu.Find(5), 5);

  // Union some glyphs
  ASSERT_TRUE(gu.Union({1, 3, 5}).ok());
  ASSERT_EQ(*gu.Find(1), *gu.Find(5));
  ASSERT_EQ(*gu.Find(3), *gu.Find(5));
  ASSERT_EQ(*gu.Find(3), *gu.Find(1));
  ASSERT_NE(*gu.Find(1), *gu.Find(2));

  // Other glyphs should be unaffected
  ASSERT_EQ(*gu.Find(0), 0);
  ASSERT_EQ(*gu.Find(2), 2);
  ASSERT_EQ(*gu.Find(4), 4);

  // Union another set
  ASSERT_TRUE(gu.Union({2, 4}).ok());
  ASSERT_EQ(*gu.Find(2), *gu.Find(4));
  ASSERT_NE(*gu.Find(1), *gu.Find(2));

  // Union overlapping sets
  ASSERT_TRUE(gu.Union({5, 2}).ok());
  ASSERT_EQ(*gu.Find(1), *gu.Find(5));
  ASSERT_EQ(*gu.Find(3), *gu.Find(5));
  ASSERT_EQ(*gu.Find(1), *gu.Find(4));
  ASSERT_EQ(*gu.Find(3), *gu.Find(4));
  ASSERT_NE(*gu.Find(3), *gu.Find(6));

  // Check a glyph not in any union
  ASSERT_EQ(*gu.Find(9), 9);
}

TEST_F(GlyphPartitionTest, NonIdentityGroups) {
  GlyphPartition gu(10);

  ASSERT_TRUE(gu.Union({1, 3, 5}).ok());

  std::vector<GlyphSet> expected = {{1, 3, 5}};

  ASSERT_EQ(*gu.NonIdentityGroups(), absl::Span<const GlyphSet>(expected));

  ASSERT_TRUE(gu.Union({2, 4}).ok());

  expected.push_back({2, 4});
  ASSERT_EQ(*gu.NonIdentityGroups(), absl::Span<const GlyphSet>(expected));
}

TEST_F(GlyphPartitionTest, GlyphsFor) {
  GlyphPartition gu(10);

  ASSERT_TRUE(gu.Union({1, 3, 5}).ok());
  ASSERT_TRUE(gu.Union({2, 4}).ok());

  ASSERT_EQ(*gu.GlyphsFor(1), (GlyphSet{1, 3, 5}));
  ASSERT_EQ(*gu.GlyphsFor(3), (GlyphSet{1, 3, 5}));
  ASSERT_EQ(*gu.GlyphsFor(5), (GlyphSet{1, 3, 5}));
  ASSERT_EQ(*gu.GlyphsFor(2), (GlyphSet{2, 4}));
  ASSERT_EQ(*gu.GlyphsFor(4), (GlyphSet{2, 4}));
  ASSERT_EQ(*gu.GlyphsFor(6), (GlyphSet{6}));

  ASSERT_TRUE(gu.Union(3, 2).ok());
  ASSERT_EQ(*gu.GlyphsFor(1), (GlyphSet{1, 2, 3, 4, 5}));
  ASSERT_EQ(*gu.GlyphsFor(2), (GlyphSet{1, 2, 3, 4, 5}));
  ASSERT_EQ(*gu.GlyphsFor(6), (GlyphSet{6}));
}

TEST_F(GlyphPartitionTest, UnionWithEmptyOrSingleSet) {
  GlyphPartition gu(5);

  ASSERT_TRUE(gu.Union(GlyphSet{}).ok());
  ASSERT_TRUE(gu.Union(GlyphSet{2}).ok());

  ASSERT_EQ(*gu.Find(0), 0);
  ASSERT_EQ(*gu.Find(1), 1);
  ASSERT_EQ(*gu.Find(2), 2);
  ASSERT_EQ(*gu.Find(3), 3);
  ASSERT_EQ(*gu.Find(4), 4);
}

TEST_F(GlyphPartitionTest, OutOfBounds) {
  GlyphPartition gu(10);

  // Find
  auto status = gu.Find(10);
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status.status().code(), absl::StatusCode::kInvalidArgument);

  status = gu.Find(100);
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status.status().code(), absl::StatusCode::kInvalidArgument);

  // GlyphsFor
  auto glyphs_for_status = gu.GlyphsFor(10);
  ASSERT_FALSE(glyphs_for_status.ok());
  ASSERT_EQ(glyphs_for_status.status().code(),
            absl::StatusCode::kInvalidArgument);

  // Union
  auto union_status = gu.Union(GlyphSet{10});
  ASSERT_FALSE(union_status.ok());
  ASSERT_EQ(union_status.code(), absl::StatusCode::kInvalidArgument);

  union_status = gu.Union({1, 10});
  ASSERT_FALSE(union_status.ok());
  ASSERT_EQ(union_status.code(), absl::StatusCode::kInvalidArgument);

  union_status = gu.Union({11, 2});
  ASSERT_FALSE(union_status.ok());
  ASSERT_EQ(union_status.code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(GlyphPartitionTest, Copy) {
  GlyphPartition gu(10);
  ASSERT_TRUE(gu.Union({1, 3, 5}).ok());
  ASSERT_TRUE(gu.Union({2, 4}).ok());

  // Test copy constructor
  GlyphPartition gu2(gu);
  ASSERT_EQ(*gu2.Find(1), *gu2.Find(3));
  ASSERT_EQ(*gu2.Find(1), *gu2.Find(5));
  ASSERT_EQ(*gu2.Find(3), *gu2.Find(5));
  ASSERT_EQ(*gu2.Find(2), *gu2.Find(4));
  ASSERT_NE(*gu2.Find(1), *gu2.Find(2));

  // Test copy assignment
  GlyphPartition gu3(5);
  gu3 = gu;
  ASSERT_EQ(*gu3.Find(1), *gu3.Find(3));
  ASSERT_EQ(*gu3.Find(1), *gu3.Find(5));
  ASSERT_EQ(*gu3.Find(3), *gu3.Find(5));
  ASSERT_EQ(*gu3.Find(2), *gu3.Find(4));
  ASSERT_NE(*gu3.Find(1), *gu3.Find(2));

  // Test that copies are independent
  ASSERT_TRUE(gu.Union({1, 2}).ok());
  ASSERT_EQ(*gu.Find(1), *gu.Find(2));
  ASSERT_NE(*gu2.Find(1), *gu2.Find(2));
  ASSERT_NE(*gu3.Find(1), *gu3.Find(2));
}

TEST_F(GlyphPartitionTest, UnionPair) {
  GlyphPartition gu(10);
  ASSERT_TRUE(gu.Union(1, 3).ok());
  ASSERT_EQ(*gu.Find(1), *gu.Find(3));
  ASSERT_NE(*gu.Find(1), *gu.Find(2));

  ASSERT_TRUE(gu.Union(3, 3).ok());
  ASSERT_EQ(*gu.Find(1), *gu.Find(3));
  ASSERT_NE(*gu.Find(1), *gu.Find(2));

  ASSERT_TRUE(gu.Union(3, 5).ok());
  ASSERT_EQ(*gu.Find(1), *gu.Find(3));
  ASSERT_EQ(*gu.Find(1), *gu.Find(5));
  ASSERT_EQ(*gu.Find(3), *gu.Find(5));
  ASSERT_NE(*gu.Find(1), *gu.Find(2));
}

TEST_F(GlyphPartitionTest, UnionOtherUnion) {
  GlyphPartition gu1(10);
  GlyphPartition gu2(10);

  ASSERT_TRUE(gu1.Union(gu2).ok());
  ASSERT_EQ(*gu1.Find(3), 3);
  ASSERT_EQ(*gu2.Find(3), 3);
  ASSERT_EQ(*gu1.Find(8), 8);
  ASSERT_EQ(*gu2.Find(8), 8);

  ASSERT_TRUE(gu1.Union(1, 3).ok());
  ASSERT_TRUE(gu1.Union(gu2).ok());

  ASSERT_EQ(*gu1.Find(1), *gu1.Find(3));
  ASSERT_EQ(*gu1.Find(8), 8);

  ASSERT_TRUE(gu2.Union(7, 9).ok());
  ASSERT_TRUE(gu2.Union(9, 8).ok());

  ASSERT_TRUE(gu1.Union(gu2).ok());
  ASSERT_EQ(*gu1.GlyphsFor(1), (GlyphSet{1, 3}));
  ASSERT_EQ(*gu1.GlyphsFor(8), (GlyphSet{7, 8, 9}));

  GlyphPartition gu3(10);
  ASSERT_TRUE(gu3.Union(3, 7).ok());

  ASSERT_TRUE(gu1.Union(gu3).ok());
  ASSERT_EQ(*gu1.GlyphsFor(1), (GlyphSet{1, 3, 7, 8, 9}));
}

TEST_F(GlyphPartitionTest, UnionOtherUnion_Invalid) {
  GlyphPartition gu1(10);
  GlyphPartition gu2(11);
  ASSERT_TRUE(absl::IsInvalidArgument(gu1.Union(gu2)));
  ASSERT_TRUE(absl::IsInvalidArgument(gu2.Union(gu1)));
}

}  // namespace ift::encoder