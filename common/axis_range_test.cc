#include "common/axis_range.h"

#include <optional>

#include "gtest/gtest.h"

namespace common {

class AxisRangeTest : public ::testing::Test {};

TEST_F(AxisRangeTest, AxisRange_Intersects) {
  auto a = AxisRange::Range(1, 4);
  auto b = AxisRange::Range(5, 9);
  ASSERT_FALSE(a->Intersects(*b));
  ASSERT_FALSE(b->Intersects(*a));

  auto c = AxisRange::Range(1, 5);
  auto d = AxisRange::Range(5, 9);
  ASSERT_TRUE(c->Intersects(*d));
  ASSERT_TRUE(d->Intersects(*c));

  auto e = AxisRange::Range(1, 8);
  auto f = AxisRange::Range(3, 6);
  ASSERT_TRUE(e->Intersects(*f));
  ASSERT_TRUE(f->Intersects(*e));

  auto g = AxisRange::Range(5, 5);
  ASSERT_FALSE(a->Intersects(*g));
  ASSERT_FALSE(g->Intersects(*a));

  ASSERT_TRUE(c->Intersects(*g));
  ASSERT_TRUE(g->Intersects(*c));

  ASSERT_TRUE(f->Intersects(*g));
  ASSERT_TRUE(g->Intersects(*f));
}

TEST_F(AxisRangeTest, AxisRange_Intersection) {
  auto a = AxisRange::Range(1, 4);
  auto b = AxisRange::Range(2, 9);
  auto c = AxisRange::Range(0, 5);
  auto d = AxisRange::Range(4, 7);
  auto e = AxisRange::Range(10, 12);

  ASSERT_EQ(a->Intersection(*e), std::nullopt);

  ASSERT_EQ(a->Intersection(*b), *AxisRange::Range(2, 4));
  ASSERT_EQ(b->Intersection(*a), *AxisRange::Range(2, 4));

  ASSERT_EQ(a->Intersection(*c), *AxisRange::Range(1, 4));
  ASSERT_EQ(c->Intersection(*a), *AxisRange::Range(1, 4));

  ASSERT_EQ(a->Intersection(*d), AxisRange::Point(4));
  ASSERT_EQ(d->Intersection(*a), AxisRange::Point(4));
}

TEST_F(AxisRangeTest, AxisRange_Creation) {
  {
    auto range = AxisRange::Point(1.5);
    ASSERT_EQ(range.start(), 1.5);
    ASSERT_EQ(range.end(), 1.5);
  }

  auto range = AxisRange::Range(2.5, 3.5);
  ASSERT_TRUE(range.ok()) << range.status();
  ASSERT_EQ(range->start(), 2.5);
  ASSERT_EQ(range->end(), 3.5);

  range = AxisRange::Range(2, 2);
  ASSERT_TRUE(range.ok()) << range.status();
  ASSERT_EQ(range->start(), 2);
  ASSERT_EQ(range->end(), 2);

  range = AxisRange::Range(3, 2);
  ASSERT_TRUE(absl::IsInvalidArgument(range.status())) << range.status();
}

}  // namespace common