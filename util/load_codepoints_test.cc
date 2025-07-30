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

}  // namespace util
