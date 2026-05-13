#include "ift/freq/unicode_frequencies.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "ift/common/int_set.h"

using testing::Not;
using testing::DoubleEq;
using ift::common::CodepointSet;

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
  UnicodeFrequenciesBuilder builder;

  builder.Add(2, 3, 20);
  EXPECT_DOUBLE_EQ(builder.Build().ProbabilityFor(2, 3), 20.0 / 20.0);

  builder.Add(1, 2, 10);
  EXPECT_DOUBLE_EQ(builder.Build().ProbabilityFor(1, 2), 10.0 / 20.0);

  builder.Add(2, 1, 15);
  EXPECT_DOUBLE_EQ(builder.Build().ProbabilityFor(1, 2), 1.0);
  EXPECT_DOUBLE_EQ(builder.Build().ProbabilityFor(2, 3), 20.0 / 25.0);
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

TEST(UnicodeFrequenciesTest, FilteredBuilder) {
  CodepointSet filter{1, 2};
  UnicodeFrequenciesBuilder builder(filter);
  UnicodeFrequenciesBuilder unfiltered_builder;

  builder.Add(1, 2, 10); // Kept
  unfiltered_builder.Add(1, 2, 10);

  builder.Add(2, 3, 20); // Ignored (3 not in filter)
  unfiltered_builder.Add(2, 3, 20);

  builder.Add(1, 1, 5);  // Kept
  unfiltered_builder.Add(1, 1, 5);

  UnicodeFrequencies freq = builder.Build();
  UnicodeFrequencies unfiltered_freq = unfiltered_builder.Build();

  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(1, 2), unfiltered_freq.ProbabilityFor(1, 2));
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(1), unfiltered_freq.ProbabilityFor(1));
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(2), unfiltered_freq.ProbabilityFor(2));
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(3), unfiltered_freq.ProbabilityFor(3));

  // (2, 3) is filtered out and should equal the default probability
  EXPECT_THAT(freq.ProbabilityFor(2, 3), Not(DoubleEq(unfiltered_freq.ProbabilityFor(2, 3))));
  EXPECT_DOUBLE_EQ(freq.ProbabilityFor(2, 3), unfiltered_freq.ProbabilityFor(100, 200));
}

}  // namespace ift::freq
