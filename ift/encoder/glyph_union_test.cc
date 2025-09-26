#include "ift/encoder/glyph_union.h"

#include "absl/status/status.h"
#include "common/int_set.h"
#include "gtest/gtest.h"

using common::GlyphSet;

namespace ift::encoder {

class GlyphUnionTest : public ::testing::Test {};

TEST_F(GlyphUnionTest, SingleGid) {
  GlyphUnion gu(1);
  ASSERT_TRUE(gu.Union(GlyphSet{0}).ok());
  ASSERT_EQ(*gu.Find(0), 0);
}

TEST_F(GlyphUnionTest, BasicOperation) {
  GlyphUnion gu(10);

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

TEST_F(GlyphUnionTest, UnionWithEmptyOrSingleSet) {
  GlyphUnion gu(5);

  ASSERT_TRUE(gu.Union({}).ok());
  ASSERT_TRUE(gu.Union({2}).ok());

  ASSERT_EQ(*gu.Find(0), 0);
  ASSERT_EQ(*gu.Find(1), 1);
  ASSERT_EQ(*gu.Find(2), 2);
  ASSERT_EQ(*gu.Find(3), 3);
  ASSERT_EQ(*gu.Find(4), 4);
}

TEST_F(GlyphUnionTest, OutOfBounds) {
  GlyphUnion gu(10);

  // GlyphsFor
  auto status = gu.Find(10);
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status.status().code(), absl::StatusCode::kInvalidArgument);

  status = gu.Find(100);
  ASSERT_FALSE(status.ok());
  ASSERT_EQ(status.status().code(), absl::StatusCode::kInvalidArgument);

  // Union
  auto union_status = gu.Union({10});
  ASSERT_FALSE(union_status.ok());
  ASSERT_EQ(union_status.code(), absl::StatusCode::kInvalidArgument);

  union_status = gu.Union({1, 10});
  ASSERT_FALSE(union_status.ok());
  ASSERT_EQ(union_status.code(), absl::StatusCode::kInvalidArgument);

  union_status = gu.Union({11, 2});
  ASSERT_FALSE(union_status.ok());
  ASSERT_EQ(union_status.code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(GlyphUnionTest, Copy) {
  GlyphUnion gu(10);
  ASSERT_TRUE(gu.Union({1, 3, 5}).ok());
  ASSERT_TRUE(gu.Union({2, 4}).ok());

  // Test copy constructor
  GlyphUnion gu2(gu);
  ASSERT_EQ(*gu2.Find(1), *gu2.Find(3));
  ASSERT_EQ(*gu2.Find(1), *gu2.Find(5));
  ASSERT_EQ(*gu2.Find(3), *gu2.Find(5));
  ASSERT_EQ(*gu2.Find(2), *gu2.Find(4));
  ASSERT_NE(*gu2.Find(1), *gu2.Find(2));

  // Test copy assignment
  GlyphUnion gu3(5);
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

TEST_F(GlyphUnionTest, UnionPair) {
  GlyphUnion gu(10);
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

}  // namespace ift::encoder
