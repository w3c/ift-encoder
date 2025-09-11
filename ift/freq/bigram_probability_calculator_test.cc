#include "ift/freq/bigram_probability_calculator.h"

#include "gtest/gtest.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/unicode_frequencies.h"

using ift::encoder::Segment;
using ift::encoder::SubsetDefinition;

namespace ift::freq {

TEST(BigramProbabilityCalculatorTest, ComputeProbability) {
  UnicodeFrequencies frequencies{
      {{'a', 'a'}, 70}, {{'b', 'b'}, 60}, {{'c', 'c'}, 100},

      {{'a', 'b'}, 40}, {{'a', 'c'}, 50}, {{'b', 'c'}, 60},
  };

  BigramProbabilityCalculator calc(std::move(frequencies));

  ASSERT_EQ(calc.ComputeProbability({}), (ProbabilityBound{1, 1}));

  ASSERT_EQ(calc.ComputeProbability({'a'}), (ProbabilityBound{0.7, 0.7}));
  ASSERT_EQ(calc.ComputeProbability({'b'}), (ProbabilityBound{0.6, 0.6}));
  ASSERT_EQ(calc.ComputeProbability({'c'}), (ProbabilityBound{1.0, 1.0}));
  ASSERT_EQ(calc.ComputeProbability({'a', 'b'}),
            (ProbabilityBound{0.70 + 0.60 - 0.40, 1.0}));

  auto b = calc.ComputeProbability({'a', 'b', 'c'});
  ASSERT_DOUBLE_EQ(b.Min(), 1.00 + 0.70 + 0.60 - 0.40 - 0.50 - 0.60);
  ASSERT_DOUBLE_EQ(b.Max(), 1.00);
}

TEST(BigramProbabilityCalculatorTest, ComputeMergedProbability) {
  UnicodeFrequencies frequencies{
      {{'a', 'a'}, 70}, {{'b', 'b'}, 60}, {{'c', 'c'}, 100},

      {{'a', 'b'}, 40}, {{'a', 'c'}, 50}, {{'b', 'c'}, 60},
  };

  BigramProbabilityCalculator calc(std::move(frequencies));

  Segment s1{{'a'}, ProbabilityBound::Zero()};
  Segment s2{{'b'}, ProbabilityBound::Zero()};
  ASSERT_EQ(calc.ComputeMergedProbability({&s1, &s2}),
            (ProbabilityBound{0.70 + 0.60 - 0.40, 1.0}));
}

TEST(BigramProbabilityCalculatorTest, ComputeProbability_Clamped) {
  UnicodeFrequencies frequencies{
      {{'a', 'a'}, 10}, {{'b', 'b'}, 20}, {{'c', 'c'}, 100},

      {{'a', 'b'}, 40}, {{'a', 'c'}, 50}, {{'b', 'c'}, 60},
  };

  BigramProbabilityCalculator calc(std::move(frequencies));
  auto b = calc.ComputeProbability({'a', 'b'});
  ASSERT_DOUBLE_EQ(b.Min(), 0.0);
  ASSERT_DOUBLE_EQ(b.Max(), 0.3);

  UnicodeFrequencies frequencies2{
      {{'a', 'a'}, 100}, {{'b', 'b'}, 100}, {{'c', 'c'}, 100},

      {{'a', 'b'}, 40},  {{'a', 'c'}, 50},  {{'b', 'c'}, 60},
  };

  BigramProbabilityCalculator calc2(std::move(frequencies2));
  b = calc2.ComputeProbability({'a', 'b'});
  ASSERT_DOUBLE_EQ(b.Min(), 1.0);
  ASSERT_DOUBLE_EQ(b.Max(), 1.0);
}

TEST(BigramProbabilityCalculatorTest, ComputeProbability_WithLayoutTags) {
  // TODO XXX test with layout tags
}

TEST(BigramProbabilityCalculatorTest, ComputeConjunctiveProbability) {
  // TODO XXX
}

}  // namespace ift::freq
