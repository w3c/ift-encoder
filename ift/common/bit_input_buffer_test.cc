#include "ift/common/bit_input_buffer.h"

#include <bitset>
#include <string>

#include "gtest/gtest.h"
#include "ift/common/branch_factor.h"

namespace ift::common {

using std::bitset;
using std::string;

class BitInputBufferTest : public ::testing::Test {};

TEST_F(BitInputBufferTest, SingleByte2) {
  string in{0b00000100, 0b00001111};
  //        ^ d1 bf2 ^
  BitInputBuffer bin(in);
  EXPECT_EQ(BF2, bin.GetBranchFactor());
  EXPECT_EQ(1, bin.Depth());
  uint32_t out = UINT32_MAX;
  EXPECT_TRUE(bin.read(&out));
  EXPECT_EQ(out, 0b11);
  EXPECT_TRUE(bin.read(&out));
  EXPECT_EQ(out, 0b11);
  EXPECT_TRUE(bin.read(&out));
  EXPECT_EQ(out, 0b00);
  EXPECT_TRUE(bin.read(&out));
  EXPECT_EQ(out, 0b00);
  EXPECT_FALSE(bin.read(&out));
}

TEST_F(BitInputBufferTest, SingleByte4) {
  string in{0b00000101, 0b00001111};
  //        ^ d1 bf4 ^
  BitInputBuffer bin(in);
  EXPECT_EQ(BF4, bin.GetBranchFactor());
  EXPECT_EQ(1, bin.Depth());
  uint32_t out = UINT32_MAX;
  EXPECT_TRUE(bin.read(&out));
  EXPECT_EQ(out, 0xF);
  EXPECT_TRUE(bin.read(&out));
  EXPECT_EQ(out, 0x0);
  EXPECT_FALSE(bin.read(&out));
}

TEST_F(BitInputBufferTest, SingleRead8) {
  string in{0b00000110, 0x2F};
  //        ^ d1 bf8 ^
  BitInputBuffer bin(in);
  EXPECT_EQ(BF8, bin.GetBranchFactor());
  EXPECT_EQ(1, bin.Depth());
  uint32_t out = UINT32_MAX;
  EXPECT_TRUE(bin.read(&out));
  EXPECT_EQ(out, 0x2F);
  EXPECT_FALSE(bin.read(&out));
}

TEST_F(BitInputBufferTest, SingleRead32) {
  string in{0b00000111, 0x11, 0x22, 0x33, 0x44};
  //        ^ d1 bf32^
  BitInputBuffer bin(in);
  EXPECT_EQ(BF32, bin.GetBranchFactor());
  EXPECT_EQ(1, bin.Depth());
  uint32_t out = UINT32_MAX;
  EXPECT_TRUE(bin.read(&out));
  EXPECT_EQ(out, 0x44332211u);
  EXPECT_FALSE(bin.read(&out));
}

TEST_F(BitInputBufferTest, Empty) {
  uint32_t out;
  EXPECT_FALSE(BitInputBuffer("").read(&out));
  EXPECT_FALSE(BitInputBuffer(string{0x00}).read(&out));

  EXPECT_FALSE(BitInputBuffer(string{0x01}).read(&out));

  EXPECT_FALSE(BitInputBuffer(string{0x02}).read(&out));

  EXPECT_FALSE(BitInputBuffer(string{0x03}).read(&out));
  EXPECT_FALSE(BitInputBuffer(string{0x03, 0x01}).read(&out));
  EXPECT_FALSE(BitInputBuffer(string{0x03, 0x01, 0x01}).read(&out));
  EXPECT_FALSE(BitInputBuffer(string{0x03, 0x01, 0x01, 0x01}).read(&out));
}

TEST_F(BitInputBufferTest, Null) {
  EXPECT_FALSE(BitInputBuffer(string{0x00}).read(nullptr));
  EXPECT_FALSE(BitInputBuffer(string{0x01}).read(nullptr));
  EXPECT_FALSE(BitInputBuffer(string{0x02}).read(nullptr));
  EXPECT_FALSE(BitInputBuffer(string{0x03}).read(nullptr));
}

TEST_F(BitInputBufferTest, ReservedBitIgnored) {
  for (const string& s : {string{0b00000100}, string{(char)0b10000100}}) {
    BitInputBuffer bin(s);
    EXPECT_EQ(BF2, bin.GetBranchFactor());
    EXPECT_EQ(1, bin.Depth());
  }
  for (const string& s : {string{0b00000101}, string{(char)0b10000101}}) {
    BitInputBuffer bin(s);
    EXPECT_EQ(BF4, bin.GetBranchFactor());
    EXPECT_EQ(1, bin.Depth());
  }
  for (const string& s : {string{0b00000110}, string{(char)0b10000110}}) {
    BitInputBuffer bin(s);
    EXPECT_EQ(BF8, bin.GetBranchFactor());
    EXPECT_EQ(1, bin.Depth());
  }
  for (const string& s : {string{0b00000111}, string{(char)0b10000111}}) {
    BitInputBuffer bin(s);
    EXPECT_EQ(BF32, bin.GetBranchFactor());
    EXPECT_EQ(1, bin.Depth());
  }
}

TEST_F(BitInputBufferTest, RemainingBf2) {
  string in{0b00000100, 0b00001111, 0b00000000};
  //        ^ d1 bf2 ^

  string exp1{0b00001111, 0b00000000};
  string exp2{0b00000000};
  string exp3{};

  BitInputBuffer bin(in);
  EXPECT_EQ(bin.GetBranchFactor(), BF2);
  EXPECT_EQ(bin.Remaining(), exp1);

  uint32_t out = UINT32_MAX;
  EXPECT_TRUE(bin.read(&out));
  EXPECT_EQ(bin.Remaining(), exp2);

  EXPECT_TRUE(bin.read(&out));
  EXPECT_TRUE(bin.read(&out));
  EXPECT_TRUE(bin.read(&out));
  EXPECT_EQ(bin.Remaining(), exp2);

  EXPECT_TRUE(bin.read(&out));
  EXPECT_EQ(bin.Remaining(), exp3);
}

TEST_F(BitInputBufferTest, RemainingBf4) {
  string in{0b00000101, 0b00001111, 0b00000000};
  //        ^ d1 bf4 ^

  string exp1{0b00001111, 0b00000000};
  string exp2{0b00000000};
  string exp3{};

  BitInputBuffer bin(in);
  EXPECT_EQ(BF4, bin.GetBranchFactor());
  EXPECT_EQ(bin.Remaining(), exp1);

  uint32_t out = UINT32_MAX;
  EXPECT_TRUE(bin.read(&out));
  EXPECT_EQ(bin.Remaining(), exp2);

  EXPECT_TRUE(bin.read(&out));
  EXPECT_EQ(bin.Remaining(), exp2);

  EXPECT_TRUE(bin.read(&out));
  EXPECT_EQ(bin.Remaining(), exp3);

  EXPECT_TRUE(bin.read(&out));
  EXPECT_EQ(bin.Remaining(), exp3);
}

TEST_F(BitInputBufferTest, RemainingBf8) {
  string in{0b00000110, 0x2F, 0x3F};
  //        ^ d1 bf8 ^

  string exp1{0x2F, 0x3F};
  string exp2{0x3F};
  string exp3{};

  BitInputBuffer bin(in);
  EXPECT_EQ(BF8, bin.GetBranchFactor());
  EXPECT_EQ(bin.Remaining(), exp1);

  uint32_t out = UINT32_MAX;
  EXPECT_TRUE(bin.read(&out));
  EXPECT_EQ(bin.Remaining(), exp2);

  EXPECT_TRUE(bin.read(&out));
  EXPECT_EQ(bin.Remaining(), exp3);
}

TEST_F(BitInputBufferTest, RemainingBf32) {
  string in{0b00000111, 0x11, 0x22, 0x33,       0x44,
            0x55,       0x66, 0x77, (char)0x88, (char)0x99};
  //        ^ d1 bf32^

  string exp1{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, (char)0x88, (char)0x99};
  string exp2{0x55, 0x66, 0x77, (char)0x88, (char)0x99};
  string exp3{(char)0x99};

  BitInputBuffer bin(in);
  EXPECT_EQ(BF32, bin.GetBranchFactor());
  EXPECT_EQ(bin.Remaining(), exp1);

  uint32_t out = UINT32_MAX;
  EXPECT_TRUE(bin.read(&out));
  EXPECT_EQ(bin.Remaining(), exp2);

  EXPECT_TRUE(bin.read(&out));
  EXPECT_EQ(bin.Remaining(), exp3);
}

}  // namespace ift::common
