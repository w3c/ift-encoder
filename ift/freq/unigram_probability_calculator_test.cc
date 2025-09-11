#include "ift/freq/unigram_probability_calculator.h"

#include "gtest/gtest.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/probability_bound.h"
#include "ift/freq/unicode_frequencies.h"

using ift::encoder::Segment;
using ift::encoder::SubsetDefinition;

namespace ift::freq {

TEST(UnigramProbabilityCalculatorTest, ComputeProbability) {
  UnicodeFrequencies frequencies;
  frequencies.Add(1, 1, 10);
  frequencies.Add(2, 2, 20);
  frequencies.Add(3, 3, 5);

  UnigramProbabilityCalculator calculator(std::move(frequencies));

  SubsetDefinition def1;
  def1.codepoints = {1, 2};

  double p1 = 10.0 / 20.0;
  double p2 = 20.0 / 20.0;
  double expected_prob1 = 1.0 - (1.0 - p1) * (1.0 - p2);

  ProbabilityBound bound1 = calculator.ComputeProbability(def1);
  EXPECT_DOUBLE_EQ(bound1.Min(), expected_prob1);
  EXPECT_DOUBLE_EQ(bound1.Max(), expected_prob1);

  SubsetDefinition def2;
  def2.codepoints = {1, 3};

  double p3 = 5.0 / 20.0;
  double expected_prob2 = 1.0 - (1.0 - p1) * (1.0 - p3);

  ProbabilityBound bound2 = calculator.ComputeProbability(def2);
  EXPECT_DOUBLE_EQ(bound2.Min(), expected_prob2);
  EXPECT_DOUBLE_EQ(bound2.Max(), expected_prob2);
}

TEST(UnigramProbabilityCalculatorTest, ComputeMergedProbability) {
  UnicodeFrequencies frequencies;
  frequencies.Add(1, 1, 10);
  frequencies.Add(2, 2, 20);
  frequencies.Add(3, 3, 5);

  UnigramProbabilityCalculator calculator(std::move(frequencies));

  Segment s1{{1}, calculator.ComputeProbability({1})};
  Segment s3{{3}, calculator.ComputeProbability({3})};
  s3.SetProbability(calculator.ComputeProbability(s3.Definition()));

  double p1 = 10.0 / 20.0;
  double p3 = 5.0 / 20.0;
  double expected_prob1 = 1.0 - (1.0 - p1) * (1.0 - p3);

  ProbabilityBound bound = calculator.ComputeMergedProbability({&s1, &s3});
  EXPECT_DOUBLE_EQ(bound.Min(), expected_prob1);
  EXPECT_DOUBLE_EQ(bound.Max(), expected_prob1);
}

TEST(UnigramProbabilityCalculatorTest, ComputeConjunctiveProbability) {
  Segment s1{{'a'}, ProbabilityBound{0.5, 0.5}};
  Segment s2{{'b'}, ProbabilityBound{0.2, 0.2}};
  Segment s3{{'c'}, ProbabilityBound{0.7, 0.7}};

  UnicodeFrequencies frequencies;
  frequencies.Add(1, 1, 10);

  UnigramProbabilityCalculator calculator(std::move(frequencies));

  std::vector<const Segment *> segments{&s2};
  ProbabilityBound bound = calculator.ComputeConjunctiveProbability(segments);
  EXPECT_DOUBLE_EQ(bound.Min(), 0.2);
  EXPECT_DOUBLE_EQ(bound.Max(), 0.2);

  segments = {&s1, &s3};
  bound = calculator.ComputeConjunctiveProbability(segments);
  EXPECT_DOUBLE_EQ(bound.Min(), 0.5 * 0.7);
  EXPECT_DOUBLE_EQ(bound.Max(), 0.5 * 0.7);

  segments = {&s1, &s3, &s2};
  bound = calculator.ComputeConjunctiveProbability(segments);
  EXPECT_DOUBLE_EQ(bound.Min(), 0.5 * 0.7 * 0.2);
  EXPECT_DOUBLE_EQ(bound.Max(), 0.5 * 0.7 * 0.2);

  segments = {};
  bound = calculator.ComputeConjunctiveProbability(segments);
  EXPECT_DOUBLE_EQ(bound.Min(), 1.0);
  EXPECT_DOUBLE_EQ(bound.Max(), 1.0);
}

}  // namespace ift::freq
