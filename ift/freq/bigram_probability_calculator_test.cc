#include "ift/freq/bigram_probability_calculator.h"

#include "gtest/gtest.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/probability_bound.h"
#include "ift/freq/unicode_frequencies.h"

using ift::encoder::Segment;
using ift::encoder::SubsetDefinition;

namespace ift::freq {

TEST(BigramProbabilityCalculatorTest, ComputeProbability) {
  UnicodeFrequencies frequencies{
      {{'a', 'a'}, 70}, {{'b', 'b'}, 60}, {{'c', 'c'}, 100}, {{'d', 'd'}, 50},

      {{'a', 'b'}, 40}, {{'a', 'c'}, 50}, {{'b', 'c'}, 60},  {{'a', 'd'}, 45},
      {{'b', 'd'}, 17},
  };

  BigramProbabilityCalculator calc(std::move(frequencies));

  ASSERT_EQ(calc.ComputeProbability({}), (ProbabilityBound{1, 1}));

  ASSERT_EQ(calc.ComputeProbability({'a'}), (ProbabilityBound{0.7, 0.7}));
  ASSERT_EQ(calc.ComputeProbability({'b'}), (ProbabilityBound{0.6, 0.6}));
  ASSERT_EQ(calc.ComputeProbability({'c'}), (ProbabilityBound{1.0, 1.0}));

  double Pab = 0.70 + 0.60 - 0.40;  // 0.9
  ASSERT_EQ(calc.ComputeProbability({'a', 'b'}), (ProbabilityBound{Pab, Pab}));

  double Pbd = 0.60 + 0.50 - 0.17;
  double Pabd_upper =
      0.70 + 0.60 + 0.50 - 0.40 - 0.45;  // sum(Pi) - P(a and b) - P(a and d)
  auto b = calc.ComputeProbability({'a', 'b', 'd'});
  ASSERT_DOUBLE_EQ(b.Min(), Pbd);
  ASSERT_DOUBLE_EQ(b.Max(), Pabd_upper);
}

TEST(BigramProbabilityCalculatorTest, ComputeMergedProbability) {
  UnicodeFrequencies frequencies{
      {{'a', 'a'}, 70}, {{'b', 'b'}, 60}, {{'c', 'c'}, 100},

      {{'a', 'b'}, 40}, {{'a', 'c'}, 50}, {{'b', 'c'}, 60},
  };

  BigramProbabilityCalculator calc(std::move(frequencies));

  Segment s1{{'a'}, calc.ComputeProbability({'a'})};
  Segment s2{{'b'}, calc.ComputeProbability({'b'})};

  double Pab = 0.70 + 0.60 - 0.40;
  ASSERT_EQ(calc.ComputeMergedProbability({&s1, &s2}),
            (ProbabilityBound{Pab, Pab}));
}

TEST(BigramProbabilityCalculatorTest,
     ComputeMergedProbability_SegmentProbConsidered) {
  UnicodeFrequencies frequencies{
      {{'a', 'a'}, 70}, {{'b', 'b'}, 60}, {{'c', 'c'}, 100},

      {{'a', 'b'}, 40}, {{'a', 'c'}, 50}, {{'b', 'c'}, 60},
  };

  BigramProbabilityCalculator calc(std::move(frequencies));

  Segment s1{{'a'}, ProbabilityBound{0.93, 0.85}};
  Segment s2{{'b'}, calc.ComputeProbability({'b'})};

  // On merge the individual segment probabilities can be used in the new lower
  // bound
  ASSERT_EQ(calc.ComputeMergedProbability({&s1, &s2}),
            (ProbabilityBound{0.93, 0.93}));
}

TEST(BigramProbabilityCalculatorTest, ComputeMergedProbability_Complex) {
  UnicodeFrequencies frequencies{
      {{'a', 'a'}, 70}, {{'b', 'b'}, 60}, {{'c', 'c'}, 100}, {{'d', 'd'}, 55},
      {{'e', 'e'}, 65}, {{'a', 'b'}, 40}, {{'a', 'c'}, 50},  {{'b', 'c'}, 60},
      {{'a', 'd'}, 30}, {{'b', 'd'}, 20}, {{'c', 'd'}, 35},  {{'a', 'e'}, 5},
      {{'b', 'e'}, 10}, {{'c', 'e'}, 15}, {{'d', 'e'}, 20},
  };

  BigramProbabilityCalculator calc(std::move(frequencies));

  Segment s1{{'a', 'b'}, calc.ComputeProbability({'a', 'b'})};
  Segment s2{{'c', 'd'}, calc.ComputeProbability({'c', 'd'})};
  ProbabilityBound expected = calc.ComputeProbability({'a', 'b', 'c', 'd'});
  ASSERT_EQ(calc.ComputeMergedProbability({&s1, &s2}), expected);

  expected = calc.ComputeProbability(s1.Definition());
  ASSERT_EQ(calc.ComputeMergedProbability({&s1}), expected);

  expected = calc.ComputeProbability({'a', 'b', 'c', 'd', 'e'});
  Segment s3{{'a', 'd'}, calc.ComputeProbability({'a', 'd'})};
  Segment s4{{'b', 'e'}, calc.ComputeProbability({'b', 'e'})};
  Segment s5{{'c'}, calc.ComputeProbability({'c'})};
  ProbabilityBound actual = calc.ComputeMergedProbability({&s3, &s4, &s5});
  ASSERT_NEAR(actual.Min(), expected.Min(), 1e-9);
  ASSERT_NEAR(actual.Max(), expected.Max(), 1e-9);
}

TEST(BigramProbabilityCalculatorTest, ComputeProbability_Clamped) {
  UnicodeFrequencies frequencies{
      {{'a', 'a'}, 10}, {{'b', 'b'}, 20}, {{'c', 'c'}, 100},

      {{'a', 'b'}, 40}, {{'a', 'c'}, 50}, {{'b', 'c'}, 60},
  };

  BigramProbabilityCalculator calc(std::move(frequencies));
  auto b = calc.ComputeProbability({'a', 'b'});
  ASSERT_DOUBLE_EQ(b.Min(), 0.2);  // P(b) sets a lower bound in this case
  ASSERT_DOUBLE_EQ(b.Max(), 0.2);
}

TEST(BigramProbabilityCalculatorTest, ComputeProbability_ClampedUpper) {
  UnicodeFrequencies frequencies{
      {{'a', 'a'}, 80}, {{'b', 'b'}, 90}, {{'c', 'c'}, 100},

      {{'a', 'b'}, 10}, {{'a', 'c'}, 10}, {{'b', 'c'}, 10},
  };

  BigramProbabilityCalculator calc(std::move(frequencies));
  auto b = calc.ComputeProbability({'a', 'b'});
  ASSERT_DOUBLE_EQ(b.Min(), 1.0);  // P(b) sets a lower bound in this case
  ASSERT_DOUBLE_EQ(b.Max(), 1.0);
}

TEST(BigramProbabilityCalculatorTest, ComputeProbability_WithLayoutTags) {
  // TODO(garretrieger): XXXX test with layout tags once implemented.
}

TEST(BigramProbabilityCalculatorTest, ComputeConjunctiveProbability) {
  UnicodeFrequencies freqs;
  BigramProbabilityCalculator calculator(std::move(freqs));

  Segment s1(SubsetDefinition({1}), ProbabilityBound(0.8, 0.9));
  Segment s2(SubsetDefinition({2}), ProbabilityBound(0.7, 0.8));

  std::vector<const Segment*> segments = {&s1, &s2};

  // sum(min) = 0.8 + 0.7 = 1.5
  // n = 2
  // min = 1.5 - 2 + 1 = 0.5
  // max = min(0.9, 0.8) = 0.8
  ProbabilityBound result = calculator.ComputeConjunctiveProbability(segments);
  ASSERT_DOUBLE_EQ(result.Min(), 0.5);
  ASSERT_DOUBLE_EQ(result.Max(), 0.8);
}

TEST(BigramProbabilityCalculatorTest, ComputeConjunctiveProbability_Clamped) {
  UnicodeFrequencies freqs;
  BigramProbabilityCalculator calculator(std::move(freqs));

  Segment s1(SubsetDefinition({1}), ProbabilityBound(0.1, 0.2));
  Segment s2(SubsetDefinition({2}), ProbabilityBound(0.3, 0.4));
  Segment s3(SubsetDefinition({3}), ProbabilityBound(0.5, 0.6));

  std::vector<const Segment*> segments = {&s1, &s2, &s3};

  // sum(min) = 0.1 + 0.3 + 0.5 = 0.9
  // n = 3
  // min = max(0.0, 0.9 - 3 + 1) = 0.0
  // max = min(0.2, 0.4, 0.6) = 0.2
  ProbabilityBound result = calculator.ComputeConjunctiveProbability(segments);
  ASSERT_DOUBLE_EQ(result.Min(), 0.0);
  ASSERT_DOUBLE_EQ(result.Max(), 0.2);
}

TEST(BigramProbabilityCalculatorTest,
     ComputeConjunctiveProbabilitySingleSegment) {
  UnicodeFrequencies freqs;
  BigramProbabilityCalculator calculator(std::move(freqs));

  Segment s1(SubsetDefinition({1}), ProbabilityBound(0.1, 0.2));

  std::vector<const Segment*> segments = {&s1};

  // sum(min) = 0.1
  // n = 1
  // min = 0.1 - 1 + 1 = 0.1
  // max = 0.2
  ProbabilityBound result = calculator.ComputeConjunctiveProbability(segments);
  ASSERT_DOUBLE_EQ(result.Min(), 0.1);
  ASSERT_DOUBLE_EQ(result.Max(), 0.2);
}

TEST(BigramProbabilityCalculatorTest, ComputeConjunctiveProbabilityNoSegments) {
  UnicodeFrequencies freqs;
  BigramProbabilityCalculator calculator(std::move(freqs));

  std::vector<const Segment*> segments;

  // sum(min) = 0
  // n = 0
  // min = 0 - 0 + 1 = 1.0
  // max = 1.0
  ProbabilityBound result = calculator.ComputeConjunctiveProbability(segments);
  ASSERT_DOUBLE_EQ(result.Min(), 1.0);
  ASSERT_DOUBLE_EQ(result.Max(), 1.0);
}

}  // namespace ift::freq
