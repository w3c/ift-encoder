#include "util/load_codepoints.h"

#include <cstdint>

#include "gtest/gtest.h"

namespace util {

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

  std::vector<uint32_t> expected = {
      0x0000, 0x000D, 0x0020, 0x0021, 0x0028,  0x0029, 0x002C, 0x002E,
      0x0030, 0x0031, 0x0032, 0x0033, 0x0034,  0x0035, 0x0036, 0x0037,
      0x0038, 0x0039, 0x003A, 0x003B, 0x003F,  0x0041, 0x0042, 0x0043,
      0x0044, 0x0045, 0x0046, 0x0043, 0x10043, 0x0048, 0x0047};

  ASSERT_EQ(*result, expected);
}

// TODO test with bad formatted hex

}  // namespace util
