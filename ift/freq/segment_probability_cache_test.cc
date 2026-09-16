#include "ift/freq/segment_probability_cache.h"

#include "gtest/gtest.h"
#include "ift/common/int_set.h"
#include "ift/freq/probability_bound.h"

using ift::common::SegmentSet;
using ift::freq::ProbabilityBound;
using ift::freq::SegmentProbabilityCache;

namespace {

TEST(SegmentProbabilityCacheTest, ClearAndAccess) {
  SegmentProbabilityCache cache;
  cache.Reset(3);

  EXPECT_FALSE(cache[0].has_value());
  EXPECT_FALSE(cache[1].has_value());
  EXPECT_FALSE(cache[2].has_value());

  cache[1] = ProbabilityBound(0.25, 0.75);
  ASSERT_TRUE(cache[1].has_value());
  EXPECT_DOUBLE_EQ(cache[1]->Min(), 0.25);
  EXPECT_DOUBLE_EQ(cache[1]->Max(), 0.75);

  // Clear resets entries and resizes
  cache.Reset(2);
  EXPECT_FALSE(cache[1].has_value());
  EXPECT_FALSE(cache[2].has_value());
}

TEST(SegmentProbabilityCacheTest, Invalidate) {
  SegmentProbabilityCache cache;
  cache.Reset(4);

  cache[0] = ProbabilityBound(0.1, 0.1);
  cache[1] = ProbabilityBound(0.2, 0.2);
  cache[2] = ProbabilityBound(0.3, 0.3);
  cache[3] = ProbabilityBound(0.4, 0.4);

  SegmentSet to_invalidate = {1, 3, 10};
  cache.Invalidate(to_invalidate);

  EXPECT_TRUE(cache[0].has_value());
  EXPECT_FALSE(cache[1].has_value());
  EXPECT_TRUE(cache[2].has_value());
  EXPECT_FALSE(cache[3].has_value());
}

}  // namespace
