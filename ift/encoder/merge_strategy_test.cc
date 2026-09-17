#include "ift/encoder/merge_strategy.h"

#include <memory>
#include <optional>
#include <sstream>

#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "ift/freq/bigram_probability_calculator.h"
#include "ift/freq/mock_probability_calculator.h"
#include "ift/freq/unicode_frequencies.h"
#include "ift/freq/unigram_probability_calculator.h"

namespace ift::encoder {
namespace {

using ift::freq::BigramProbabilityCalculator;
using ift::freq::MockProbabilityCalculator;
using ift::freq::UnicodeFrequencies;
using ift::freq::UnigramProbabilityCalculator;
using ProbabilityProfile = MergeStrategy::ProbabilityProfile;

std::shared_ptr<MockProbabilityCalculator> MockCalculator() {
  return std::make_shared<MockProbabilityCalculator>(
      std::vector<std::pair<Segment, double>>{});
}

TEST(MergeStrategyTest, NoneStrategy) {
  MergeStrategy strategy = MergeStrategy::None();
  EXPECT_TRUE(strategy.IsNone());
  EXPECT_FALSE(strategy.UseCosts());
  EXPECT_TRUE(strategy.ProbabilityProfiles().empty());
  EXPECT_FALSE(strategy.HasInitFontMerge());
}

TEST(MergeStrategyTest, HeuristicStrategy) {
  MergeStrategy strategy = MergeStrategy::Heuristic(100, 500);
  EXPECT_FALSE(strategy.IsNone());
  EXPECT_FALSE(strategy.UseCosts());
  EXPECT_EQ(strategy.PatchSizeMinBytes(), 100);
  EXPECT_EQ(strategy.PatchSizeMaxBytes(), 500);
  EXPECT_TRUE(strategy.ProbabilityProfiles().empty());
  EXPECT_FALSE(strategy.HasInitFontMerge());
}

TEST(MergeStrategyTest, UnigramProfile) {
  UnicodeFrequencies frequencies{
      {{' ', ' '}, 100},
      {{'a', 'a'}, 50},
  };

  auto profile = ProbabilityProfile::Unigram(std::move(frequencies));
  ASSERT_TRUE(profile.ok()) << profile.status();
  EXPECT_NE(
      dynamic_cast<const UnigramProbabilityCalculator*>(*profile->Calculator()),
      nullptr);
  EXPECT_EQ(profile->init_font_merge_threshold, std::nullopt);
  EXPECT_EQ(profile->init_font_merge_probability_threshold, std::nullopt);
}

TEST(MergeStrategyTest, BigramProfile) {
  UnicodeFrequencies frequencies{
      {{' ', ' '}, 100},
      {{'a', 'a'}, 50},
  };

  auto profile = ProbabilityProfile::Bigram(std::move(frequencies));
  ASSERT_TRUE(profile.ok()) << profile.status();
  EXPECT_NE(
      dynamic_cast<const BigramProbabilityCalculator*>(*profile->Calculator()),
      nullptr);
}

TEST(MergeStrategyTest, ProfileRequiresFrequencyData) {
  EXPECT_FALSE(ProbabilityProfile::Unigram(UnicodeFrequencies{}).ok());
  EXPECT_FALSE(ProbabilityProfile::Bigram(UnicodeFrequencies{}).ok());
}

TEST(MergeStrategyTest, ProfileWithoutCalculator) {
  ProbabilityProfile profile;
  EXPECT_FALSE(profile.Calculator().ok());
}

TEST(MergeStrategyTest, CostBasedSingleProfile) {
  auto calc = MockCalculator();
  MergeStrategy strategy =
      MergeStrategy::CostBased(ProbabilityProfile(calc), 75, 4);

  EXPECT_TRUE(strategy.UseCosts());
  EXPECT_EQ(strategy.NetworkOverheadCost(), 75);
  EXPECT_EQ(strategy.MinimumGroupSize(), 4);
  EXPECT_EQ(strategy.ProbabilityProfiles().size(), 1);
  EXPECT_EQ(*strategy.ProbabilityCalculator(0), calc.get());
  EXPECT_FALSE(strategy.HasInitFontMerge());
  EXPECT_EQ(strategy.ProbabilityProfiles()[0].init_font_merge_threshold,
            std::nullopt);
  EXPECT_EQ(
      strategy.ProbabilityProfiles()[0].init_font_merge_probability_threshold,
      std::nullopt);
}

TEST(MergeStrategyTest, CostBasedProfileWithInitFontThresholds) {
  auto calc = MockCalculator();
  MergeStrategy strategy =
      MergeStrategy::CostBased(ProbabilityProfile(calc, -50.0, 0.01), 75, 4);

  EXPECT_TRUE(strategy.HasInitFontMerge());
  EXPECT_EQ(strategy.ProbabilityProfiles()[0].init_font_merge_threshold, -50.0);
  EXPECT_EQ(
      strategy.ProbabilityProfiles()[0].init_font_merge_probability_threshold,
      0.01);
}

TEST(MergeStrategyTest, OutOfBoundsProfileIndex) {
  auto calc = MockCalculator();
  MergeStrategy strategy =
      MergeStrategy::CostBased(ProbabilityProfile(calc), 75, 4);

  EXPECT_TRUE(strategy.GetProbabilityProfile(0).ok());
  EXPECT_FALSE(strategy.GetProbabilityProfile(1).ok());
  EXPECT_FALSE(strategy.ProbabilityCalculator(1).ok());
}

TEST(MergeStrategyTest, MultipleProfiles) {
  auto calc1 = MockCalculator();
  auto calc2 = MockCalculator();

  MergeStrategy strategy =
      MergeStrategy::CostBased(ProbabilityProfile(calc1, -100.0, 0.05), 75, 1);
  strategy.AddProbabilityProfile(ProbabilityProfile(calc2, std::nullopt, 0.02));

  EXPECT_EQ(strategy.ProbabilityProfiles().size(), 2);
  EXPECT_TRUE(strategy.HasInitFontMerge());

  // Check first profile
  EXPECT_EQ(strategy.ProbabilityProfiles()[0].calculator.get(), calc1.get());
  EXPECT_EQ(strategy.ProbabilityProfiles()[0].init_font_merge_threshold,
            -100.0);
  EXPECT_EQ(
      strategy.ProbabilityProfiles()[0].init_font_merge_probability_threshold,
      0.05);

  // Check second profile
  EXPECT_EQ(strategy.ProbabilityProfiles()[1].calculator.get(), calc2.get());
  EXPECT_EQ(strategy.ProbabilityProfiles()[1].init_font_merge_threshold,
            std::nullopt);
  EXPECT_EQ(
      strategy.ProbabilityProfiles()[1].init_font_merge_probability_threshold,
      0.02);

  // Indexed accessors reference the corresponding profile.
  EXPECT_EQ(*strategy.ProbabilityCalculator(0), calc1.get());
  EXPECT_EQ(*strategy.ProbabilityCalculator(1), calc2.get());
  EXPECT_EQ((*strategy.GetProbabilityProfile(1))->init_font_merge_threshold,
            std::nullopt);
}

TEST(MergeStrategyTest, AddProbabilityProfileStruct) {
  auto calc = MockCalculator();
  MergeStrategy strategy =
      MergeStrategy::CostBased(ProbabilityProfile(calc), 75, 1);

  strategy.AddProbabilityProfile(
      ProbabilityProfile(MockCalculator(), -20.0, 0.001));

  EXPECT_EQ(strategy.ProbabilityProfiles().size(), 2);
  EXPECT_EQ(strategy.ProbabilityProfiles()[1].init_font_merge_threshold, -20.0);
  EXPECT_EQ(
      strategy.ProbabilityProfiles()[1].init_font_merge_probability_threshold,
      0.001);
}

TEST(MergeStrategyTest, EqualityOperator) {
  auto calc1 = MockCalculator();
  auto calc2 = MockCalculator();

  MergeStrategy s1 = MergeStrategy::CostBased(ProbabilityProfile(calc1), 75, 2);
  MergeStrategy s2 = MergeStrategy::CostBased(ProbabilityProfile(calc2), 75, 2);
  EXPECT_EQ(s1, s2);

  MergeStrategy s3 =
      MergeStrategy::CostBased(ProbabilityProfile(calc1, -10.0), 75, 2);
  EXPECT_NE(s1, s3);
  MergeStrategy s4 =
      MergeStrategy::CostBased(ProbabilityProfile(calc2, -10.0), 75, 2);
  EXPECT_EQ(s3, s4);

  s3.AddProbabilityProfile(ProbabilityProfile(calc2, -5.0, 0.1));
  EXPECT_NE(s3, s4);
  s4.AddProbabilityProfile(ProbabilityProfile(calc1, -5.0, 0.1));
  EXPECT_EQ(s3, s4);

  s4.SetUsePatchMerges(true);
  EXPECT_NE(s3, s4);
}

TEST(MergeStrategyTest, PrintToOutput) {
  auto calc1 = MockCalculator();
  auto calc2 = MockCalculator();

  MergeStrategy strategy =
      MergeStrategy::CostBased(ProbabilityProfile(calc1, -50.0), 75, 1);
  strategy.AddProbabilityProfile(ProbabilityProfile(calc2, -25.0, 0.01));

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
