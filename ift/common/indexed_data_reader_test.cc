#include "ift/common/indexed_data_reader.h"

#include <cstdint>

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

using absl::string_view;

namespace ift::common {

class IndexedDataReaderTest : public ::testing::Test {
 protected:
  IndexedDataReaderTest()
      : short_reader(string_view((const char*)short_index, 10), data),
        wide_reader(string_view((const char*)wide_index, 20), data) {}

  uint8_t short_index[10] = {
      0x00, 0x00,  // 0
      0x00, 0x03,  // 1
      0x00, 0x07,  // 2
      0x00, 0x07,  // 3
      0x00, 0x0a,  // 4
  };
  uint8_t wide_index[20] = {
      0x00, 0x00, 0x00, 0x00,  // 0
      0x00, 0x00, 0x00, 0x06,  // 1
      0x00, 0x00, 0x00, 0x0e,  // 2
      0x00, 0x00, 0x00, 0x0e,  // 3
      0x00, 0x00, 0x00, 0x14,  // 4
  };
  uint8_t bad_index[10] = {
      0x00, 0x00,  // 0
      0x00, 0x07,  // 1
      0x00, 0x03,  // 2
      0x00, 0x07,  // 3
      0x00, 0x0a,  // 4
  };
  const char* data = "00010203040506070809";

  IndexedDataReader<uint16_t, 2> short_reader;
  IndexedDataReader<uint32_t, 1> wide_reader;
};

TEST_F(IndexedDataReaderTest, ShortRead) {
  auto data = short_reader.DataFor(0);
  ASSERT_TRUE(data.ok()) << data.status();
  ASSERT_EQ(data->size(), 6);
  ASSERT_EQ(*data, "000102");

  data = short_reader.DataFor(1);
  ASSERT_TRUE(data.ok()) << data.status();
  ASSERT_EQ(data->size(), 8);
  ASSERT_EQ(*data, "03040506");

  data = short_reader.DataFor(2);
  ASSERT_TRUE(data.ok()) << data.status();
  ASSERT_EQ(data->size(), 0);

  data = short_reader.DataFor(3);
  ASSERT_TRUE(data.ok()) << data.status();
  ASSERT_EQ(data->size(), 6);
  ASSERT_EQ(*data, "070809");

  data = short_reader.DataFor(4);
  ASSERT_TRUE(absl::IsNotFound(data.status())) << data.status();
}

TEST_F(IndexedDataReaderTest, LargeOffset) {
  uint8_t index[4] = {
      0x00,
      0x00,
      0x01,
      0xFa,
  };
  std::string data;
  data.resize(506);

  IndexedDataReader<uint16_t, 1> reader(string_view((const char*)index, 4),
                                        data);
  auto d = reader.DataFor(0);
  ASSERT_TRUE(d.ok()) << d.status();
  ASSERT_EQ(d->size(), 506);
}

TEST_F(IndexedDataReaderTest, WideRead) {
  auto data = wide_reader.DataFor(0);
  ASSERT_TRUE(data.ok()) << data.status();
  ASSERT_EQ(data->size(), 6);
  ASSERT_EQ(*data, "000102");

  data = wide_reader.DataFor(1);
  ASSERT_TRUE(data.ok()) << data.status();
  ASSERT_EQ(data->size(), 8);
  ASSERT_EQ(*data, "03040506");

  data = wide_reader.DataFor(2);
  ASSERT_TRUE(data.ok()) << data.status();
  ASSERT_EQ(data->size(), 0);

  data = wide_reader.DataFor(3);
  ASSERT_TRUE(data.ok()) << data.status();
  ASSERT_EQ(data->size(), 6);
  ASSERT_EQ(*data, "070809");

  data = wide_reader.DataFor(4);
  ASSERT_TRUE(absl::IsNotFound(data.status())) << data.status();
}

TEST_F(IndexedDataReaderTest, BadData) {
  IndexedDataReader<uint16_t, 2> reader(
      string_view((const char*)short_index, 10), string_view(data, 7));

  auto data = reader.DataFor(1);
  ASSERT_TRUE(absl::IsInvalidArgument(data.status())) << data.status();
}

TEST_F(IndexedDataReaderTest, BadIndex) {
  IndexedDataReader<uint16_t, 2> reader(string_view((const char*)bad_index, 10),
                                        data);

  auto data = reader.DataFor(1);
  ASSERT_TRUE(absl::IsInvalidArgument(data.status())) << data.status();
}

}  // namespace ift::common