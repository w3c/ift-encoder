#include <vector>

#include "gtest/gtest.h"
#include "ift/common/bit_input_buffer.h"
#include "ift/common/bit_output_buffer.h"

namespace ift::common {

using std::string;
using std::vector;

class BitBufferTest : public ::testing::Test {
 protected:
  static void check_transcode(const vector<uint32_t> &input, BranchFactor bf,
                              unsigned int depth) {
    BitOutputBuffer bout(bf, depth);
    for (uint32_t value : input) {
      bout.append(value);
    }
    string bits = bout.to_string();
    BitInputBuffer bin(bits);
    vector<uint32_t> results;
    uint32_t out;
    bool ok;
    do {
      ok = bin.read(&out);
      if (ok) {
        results.push_back(out);
      }
    } while (ok);
    EXPECT_TRUE(results == input);
    EXPECT_EQ(depth, bin.Depth());
    EXPECT_EQ(bf, bin.GetBranchFactor());
  }
};

TEST_F(BitBufferTest, Transcode2) {
  // Min string length is 1 byte, which is 4 2-bit values.
  check_transcode({0, 0, 0, 0}, BF2, 1);
  check_transcode({0, 1, 0, 0}, BF2, 2);
}

TEST_F(BitBufferTest, Transcode4) {
  // Min string length is 1 bytes, which is 2 4-bit values.
  check_transcode({0, 0}, BF4, 1);
  check_transcode({0, 1, 2, 3}, BF4, 2);
}

TEST_F(BitBufferTest, Transcode8) {
  check_transcode({0, 0}, BF8, 1);
  check_transcode({0, 1, 2, 3}, BF8, 2);
  check_transcode({255, 254, 129, 128, 127, 65, 64, 63, 33, 32, 31, 17,
                   16,  15,  9,   8,   7,   5,  4,  3,  2,  1,  0},
                  BF8, 3);
}

TEST_F(BitBufferTest, Transcode32) {
  check_transcode({0, 0}, BF32, 1);
  check_transcode({0, 1, 2, 3}, BF32, 2);
  check_transcode({255, 254, 129, 128, 127, 65, 64, 63, 33, 32, 31, 17,
                   16,  15,  9,   8,   7,   5,  4,  3,  2,  1,  0},
                  BF32, 3);
  check_transcode({0xFFFF, 0xFFFE, 0xFF00, 0x0F0F, 0x00FF, 0x000F, 0x0000},
                  BF32, 4);
  check_transcode({0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFF00, 0xFFFF0000, 0xFFEEDDCC},
                  BF32, 5);
}

}  // namespace ift::common
