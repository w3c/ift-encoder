#include "ift/encoder/merge_strategy.h"

#include <memory>
#include <optional>
#include <sstream>

#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "ift/freq/mock_probability_calculator.h"

namespace ift::encoder {
namespace {

using ift::freq::MockProbabilityCalculator;

TEST(MergeStrategyTest, NoneStrategy) {
  MergeStrategy strategy = MergeStrategy::None();
  EXPECT_TRUE(strategy.IsNone());
  EXPECT_FALSE(strategy.UseCosts());
  EXPECT_EQ(strategy.ProbabilityProfiles().size(), 1);
  EXPECT_TRUE(strategy.ProbabilityCalculator().ok());
  EXPECT_FALSE(strategy.HasInitFontMerge());
}

TEST(MergeStrategyTest, HeuristicStrategy) {
  MergeStrategy strategy = MergeStrategy::Heuristic(100, 500);
  EXPECT_FALSE(strategy.IsNone());
  EXPECT_FALSE(strategy.UseCosts());
  EXPECT_EQ(strategy.PatchSizeMinBytes(), 100);
  EXPECT_EQ(strategy.PatchSizeMaxBytes(), 500);
  EXPECT_EQ(strategy.ProbabilityProfiles().size(), 1);
  EXPECT_TRUE(strategy.ProbabilityCalculator().ok());
  EXPECT_FALSE(strategy.HasInitFontMerge());
}

TEST(MergeStrategyTest, CostBasedSingleProfile) {
  auto calc = std::make_shared<MockProbabilityCalculator>(
      std::vector<std::pair<Segment, double>>{});
  MergeStrategy strategy = MergeStrategy::CostBased(calc, 75, 4);

  EXPECT_TRUE(strategy.UseCosts());
  EXPECT_EQ(strategy.NetworkOverheadCost(), 75);
  EXPECT_EQ(strategy.MinimumGroupSize(), 4);
  EXPECT_EQ(strategy.ProbabilityProfiles().size(), 1);
  EXPECT_EQ(*strategy.ProbabilityCalculator(), calc.get());
  EXPECT_FALSE(strategy.HasInitFontMerge());
  EXPECT_EQ(strategy.InitFontMergeThreshold(), std::nullopt);
  EXPECT_EQ(strategy.InitFontMergeProbabilityThreshold(), std::nullopt);

  strategy.SetInitFontMergeThreshold(-50.0);
  strategy.SetInitFontMergeProbabilityThreshold(0.01);
  EXPECT_TRUE(strategy.HasInitFontMerge());
  EXPECT_EQ(strategy.InitFontMergeThreshold(), -50.0);
  EXPECT_EQ(strategy.InitFontMergeProbabilityThreshold(), 0.01);
  EXPECT_EQ(strategy.ProbabilityProfiles()[0].init_font_merge_threshold, -50.0);
  EXPECT_EQ(strategy.ProbabilityProfiles()[0].init_font_merge_probability_threshold,
            0.01);
}

TEST(MergeStrategyTest, MultipleProfiles) {
  auto calc1 = std::make_shared<MockProbabilityCalculator>(
      std::vector<std::pair<Segment, double>>{});
  auto calc2 = std::make_shared<MockProbabilityCalculator>(
      std::vector<std::pair<Segment, double>>{});

  MergeStrategy strategy = MergeStrategy::CostBased(calc1, 75, 1);
  strategy.SetInitFontMergeThreshold(-100.0);
  strategy.SetInitFontMergeProbabilityThreshold(0.05);

  strategy.AddProbabilityCalculator(calc2, std::nullopt, 0.02);

  EXPECT_EQ(strategy.ProbabilityProfiles().size(), 2);
  EXPECT_TRUE(strategy.HasInitFontMerge());

  // Check first profile
  EXPECT_EQ(strategy.ProbabilityProfiles()[0].calculator.get(), calc1.get());
  EXPECT_EQ(strategy.ProbabilityProfiles()[0].init_font_merge_threshold, -100.0);
  EXPECT_EQ(strategy.ProbabilityProfiles()[0].init_font_merge_probability_threshold,
            0.05);

  // Check second profile
  EXPECT_EQ(strategy.ProbabilityProfiles()[1].calculator.get(), calc2.get());
  EXPECT_EQ(strategy.ProbabilityProfiles()[1].init_font_merge_threshold,
            std::nullopt);
  EXPECT_EQ(strategy.ProbabilityProfiles()[1].init_font_merge_probability_threshold,
            0.02);

  // Backward compatibility accessors reflect the first profile
  EXPECT_EQ(*strategy.ProbabilityCalculator(), calc1.get());
  EXPECT_EQ(strategy.InitFontMergeThreshold(), -100.0);
  EXPECT_EQ(strategy.InitFontMergeProbabilityThreshold(), 0.05);
}

TEST(MergeStrategyTest, AddProbabilityProfileStruct) {
  auto calc = std::make_shared<MockProbabilityCalculator>(
      std::vector<std::pair<Segment, double>>{});
  MergeStrategy strategy = MergeStrategy::CostBased(calc, 75, 1);

  MergeStrategy::ProbabilityProfile p2{
      .calculator = std::make_shared<MockProbabilityCalculator>(
          std::vector<std::pair<Segment, double>>{}),
      .init_font_merge_threshold = -20.0,
      .init_font_merge_probability_threshold = 0.001,
  };
  strategy.AddProbabilityProfile(p2);

  EXPECT_EQ(strategy.ProbabilityProfiles().size(), 2);
  EXPECT_EQ(strategy.ProbabilityProfiles()[1].init_font_merge_threshold, -20.0);
  EXPECT_EQ(strategy.ProbabilityProfiles()[1].init_font_merge_probability_threshold,
            0.001);
}

TEST(MergeStrategyTest, EqualityOperator) {
  auto calc1 = std::make_shared<MockProbabilityCalculator>(
      std::vector<std::pair<Segment, double>>{});
  auto calc2 = std::make_shared<MockProbabilityCalculator>(
      std::vector<std::pair<Segment, double>>{});

  MergeStrategy s1 = MergeStrategy::CostBased(calc1, 75, 2);
  MergeStrategy s2 = MergeStrategy::CostBased(calc2, 75, 2);
  EXPECT_EQ(s1, s2);

  s1.SetInitFontMergeThreshold(-10.0);
  EXPECT_NE(s1, s2);
  s2.SetInitFontMergeThreshold(-10.0);
  EXPECT_EQ(s1, s2);

  s1.AddProbabilityCalculator(calc2, -5.0, 0.1);
  EXPECT_NE(s1, s2);
  s2.AddProbabilityCalculator(calc1, -5.0, 0.1);
  EXPECT_EQ(s1, s2);

  s2.SetUsePatchMerges(true);
  EXPECT_NE(s1, s2);
}

TEST(MergeStrategyTest, PrintToOutput) {
  auto calc1 = std::make_shared<MockProbabilityCalculator>(
      std::vector<std::pair<Segment, double>>{});
  auto calc2 = std::make_shared<MockProbabilityCalculator>(
      std::vector<std::pair<Segment, double>>{});

  MergeStrategy strategy = MergeStrategy::CostBased(calc1, 75, 1);
  strategy.SetInitFontMergeThreshold(-50.0);
  strategy.AddProbabilityCalculator(calc2, -25.0, 0.01);

  std::ostringstream os;
  PrintTo(strategy, &os);
  std::string output = os.str();

  EXPECT_NE(output.find("CostBased"), std::string::npos);
  EXPECT_NE(output.find("profile[0]"), std::string::npos);
  EXPECT_NE(output.find("init_font_merge_threshold = -50"), std::string::npos);
  EXPECT_NE(output.find("profile[1]"), std::string::npos);
  EXPECT_NE(output.find("init_font_merge_threshold = -25"), std::string::npos);
  EXPECT_NE(output.find("init_font_merge_probability_threshold = 0.01"),
            std::string::npos);
}

}  // namespace
}  // namespace ift::encoder
