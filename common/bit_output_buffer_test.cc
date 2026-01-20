#include "common/bit_output_buffer.h"

#include <algorithm>
#include <bitset>
#include <string>

#include "gtest/gtest.h"

namespace common {

using std::bitset;
using std::string;

class BitOutputBufferTest : public ::testing::Test {
 protected:
  static string Bits(const string &s) {
    if (s.empty()) {
      return "";
    }
    string result;
    for (unsigned int i = 0; i < s.size(); i++) {
      string byte_bits = bitset<8>(s.c_str()[i]).to_string();
      std::reverse(byte_bits.begin(), byte_bits.end());
      result += byte_bits;
      result += " ";
    }
    return result.substr(0, result.size() - 1);
  }
};

TEST_F(BitOutputBufferTest, MultipleWrites2) {
  BitOutputBuffer buf(BranchFactor::BF2, 2);
  buf.append(0b1111u);
  buf.append(0b0000u);
  buf.append(0b1111u);
  buf.append(0b0000u);
  std::string out_bits = Bits(buf.to_string());
  EXPECT_EQ("00010000 11001100", out_bits);
  //         ^bf2 d2^
}

TEST_F(BitOutputBufferTest, MultipleWrites2b) {
  BitOutputBuffer buf(BranchFactor::BF2, 2);
  buf.append(0b0000u);
  buf.append(0b1111u);
  buf.append(0b0000u);
  buf.append(0b1111u);
  std::string out_bits = Bits(buf.to_string());
  EXPECT_EQ("00010000 00110011", out_bits);
  //         ^bf2 d2^
}

TEST_F(BitOutputBufferTest, MultipleWrites2c) {
  BitOutputBuffer buf(BranchFactor::BF2, 2);
  buf.append(0b0001u);
  buf.append(0b1111u);
  buf.append(0b0010u);
  buf.append(0b1111u);
  std::string out_bits = Bits(buf.to_string());
  EXPECT_EQ("00010000 10110111", out_bits);
  //         ^bf2 d2^
}

TEST_F(BitOutputBufferTest, SingleWrite4) {
  BitOutputBuffer buf(BranchFactor::BF4, 1);
  buf.append(0b111111111111u);
  std::string out_bits = Bits(buf.to_string());
  EXPECT_EQ("10100000 11110000", out_bits);
  //         ^bf4 d1^
}

TEST_F(BitOutputBufferTest, MultipleWrites4) {
  BitOutputBuffer buf(BranchFactor::BF4, 2);
  buf.append(0b1001u);
  buf.append(0b0110u);
  buf.append(0b1100u);
  buf.append(0b0011u);
  buf.append(0b0101u);
  std::string out_bits = Bits(buf.to_string());
  EXPECT_EQ("10010000 10010110 00111100 10100000", out_bits);
  //         ^bf4 d2^
}

TEST_F(BitOutputBufferTest, SingleWrite8) {
  BitOutputBuffer buf(BranchFactor::BF8, 3);
  buf.append(0b11111111111111111111u);
  std::string out_bits = Bits(buf.to_string());
  EXPECT_EQ("01110000 11111111", out_bits);
  //         ^bf8 d3^
}

TEST_F(BitOutputBufferTest, MultipleWrites8) {
  BitOutputBuffer buf(BranchFactor::BF8, 4);
  buf.append(0b11111111u);
  buf.append(0b00000000u);
  buf.append(0b11110000u);
  buf.append(0b00001111u);
  buf.append(0b10101010u);
  std::string out_bits = Bits(buf.to_string());
  EXPECT_EQ("01001000 11111111 00000000 00001111 11110000 01010101", out_bits);
  //         ^bf8 d4^
}

TEST_F(BitOutputBufferTest, SingleWrite32) {
  BitOutputBuffer buf(BranchFactor::BF32, 7);
  buf.append(0b11111111111111111111111111111111u);
  std::string out_bits = Bits(buf.to_string());
  EXPECT_EQ("11111000 11111111 11111111 11111111 11111111", out_bits);
  //         bf16 d7
}

TEST_F(BitOutputBufferTest, MultipleWrites32) {
  BitOutputBuffer buf(BranchFactor::BF32, 8);
  buf.append(0b11111111111111111111111111111111u);
  buf.append(0b00000000000000000000000000000000u);
  buf.append(0b11111111111111110000000000000000u);
  buf.append(0b11111111000000001111111100000000u);
  buf.append(0b11110000111000110010110000000000u);
  std::string out_bits = Bits(buf.to_string());
  EXPECT_EQ(
      "11000100"
      // bf32 d8
      " 11111111 11111111 11111111 11111111 "

      "00000000 00000000 00000000 00000000 "
      "00000000 00000000 11111111 11111111 "
      "00000000 11111111 00000000 11111111 "
      "00000000 00110100 11000111 00001111",
      out_bits);
}

TEST_F(BitOutputBufferTest, EmptyBuffer) {
  EXPECT_EQ("00000100",
            Bits(BitOutputBuffer(BranchFactor::BF2, 8).to_string()));
  EXPECT_EQ("10100100",
            Bits(BitOutputBuffer(BranchFactor::BF4, 9).to_string()));
  EXPECT_EQ("01010100",
            Bits(BitOutputBuffer(BranchFactor::BF8, 10).to_string()));
  EXPECT_EQ("11110100",
            Bits(BitOutputBuffer(BranchFactor::BF32, 11).to_string()));
}

}  // namespace common
