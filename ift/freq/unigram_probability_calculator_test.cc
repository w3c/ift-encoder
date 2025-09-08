#include "ift/freq/unigram_probability_calculator.h"

#include "gtest/gtest.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
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
  EXPECT_DOUBLE_EQ(bound1.min, expected_prob1);
  EXPECT_DOUBLE_EQ(bound1.max, expected_prob1);

  SubsetDefinition def2;
  def2.codepoints = {1, 3};

  double p3 = 5.0 / 20.0;
  double expected_prob2 = 1.0 - (1.0 - p1) * (1.0 - p3);

  ProbabilityBound bound2 = calculator.ComputeProbability(def2);
  EXPECT_DOUBLE_EQ(bound2.min, expected_prob2);
  EXPECT_DOUBLE_EQ(bound2.max, expected_prob2);
}

TEST(UnigramProbabilityCalculatorTest, ComputeConjunctiveProbability) {
  Segment s1{{'a'}, 0.5};
  Segment s2{{'b'}, 0.2};
  Segment s3{{'c'}, 0.7};

  UnicodeFrequencies frequencies;
  frequencies.Add(1, 1, 10);

  UnigramProbabilityCalculator calculator(std::move(frequencies));

  std::vector<const Segment *> segments{&s2};
  ProbabilityBound bound = calculator.ComputeConjunctiveProbability(segments);
  EXPECT_DOUBLE_EQ(bound.min, 0.2);
  EXPECT_DOUBLE_EQ(bound.max, 0.2);

  segments = {&s1, &s3};
  bound = calculator.ComputeConjunctiveProbability(segments);
  EXPECT_DOUBLE_EQ(bound.min, 0.5 * 0.7);
  EXPECT_DOUBLE_EQ(bound.max, 0.5 * 0.7);

  segments = {&s1, &s3, &s2};
  bound = calculator.ComputeConjunctiveProbability(segments);
  EXPECT_DOUBLE_EQ(bound.min, 0.5 * 0.7 * 0.2);
  EXPECT_DOUBLE_EQ(bound.max, 0.5 * 0.7 * 0.2);

  segments = {};
  bound = calculator.ComputeConjunctiveProbability(segments);
  EXPECT_DOUBLE_EQ(bound.min, 1.0);
  EXPECT_DOUBLE_EQ(bound.max, 1.0);
}

}  // namespace ift::freq
