#include "ift/common/branch_factor.h"

#include "gtest/gtest.h"

namespace ift::common {

class BranchFactorTest : public ::testing::Test {};

TEST_F(BranchFactorTest, NodeSizes) {
  EXPECT_EQ(2, kBFNodeSize[BF2]);
  EXPECT_EQ(4, kBFNodeSize[BF4]);
  EXPECT_EQ(8, kBFNodeSize[BF8]);
  EXPECT_EQ(32, kBFNodeSize[BF32]);
}

TEST_F(BranchFactorTest, TwigSizes) {
  EXPECT_EQ(kBFNodeSize[BF2] * kBFNodeSize[BF2], kBFTwigSize[BF2]);
  EXPECT_EQ(kBFNodeSize[BF4] * kBFNodeSize[BF4], kBFTwigSize[BF4]);
  EXPECT_EQ(kBFNodeSize[BF8] * kBFNodeSize[BF8], kBFTwigSize[BF8]);
  EXPECT_EQ(kBFNodeSize[BF32] * kBFNodeSize[BF32], kBFTwigSize[BF32]);
}

}  // namespace ift::common
