#include "ift/freq/unicode_frequencies.h"

#include "gtest/gtest.h"

namespace ift::freq {

TEST(UnicodeFrequenciesTest, ProbabilityFor_NoFrequencies) {
  UnicodeFrequencies freq;
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(1, 2), 0.0);
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(1), 0.0);
}

TEST(UnicodeFrequenciesTest, ProbabilityFor) {
  UnicodeFrequencies freq;
  freq.Add(1, 2, 10);
  freq.Add(3, 2, 20);
  freq.Add(1, 1, 5);

  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(1, 2), 10.0 / 20.0);
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(2, 1), 10.0 / 20.0);
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(2, 3), 20.0 / 20.0);
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(3, 2), 20.0 / 20.0);
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(1), 5.0 / 20.0);
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(1, 1), 5.0 / 20.0);
}

TEST(UnicodeFrequenciesTest, ProbabilityFor_MissingFrequency) {
  UnicodeFrequencies freq;
  freq.Add(1, 2, 10);
  freq.Add(3, 2, 20);

  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(1), 1.0 / 20.0);
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(1, 1), 1.0 / 20.0);
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(4, 5), 1.0 / 20.0);
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

}  // namespace ift::freq
