#include "util/load_codepoints.h"

#include <cstdint>

#include "gtest/gtest.h"

namespace util {

bool operator==(const CodepointAndFrequency& lhs,
                const CodepointAndFrequency& rhs) {
  return lhs.codepoint == rhs.codepoint && lhs.frequency == rhs.frequency;
}

class LoadCodepointsTest : public ::testing::Test {
 protected:
  LoadCodepointsTest() {}
};

TEST_F(LoadCodepointsTest, LoadCodepoints_NotFound) {
  auto result = util::LoadCodepointsOrdered("not/found.txt");
  ASSERT_TRUE(absl::IsNotFound(result.status()));
}

TEST_F(LoadCodepointsTest, LoadCodepoints_InvalidFormat) {
  auto result =
      util::LoadCodepointsOrdered("util/testdata/codepoints_invalid_1.txt");
  ASSERT_TRUE(absl::IsInvalidArgument(result.status()));
}

TEST_F(LoadCodepointsTest, LoadCodepoints_InvalidFormat_BadHex) {
  auto result =
      util::LoadCodepointsOrdered("util/testdata/codepoints_invalid_2.txt");
  ASSERT_TRUE(absl::IsInvalidArgument(result.status()));
}

TEST_F(LoadCodepointsTest, LoadCodepoints) {
  auto result = util::LoadCodepointsOrdered("util/testdata/codepoints.txt");
  ASSERT_TRUE(result.ok()) << result.status();

  std::vector<CodepointAndFrequency> expected = {
      {0x0000, std::nullopt}, {0x000D, std::nullopt},  {0x0020, std::nullopt},
      {0x0021, std::nullopt}, {0x0028, std::nullopt},  {0x0029, std::nullopt},
      {0x002C, std::nullopt}, {0x002E, std::nullopt},  {0x0030, std::nullopt},
      {0x0031, std::nullopt}, {0x0032, std::nullopt},  {0x0033, std::nullopt},
      {0x0034, std::nullopt}, {0x0035, std::nullopt},  {0x0036, std::nullopt},
      {0x0037, std::nullopt}, {0x0038, std::nullopt},  {0x0039, std::nullopt},
      {0x003A, std::nullopt}, {0x003B, std::nullopt},  {0x003F, std::nullopt},
      {0x0041, std::nullopt}, {0x0042, std::nullopt},  {0x0043, std::nullopt},
      {0x0044, std::nullopt}, {0x0045, std::nullopt},  {0x0046, std::nullopt},
      {0x0043, std::nullopt}, {0x10043, std::nullopt}, {0x0048, std::nullopt},
      {0x0047, std::nullopt}};

  ASSERT_EQ(*result, expected);
}

TEST_F(LoadCodepointsTest, LoadCodepointsWithFrequency) {
  auto result =
      util::LoadCodepointsOrdered("util/testdata/codepoints_with_freq.txt");
  ASSERT_TRUE(result.ok()) << result.status();

  std::vector<CodepointAndFrequency> expected = {
      {0x0041, 100}, {0x0042, 200},          {0x0043, 300},
      {0x0044, 400}, {0x0045, std::nullopt}, {0x0046, std::nullopt},
  };

  ASSERT_EQ(*result, expected);
}

TEST_F(LoadCodepointsTest, LoadCodepointsWithFrequency_Invalid) {
  auto result = util::LoadCodepointsOrdered(
      "util/testdata/codepoints_with_freq_invalid.txt");
  ASSERT_TRUE(absl::IsInvalidArgument(result.status())) << result.status();
}

// TODO test with bad formatted hex

TEST_F(LoadCodepointsTest, LoadFrequenciesFromRiegeli) {
  auto result =
      util::LoadFrequenciesFromRiegeli("util/testdata/test_freq_data.riegeli");
  ASSERT_TRUE(result.ok()) << result.status();

  EXPECT_EQ(result->ProbabilityFor(0x43, 0x43), 1.0);
  EXPECT_EQ(result->ProbabilityFor(0x44, 0x44), 75.0 / 200.0);

  EXPECT_EQ(result->ProbabilityFor(0x41, 0x42), 0.5);
  EXPECT_EQ(result->ProbabilityFor(0x44, 0x45), 0.25);
}

TEST_F(LoadCodepointsTest, LoadFrequenciesFromRiegeli_Sharded) {
  auto result = util::LoadFrequenciesFromRiegeli(
      "util/testdata/sharded/test_freq_data.riegeli@*");
  ASSERT_TRUE(result.ok()) << result.status();

  EXPECT_EQ(result->ProbabilityFor(0x43, 0x43), 1.0);
  EXPECT_EQ(result->ProbabilityFor(0x44, 0x44), 75.0 / 200.0);

  EXPECT_EQ(result->ProbabilityFor(0x41, 0x42), 0.5);
  EXPECT_EQ(result->ProbabilityFor(0x44, 0x45), 0.25);
}

TEST_F(LoadCodepointsTest, LoadFrequenciesFromRiegeli_Sharded_DoesNotExist) {
  auto result = util::LoadFrequenciesFromRiegeli(
      "util/testdata/sharded/notfound.riegeli@*");
  ASSERT_TRUE(absl::IsNotFound(result.status())) << result.status();
}

TEST_F(LoadCodepointsTest, LoadFrequenciesFromRiegeli_BadData) {
  auto result = util::LoadFrequenciesFromRiegeli(
      "util/testdata/invalid_test_freq_data.riegeli");
  ASSERT_TRUE(absl::IsInvalidArgument(result.status())) << result.status();
}

TEST_F(LoadCodepointsTest, ExpandShardedPath) {
  auto result = ExpandShardedPath("util/testdata/test_freq_data.riegeli");
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(*result,
            (std::vector<std::string>{"util/testdata/test_freq_data.riegeli"}));

  result = ExpandShardedPath("util/testdata/test_freq_data.riegeli@*");
  ASSERT_TRUE(absl::IsNotFound(result.status())) << result.status();

  result = ExpandShardedPath("util/testdata/sharded/BadSuffix@*");
  ASSERT_TRUE(absl::IsNotFound(result.status())) << result.status();

  result = ExpandShardedPath("does/not/exist.file@*");
  ASSERT_TRUE(absl::IsNotFound(result.status())) << result.status();

  result = ExpandShardedPath("util/testdata/sharded/notfound.file@*");
  ASSERT_TRUE(absl::IsNotFound(result.status())) << result.status();

  result = ExpandShardedPath("does/not/exist.file");
  ASSERT_TRUE(absl::IsNotFound(result.status())) << result.status();

  result = ExpandShardedPath("util/testdata/sharded/Language_ja.riegeli@*");
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(*result,
            (std::vector<std::string>{
                "util/testdata/sharded/Language_ja.riegeli-00000-of-00003",
                "util/testdata/sharded/Language_ja.riegeli-00001-of-00003",
                "util/testdata/sharded/Language_ja.riegeli-00002-of-00003",
            }));

  result = ExpandShardedPath("util/testdata/sharded/Language_ko.riegeli@*");
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_EQ(*result,
            (std::vector<std::string>{
                "util/testdata/sharded/Language_ko.riegeli-00000-of-00100",
                "util/testdata/sharded/Language_ko.riegeli-00008-of-00100",
                "util/testdata/sharded/Language_ko.riegeli-00011-of-00100",
                "util/testdata/sharded/Language_ko.riegeli-00013-of-00100",
                "util/testdata/sharded/Language_ko.riegeli-00020-of-00100",
            }));

  result = ExpandShardedPath("util/testdata/sharded/Language_ja.riegeli");
  ASSERT_TRUE(absl::IsNotFound(result.status())) << result.status();
}

TEST_F(LoadCodepointsTest, LoadBuiltInFrequencies) {
  auto result = util::LoadBuiltInFrequencies("Script_latin.riegeli");
  ASSERT_TRUE(result.ok()) << result.status();

  EXPECT_EQ(result->ProbabilityFor(0x20, 0x20), 1.0);
  EXPECT_LT(result->ProbabilityFor(0x20, 'Z'), 1.0);
  EXPECT_EQ(result->CoveredCodepoints().size(), 1363);
}

}  // namespace util
