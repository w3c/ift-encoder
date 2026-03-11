#include "brotli/brotli_stream.h"

#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "ift/common/brotli_binary_patch.h"

namespace brotli {

using absl::Span;
using absl::Status;
using ift::common::BrotliBinaryPatch;
using ift::common::FontData;

class BrotliStreamTest : public ::testing::Test {
 protected:
  BrotliStreamTest() {}

  ~BrotliStreamTest() override {}

  void SetUp() override {}
};

void CheckDecompressesTo(
    const BrotliStream& stream, Span<const uint8_t> expected,
    Span<const uint8_t> dict_data = Span<const uint8_t>()) {
  BrotliBinaryPatch patcher;
  FontData dict;
  dict.copy((const char*)dict_data.data(), dict_data.size());
  FontData patch;
  patch.copy((const char*)stream.compressed_data().data(),
             stream.compressed_data().size());

  Span<const char> expected_char((const char*)expected.data(), expected.size());

  FontData uncompressed;
  ASSERT_EQ(patcher.Patch(dict, patch, &uncompressed), absl::OkStatus());
  EXPECT_EQ(Span<const char>(uncompressed), expected_char);
}

TEST_F(BrotliStreamTest, InsertCompressed) {
  BrotliStream stream(22);
  uint8_t data[] = {'H', 'e', 'l', 'l', 'o', ' ', 'H', 'e', 'l', 'l', 'o', ' ',
                    'H', 'e', 'l', 'l', 'o', ' ', 'H', 'e', 'l', 'l', 'o', ' '};
  ASSERT_TRUE(stream.insert_compressed(data).ok());
  stream.end_stream();

  EXPECT_LT(stream.compressed_data().size(), 24);
  CheckDecompressesTo(stream, data);
}

TEST_F(BrotliStreamTest, InsertCompressedWithDict) {
  BrotliStream stream(22, 100);
  uint8_t data[] = {'H', 'e', 'l', 'l', 'o', ' ', 'H', 'e', 'l', 'l',
                    'o', ' ', 'H', 'e', 'l', 'l', 'o', ' ', 'H', 'e',
                    'l', 'l', 'o', ' ', 'H', 'e', 'l', 'l', 'o', ' '};
  ASSERT_TRUE(stream.insert_compressed(data).ok());
  stream.end_stream();

  std::vector<uint8_t> dict;
  dict.resize(100);
  EXPECT_LT(stream.compressed_data().size(), 30);
  CheckDecompressesTo(stream, data, dict);
}

TEST_F(BrotliStreamTest, InsertCompressedWithPartialDict) {
  std::vector<uint8_t> dict;
  for (unsigned i = 0; i < 500; i++) {
    dict.push_back(i % 256);
  }

  Span<const uint8_t> data = Span<const uint8_t>(dict).subspan(5, 100);
  BrotliStream stream(22, dict.size());
  ASSERT_TRUE(stream
                  .insert_compressed_with_partial_dict(
                      data, Span<const uint8_t>(dict).subspan(0, 200))
                  .ok());
  stream.end_stream();

  EXPECT_LT(stream.compressed_data().size(), 100);
  CheckDecompressesTo(stream, data, dict);
}

TEST_F(BrotliStreamTest, InsertCompressedWithPartialDict_ExceedsWindow) {
  std::vector<uint8_t> dict;
  dict.resize(2000);

  Span<const uint8_t> data = Span<const uint8_t>(dict).subspan(1, 10);

  BrotliStream stream(10, 2000);
  EXPECT_TRUE(absl::IsInternal(stream.insert_compressed_with_partial_dict(
      data, Span<const uint8_t>(dict).subspan(0, 10))));
  EXPECT_EQ(stream.uncompressed_size(), 0);
}

TEST_F(BrotliStreamTest, InsertMultipleCompressedWithPartialDict) {
  std::vector<uint8_t> dict;
  for (unsigned i = 0; i < 500; i++) {
    dict.push_back(i % 256);
  }

  Span<const uint8_t> data = Span<const uint8_t>(dict).subspan(5, 150);
  BrotliStream stream(22, dict.size());
  ASSERT_TRUE(
      stream
          .insert_compressed_with_partial_dict(
              data.subspan(0, 75), Span<const uint8_t>(dict).subspan(0, 100))
          .ok());
  ASSERT_TRUE(
      stream
          .insert_compressed_with_partial_dict(
              data.subspan(75, 75), Span<const uint8_t>(dict).subspan(0, 200))
          .ok());
  stream.end_stream();

  EXPECT_LT(stream.compressed_data().size(), 100);
  CheckDecompressesTo(stream, data, dict);
}

TEST_F(BrotliStreamTest, InsertUncompressed) {
  BrotliStream stream(22);
  uint8_t data[] = {'H', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd'};
  stream.insert_uncompressed(data);
  stream.end_stream();

  CheckDecompressesTo(stream, data);
}

TEST_F(BrotliStreamTest, InsertUncompressedMultiple) {
  BrotliStream stream(22);
  uint8_t data_1[] = {'H', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd'};
  uint8_t data_2[] = {'t', 'e', 's', 't'};
  stream.insert_uncompressed(data_1);
  stream.insert_uncompressed(data_2);
  stream.end_stream();

  CheckDecompressesTo(stream, {'H', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l',
                               'd', 't', 'e', 's', 't'});
}

TEST_F(BrotliStreamTest, InsertUncompressedLarge) {
  BrotliStream stream(22);
  std::vector<uint8_t> data;
  data.resize(26777216);
  data[100] = 123;
  data[25000000] = 45;
  stream.insert_uncompressed(data);
  stream.end_stream();

  CheckDecompressesTo(stream, data);
}

TEST_F(BrotliStreamTest, InsertFromDictionary) {
  BrotliStream stream(22, 11);
  uint8_t dict_data[] = {'H', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd'};

  EXPECT_TRUE(stream.insert_from_dictionary(1, 4));
  EXPECT_TRUE(stream.insert_from_dictionary(6, 3));
  stream.end_stream();

  uint8_t expected[] = {'e', 'l', 'l', 'o', 'w', 'o', 'r'};
  CheckDecompressesTo(stream, expected, dict_data);
}

TEST_F(BrotliStreamTest, InsertFromDictionarySmall) {
  BrotliStream stream(22, 10);
  EXPECT_FALSE(stream.insert_from_dictionary(1, 1));
  EXPECT_EQ(stream.uncompressed_size(), 0);
}

TEST_F(BrotliStreamTest, InsertFromDictionaryLarge) {
  std::vector<uint8_t> dict;
  dict.resize(25000000);
  dict[100] = 123;
  dict[23000000] = 45;

  BrotliStream stream(22, dict.size());

  EXPECT_TRUE(stream.insert_from_dictionary(50, 24000000));
  stream.end_stream();

  CheckDecompressesTo(stream, Span<const uint8_t>(dict).subspan(50, 24000000),
                      dict);
}

TEST_F(BrotliStreamTest, InsertFromDictionaryLarge_AvoidsTooSmallRef) {
  std::vector<uint8_t> dict;
  dict.resize(25000000);
  dict[100] = 123;
  dict[23000000] = 45;

  BrotliStream stream(22, dict.size());

  EXPECT_TRUE(stream.insert_from_dictionary(50, (1 << 24) + 1));
  stream.end_stream();

  CheckDecompressesTo(
      stream, Span<const uint8_t>(dict).subspan(50, (1 << 24) + 1), dict);
}

TEST_F(BrotliStreamTest, InsertMixed) {
  BrotliStream stream(22, 11);
  uint8_t dict_data[] = {'H', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd'};
  uint8_t data1[] = {'1', '2', '3'};
  uint8_t data2[] = {'6', '7', '8', '9'};

  EXPECT_TRUE(stream.insert_from_dictionary(1, 4));
  stream.insert_uncompressed(data1);
  EXPECT_TRUE(stream.insert_from_dictionary(6, 3));
  EXPECT_TRUE(stream.insert_compressed(data2).ok());
  EXPECT_TRUE(stream.insert_from_dictionary(0, 2));
  stream.end_stream();

  uint8_t expected[] = {'e', 'l', 'l', 'o', '1', '2', '3', 'w',
                        'o', 'r', '6', '7', '8', '9', 'H', 'e'};
  CheckDecompressesTo(stream, expected, dict_data);
}

TEST_F(BrotliStreamTest, AppendStreams) {
  uint8_t dict[] = {'H', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd'};

  BrotliStream a(22, 11);
  BrotliStream b(22, 11, 5);
  BrotliStream c(22, 11, 12);

  EXPECT_TRUE(a.insert_from_dictionary(1, 5));
  EXPECT_TRUE(b.insert_from_dictionary(3, 7));
  EXPECT_TRUE(c.insert_from_dictionary(5, 4));

  a.append(b);
  a.append(c);
  a.end_stream();

  EXPECT_EQ(a.uncompressed_size(), 5 + 7 + 4);

  uint8_t expected[] = {'e', 'l', 'l', 'o', ' ', 'l', 'o', ' ',
                        'w', 'o', 'r', 'l', ' ', 'w', 'o', 'r'};
  CheckDecompressesTo(a, expected, dict);
}

TEST_F(BrotliStreamTest, FourByteAlign) {
  uint8_t dict[] = {'1', '2', '3', '4'};
  BrotliStream stream(22, 4);

  stream.four_byte_align_uncompressed();
  EXPECT_EQ(stream.uncompressed_size(), 0);

  EXPECT_TRUE(stream.insert_from_dictionary(0, 2));
  stream.four_byte_align_uncompressed();
  EXPECT_EQ(stream.uncompressed_size(), 4);
  stream.four_byte_align_uncompressed();
  EXPECT_EQ(stream.uncompressed_size(), 4);

  stream.end_stream();

  uint8_t expected[] = {'1', '2', 0, 0};
  CheckDecompressesTo(stream, expected, dict);
}

}  // namespace brotli
