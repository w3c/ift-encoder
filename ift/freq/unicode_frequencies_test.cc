#include "ift/freq/unicode_frequencies.h"

#include "common/int_set.h"
#include "gtest/gtest.h"

using common::CodepointSet;

namespace ift::freq {

TEST(UnicodeFrequenciesTest, ProbabilityFor_NoFrequencies) {
  UnicodeFrequencies freq;
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(1, 2), 0.0);
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(1), 0.0);
}

TEST(UnicodeFrequenciesTest, ProbabilityFor) {
  UnicodeFrequencies freq{
      {{1, 2}, 10},
      {{3, 2}, 20},
      {{1, 1}, 5},
  };

  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(1, 2), 10.0 / 20.0);
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(2, 1), 10.0 / 20.0);
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(2, 3), 20.0 / 20.0);
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(3, 2), 20.0 / 20.0);
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(1), 5.0 / 20.0);
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(1, 1), 5.0 / 20.0);
}

TEST(UnicodeFrequenciesTest, ProbabilityFor_MissingFrequency) {
  UnicodeFrequencies freq{
      {{1, 2}, 10},
      {{2, 3}, 20},
      {{3, 3}, 15},
  };

  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(1), 1.0 / 20.0);
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(1, 1), 1.0 / 20.0);

  // For P(a n b) with unknown probabilities they are assumed to be
  // independent.
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(4, 5), 1.0 / (20.0 * 20.0));
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(3, 4), 15.0 / (20.0 * 20.0));
}

TEST(UnicodeFrequenciesTest, AddAccumulates) {
  UnicodeFrequencies freq;

  freq.Add(2, 3, 20);
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(2, 3), 20.0 / 20.0);

  freq.Add(1, 2, 10);
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(1, 2), 10.0 / 20.0);

  freq.Add(2, 1, 15);
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(1, 2), 1.0);
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(2, 3), 20.0 / 25.0);
}

TEST(UnicodeFrequenciesTest, CoveredCodepoints) {
  UnicodeFrequencies freq{
      {{1, 2}, 10},
      {{1, 1}, 5},
      {{3, 2}, 20},
      {{5, 5}, 10},
  };
  EXPECT_EQ(freq.CoveredCodepoints(), (CodepointSet{1, 5}));

  UnicodeFrequencies empty;
  EXPECT_EQ(empty.CoveredCodepoints(), (CodepointSet{}));
}

}  // namespace ift::freq
