#include "common/font_helper.h"

#include <cstdint>

#include "absl/container/flat_hash_map.h"
#include "common/font_data.h"
#include "gtest/gtest.h"
#include "hb-subset.h"

using absl::flat_hash_map;
using absl::string_view;

namespace common {

const uint8_t roboto_glyf_gid91_w[] = {
    0x00, 0x01, 0x00, 0x2b, 0x00, 0x00, 0x05, 0xd3, 0x04, 0x3a, 0x00, 0x0c,
    0x00, 0x60, 0xb2, 0x05, 0x0d, 0x0e, 0x11, 0x12, 0x39, 0x00, 0xb0, 0x00,
    0x45, 0x58, 0xb0, 0x01, 0x2f, 0x1b, 0xb1, 0x01, 0x1a, 0x3e, 0x59, 0xb0,
    0x00, 0x45, 0x58, 0xb0, 0x08, 0x2f, 0x1b, 0xb1, 0x08, 0x1a, 0x3e, 0x59,
    0xb0, 0x00, 0x45, 0x58, 0xb0, 0x0b, 0x2f, 0x1b, 0xb1, 0x0b, 0x1a, 0x3e,
    0x59, 0xb0, 0x00, 0x45, 0x58, 0xb0, 0x03, 0x2f, 0x1b, 0xb1, 0x03, 0x12,
    0x3e, 0x59, 0xb0, 0x00, 0x45, 0x58, 0xb0, 0x06, 0x2f, 0x1b, 0xb1, 0x06,
    0x12, 0x3e, 0x59, 0xb2, 0x00, 0x0b, 0x03, 0x11, 0x12, 0x39, 0xb2, 0x05,
    0x0b, 0x03, 0x11, 0x12, 0x39, 0xb2, 0x0a, 0x0b, 0x03, 0x11, 0x12, 0x39,
    0x30, 0x31, 0x25, 0x13, 0x33, 0x01, 0x23, 0x01, 0x01, 0x23, 0x01, 0x33,
    0x13, 0x13, 0x33, 0x04, 0x4a, 0xd0, 0xb9, 0xfe, 0xc5, 0x96, 0xfe, 0xf9,
    0xff, 0x00, 0x96, 0xfe, 0xc6, 0xb8, 0xd5, 0xfc, 0x95, 0xff, 0x03, 0x3b,
    0xfb, 0xc6, 0x03, 0x34, 0xfc, 0xcc, 0x04, 0x3a, 0xfc, 0xd6, 0x03, 0x2a};

const uint8_t roboto_glyf_gid73_e[] = {
    0x00, 0x02, 0x00, 0x5d, 0xff, 0xec, 0x03, 0xf3, 0x04, 0x4e, 0x00, 0x15,
    0x00, 0x1d, 0x00, 0x6c, 0xb2, 0x08, 0x1e, 0x1f, 0x11, 0x12, 0x39, 0xb0,
    0x08, 0x10, 0xb0, 0x16, 0xd0, 0x00, 0xb0, 0x00, 0x45, 0x58, 0xb0, 0x08,
    0x2f, 0x1b, 0xb1, 0x08, 0x1a, 0x3e, 0x59, 0xb0, 0x00, 0x45, 0x58, 0xb0,
    0x00, 0x2f, 0x1b, 0xb1, 0x00, 0x12, 0x3e, 0x59, 0xb2, 0x1a, 0x08, 0x00,
    0x11, 0x12, 0x39, 0xb0, 0x1a, 0x2f, 0xb4, 0xbf, 0x1a, 0xcf, 0x1a, 0x02,
    0x5d, 0xb1, 0x0c, 0x01, 0xb0, 0x0a, 0x2b, 0x58, 0x21, 0xd8, 0x1b, 0xf4,
    0x59, 0xb0, 0x00, 0x10, 0xb1, 0x10, 0x01, 0xb0, 0x0a, 0x2b, 0x58, 0x21,
    0xd8, 0x1b, 0xf4, 0x59, 0xb2, 0x13, 0x08, 0x00, 0x11, 0x12, 0x39, 0xb0,
    0x08, 0x10, 0xb1, 0x16, 0x01, 0xb0, 0x0a, 0x2b, 0x58, 0x21, 0xd8, 0x1b,
    0xf4, 0x59, 0x30, 0x31, 0x05, 0x22, 0x00, 0x35, 0x35, 0x34, 0x36, 0x36,
    0x33, 0x32, 0x12, 0x11, 0x15, 0x21, 0x16, 0x16, 0x33, 0x32, 0x36, 0x37,
    0x17, 0x06, 0x01, 0x22, 0x06, 0x07, 0x21, 0x35, 0x26, 0x26, 0x02, 0x4d,
    0xdc, 0xfe, 0xec, 0x7b, 0xdd, 0x81, 0xd3, 0xea, 0xfd, 0x23, 0x04, 0xb3,
    0x8a, 0x62, 0x88, 0x33, 0x71, 0x88, 0xfe, 0xd9, 0x70, 0x98, 0x12, 0x02,
    0x1e, 0x08, 0x88, 0x14, 0x01, 0x21, 0xf2, 0x22, 0xa1, 0xfd, 0x8f, 0xfe,
    0xea, 0xfe, 0xfd, 0x4d, 0xa0, 0xc5, 0x50, 0x42, 0x58, 0xd1, 0x03, 0xca,
    0xa3, 0x93, 0x0e, 0x8d, 0x9b, 0x00,

};

const uint8_t roboto_glyf_gid37_A[] = {
    0x00, 0x02, 0x00, 0x1c, 0x00, 0x00, 0x05, 0x1d, 0x05, 0xb0, 0x00, 0x07,
    0x00, 0x0a, 0x00, 0x54, 0xb2, 0x0a, 0x0b, 0x0c, 0x11, 0x12, 0x39, 0xb0,
    0x0a, 0x10, 0xb0, 0x04, 0xd0, 0x00, 0xb0, 0x00, 0x45, 0x58, 0xb0, 0x04,
    0x2f, 0x1b, 0xb1, 0x04, 0x1e, 0x3e, 0x59, 0xb0, 0x00, 0x45, 0x58, 0xb0,
    0x02, 0x2f, 0x1b, 0xb1, 0x02, 0x12, 0x3e, 0x59, 0xb0, 0x00, 0x45, 0x58,
    0xb0, 0x06, 0x2f, 0x1b, 0xb1, 0x06, 0x12, 0x3e, 0x59, 0xb2, 0x08, 0x04,
    0x02, 0x11, 0x12, 0x39, 0xb0, 0x08, 0x2f, 0xb1, 0x00, 0x01, 0xb0, 0x0a,
    0x2b, 0x58, 0x21, 0xd8, 0x1b, 0xf4, 0x59, 0xb2, 0x0a, 0x04, 0x02, 0x11,
    0x12, 0x39, 0x30, 0x31, 0x01, 0x21, 0x03, 0x23, 0x01, 0x33, 0x01, 0x23,
    0x01, 0x21, 0x03, 0x03, 0xcd, 0xfd, 0x9e, 0x89, 0xc6, 0x02, 0x2c, 0xa8,
    0x02, 0x2d, 0xc5, 0xfd, 0x4d, 0x01, 0xef, 0xf8, 0x01, 0x7c, 0xfe, 0x84,
    0x05, 0xb0, 0xfa, 0x50, 0x02, 0x1a, 0x02, 0xa9,
};

class FontHelperTest : public ::testing::Test {
 protected:
  FontHelperTest()
      : noto_sans_jp_otf(make_hb_face(nullptr)),
        noto_sans_ift_ttf(make_hb_face(nullptr)),
        roboto_ab(make_hb_face(nullptr)),
        roboto_Awesome(make_hb_face(nullptr)),
        roboto(make_hb_face(nullptr)),
        roboto_vf(make_hb_face(nullptr)),
        roboto_vf_abcd(make_hb_face(nullptr)) {
    hb_blob_unique_ptr blob = make_hb_blob(
        hb_blob_create_from_file("common/testdata/Roboto-Regular.ab.ttf"));
    roboto_ab = make_hb_face(hb_face_create(blob.get(), 0));

    blob = make_hb_blob(
        hb_blob_create_from_file("common/testdata/Roboto-Regular.Awesome.ttf"));
    roboto_Awesome = make_hb_face(hb_face_create(blob.get(), 0));

    blob = make_hb_blob(
        hb_blob_create_from_file("common/testdata/Roboto-Regular.ttf"));
    roboto = make_hb_face(hb_face_create(blob.get(), 0));

    blob = make_hb_blob(
        hb_blob_create_from_file("common/testdata/Roboto[wdth,wght].ttf"));
    roboto_vf = make_hb_face(hb_face_create(blob.get(), 0));

    blob = make_hb_blob(
        hb_blob_create_from_file("common/testdata/Roboto[wdth,wght].abcd.ttf"));
    roboto_vf_abcd = make_hb_face(hb_face_create(blob.get(), 0));

    blob = make_hb_blob(
        hb_blob_create_from_file("common/testdata/NotoSansJP-Regular.otf"));
    noto_sans_jp_otf = make_hb_face(hb_face_create(blob.get(), 0));

    blob = make_hb_blob(
        hb_blob_create_from_file("ift/testdata/NotoSansJP-Regular.ift.ttf"));
    noto_sans_ift_ttf = make_hb_face(hb_face_create(blob.get(), 0));
  }

  hb_face_unique_ptr noto_sans_jp_otf;
  hb_face_unique_ptr noto_sans_ift_ttf;
  hb_face_unique_ptr roboto_ab;
  hb_face_unique_ptr roboto_Awesome;
  hb_face_unique_ptr roboto;
  hb_face_unique_ptr roboto_vf;
  hb_face_unique_ptr roboto_vf_abcd;
};

TEST_F(FontHelperTest, WillUIntOverflow) {
  ASSERT_FALSE(FontHelper::WillIntOverflow<uint8_t>(0));
  ASSERT_FALSE(FontHelper::WillIntOverflow<uint8_t>(199));
  ASSERT_FALSE(FontHelper::WillIntOverflow<uint8_t>(0xFF));
  ASSERT_TRUE(FontHelper::WillIntOverflow<uint8_t>(0x100));
  ASSERT_TRUE(FontHelper::WillIntOverflow<uint8_t>(123959));
  ASSERT_TRUE(FontHelper::WillIntOverflow<uint8_t>(-1));

  ASSERT_FALSE(FontHelper::WillIntOverflow<uint16_t>(0));
  ASSERT_FALSE(FontHelper::WillIntOverflow<uint16_t>(1234));
  ASSERT_FALSE(FontHelper::WillIntOverflow<uint16_t>(0xFFFF));
  ASSERT_TRUE(FontHelper::WillIntOverflow<uint16_t>(0x10000));
  ASSERT_TRUE(FontHelper::WillIntOverflow<uint16_t>(-1));

  ASSERT_FALSE(FontHelper::WillIntOverflow<uint32_t>(0));
  ASSERT_FALSE(FontHelper::WillIntOverflow<uint32_t>(1234567));
  ASSERT_FALSE(FontHelper::WillIntOverflow<uint32_t>(0xFFFFFFFF));
  ASSERT_TRUE(FontHelper::WillIntOverflow<uint32_t>(0x100000000));
  ASSERT_TRUE(FontHelper::WillIntOverflow<uint32_t>(-1));

  ASSERT_FALSE(FontHelper::WillIntOverflow<int16_t>(-1234));
  ASSERT_FALSE(FontHelper::WillIntOverflow<int16_t>(1234));
  ASSERT_FALSE(FontHelper::WillIntOverflow<int16_t>(-32768));
  ASSERT_FALSE(FontHelper::WillIntOverflow<int16_t>(32767));
  ASSERT_TRUE(FontHelper::WillIntOverflow<int16_t>(-32769));
  ASSERT_TRUE(FontHelper::WillIntOverflow<int16_t>(32768));
}

TEST_F(FontHelperTest, ReadUInt8) {
  uint8_t input1[] = {0x12};
  auto s = FontHelper::ReadUInt8(string_view((const char*)input1, 1));
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, 0x12);

  uint8_t input2[] = {0xFA};
  s = FontHelper::ReadUInt8(string_view((const char*)input2, 1));
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, 0x00FA);

  s = FontHelper::ReadUInt8(string_view((const char*)input1, 0));
  ASSERT_FALSE(s.ok());
}

TEST_F(FontHelperTest, WriteUInt8) {
  std::string out = "";
  FontHelper::WriteUInt8(0x12, out);
  char expected1[] = {0x12};
  ASSERT_EQ(out, absl::string_view(expected1, 1));

  out = "";
  FontHelper::WriteUInt8(0xFA, out);
  char expected2[] = {(char)0xFA};
  ASSERT_EQ(out, absl::string_view(expected2, 1));
}

TEST_F(FontHelperTest, ReadUInt16) {
  uint8_t input1[] = {0x12, 0x34, 0x56, 0x78};
  auto s = FontHelper::ReadUInt16(string_view((const char*)input1, 4));
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, 0x1234);

  uint8_t input2[] = {0x00, 0xFA};
  s = FontHelper::ReadUInt16(string_view((const char*)input2, 2));
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, 0x00FA);

  s = FontHelper::ReadUInt16(string_view((const char*)input1, 1));
  ASSERT_FALSE(s.ok());
}

TEST_F(FontHelperTest, WriteUInt16) {
  std::string out = "ab";
  FontHelper::WriteUInt16(0x1234, out);
  char expected1[] = {'a', 'b', 0x12, 0x34};
  ASSERT_EQ(out, absl::string_view(expected1, 4));

  out = "";
  FontHelper::WriteUInt16(0x00FA, out);
  char expected2[] = {0x00, (char)0xFA};
  ASSERT_EQ(out, absl::string_view(expected2, 2));
}

TEST_F(FontHelperTest, ReadInt16) {
  uint8_t input1[] = {0xED, 0xCC};
  auto s = FontHelper::ReadInt16(string_view((const char*)input1, 2));
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, -0x1234);
}

TEST_F(FontHelperTest, WriteInt16) {
  std::string out = "";
  FontHelper::WriteInt16(-0x1234, out);
  char expected1[] = {(char)0xED, (char)0xCC};
  ASSERT_EQ(out, absl::string_view(expected1, 2));
}

TEST_F(FontHelperTest, ReadUInt24) {
  uint8_t input1[] = {0x12, 0x34, 0x56};
  auto s = FontHelper::ReadUInt24(string_view((const char*)input1, 3));
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, 0x123456);
}

TEST_F(FontHelperTest, WriteUInt24) {
  std::string out = "";
  FontHelper::WriteUInt24(0x00123456, out);
  char expected1[] = {0x12, 0x34, 0x56};
  ASSERT_EQ(out, absl::string_view(expected1, 3));
}

TEST_F(FontHelperTest, ReadUInt32) {
  uint8_t input1[] = {0x12, 0x34, 0x56, 0x78};
  auto s = FontHelper::ReadUInt32(string_view((const char*)input1, 4));
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, 0x12345678);

  uint8_t input2[] = {0x00, 0x00, 0x00, 0xFA};
  s = FontHelper::ReadUInt32(string_view((const char*)input2, 4));
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, 0x000000FA);

  s = FontHelper::ReadUInt32(string_view((const char*)input1, 3));
  ASSERT_FALSE(s.ok());
}

TEST_F(FontHelperTest, WriteUInt32) {
  std::string out = "";
  FontHelper::WriteUInt32(0x12345678, out);
  char expected1[] = {0x12, 0x34, 0x56, 0x78};
  ASSERT_EQ(out, absl::string_view(expected1, 4));

  out = "";
  FontHelper::WriteUInt32(0x000000FA, out);
  char expected2[] = {0x00, 0x00, 0x00, (char)0xFA};
  ASSERT_EQ(out, absl::string_view(expected2, 4));
}

TEST_F(FontHelperTest, WriteFixed) {
  std::string out = "";
  FontHelper::WriteFixed(0.456, out);
  char expected1[] = {0x00, 0x00, 0x74, (char)0xbc};
  ASSERT_EQ(out, absl::string_view(expected1, 4));

  out = "";
  FontHelper::WriteFixed(12.456, out);
  char expected2[] = {0x00, 0x0C, 0x74, (char)0xbc};
  ASSERT_EQ(out, absl::string_view(expected2, 4));

  out = "";
  FontHelper::WriteFixed(-12.456, out);
  char expected3[] = {(char)0xff, (char)0xf3, (char)0x8b, (char)0x44};
  ASSERT_EQ(out, absl::string_view(expected3, 4));
}

TEST_F(FontHelperTest, ReadFixed) {
  // 0x123
  char in1[] = {(char)0x01, (char)0x23, (char)0x00, (char)0x00};
  auto out = FontHelper::ReadFixed(string_view(in1, 4));
  ASSERT_EQ((int)(roundf(*out * 1000.0f)), 0x123 * 1000.0f);

  // -12.456
  char in2[] = {(char)0xff, (char)0xf3, (char)0x8b, (char)0x44};
  out = FontHelper::ReadFixed(string_view(in2, 4));
  ASSERT_EQ((int)(roundf(*out * 1000.0f)), -12456);
}

TEST_F(FontHelperTest, WillFixedOverflow) {
  ASSERT_FALSE(FontHelper::WillFixedOverflow(-1234.0f));
  ASSERT_FALSE(FontHelper::WillFixedOverflow(1234.0f));
  ASSERT_FALSE(FontHelper::WillFixedOverflow(-32768.0f));
  ASSERT_FALSE(FontHelper::WillFixedOverflow(32767.0f));
  ASSERT_TRUE(FontHelper::WillFixedOverflow(-32769.0f));
  ASSERT_TRUE(FontHelper::WillFixedOverflow(32768.0f));
}

TEST_F(FontHelperTest, GlyfData_Short) {
  auto data = FontHelper::GlyfData(roboto_ab.get(), 0);
  ASSERT_TRUE(data.ok()) << data.status();
  ASSERT_EQ(data->size(), 0);

  data = FontHelper::GlyfData(roboto_ab.get(), 45);
  ASSERT_TRUE(data.ok()) << data.status();
  ASSERT_EQ(data->size(), 0);

  data = FontHelper::GlyfData(roboto_ab.get(), 69);
  ASSERT_TRUE(data.ok()) << data.status();
  ASSERT_GT(data->size(), 0);

  data = FontHelper::GlyfData(roboto_ab.get(), 70);
  ASSERT_TRUE(data.ok()) << data.status();
  ASSERT_GT(data->size(), 0);

  data = FontHelper::GlyfData(roboto_Awesome.get(), 91);
  ASSERT_TRUE(data.ok()) << data.status();
  absl::string_view expected_w((const char*)roboto_glyf_gid91_w, 156);
  ASSERT_EQ(expected_w, *data);

  data = FontHelper::GlyfData(roboto_Awesome.get(), 37);
  ASSERT_TRUE(data.ok()) << data.status();
  absl::string_view expected_A((const char*)roboto_glyf_gid37_A, 140);
  ASSERT_EQ(expected_A, *data);

  data = FontHelper::GlyfData(roboto_Awesome.get(), 73);
  ASSERT_TRUE(data.ok()) << data.status();
  absl::string_view expected_e((const char*)roboto_glyf_gid73_e, 210);
  ASSERT_EQ(expected_e, *data);

  data = FontHelper::GlyfData(roboto_ab.get(), 71);
  ASSERT_TRUE(absl::IsNotFound(data.status())) << data.status();
}

TEST_F(FontHelperTest, GlyfData_Long) {
  auto data = FontHelper::GlyfData(noto_sans_ift_ttf.get(), 0);
  ASSERT_TRUE(data.ok()) << data.status();
  ASSERT_EQ(data->size(), 0);

  data = FontHelper::GlyfData(noto_sans_ift_ttf.get(), 52);
  ASSERT_TRUE(data.ok()) << data.status();
  ASSERT_GT(data->size(), 0);

  data = FontHelper::GlyfData(noto_sans_ift_ttf.get(), 72);
  ASSERT_TRUE(data.ok()) << data.status();
  ASSERT_GT(data->size(), 0);

  data = FontHelper::GlyfData(noto_sans_ift_ttf.get(), 1055);
  ASSERT_TRUE(data.ok()) << data.status();
  ASSERT_EQ(data->size(), 0);
}

TEST_F(FontHelperTest, GvarData) {
  auto data = FontHelper::GvarData(roboto_vf.get(), 2);
  ASSERT_TRUE(data.ok()) << data.status();
  ASSERT_EQ(data->size(), 0);
  ASSERT_EQ(*data, "");

  data = FontHelper::GvarData(roboto_vf.get(), 5);
  ASSERT_TRUE(data.ok()) << data.status();
  ASSERT_EQ(data->size(), 250);
  const uint8_t expected[11] = {0x80, 0x06, 0x00, 0x2c, 0x00, 0x2a,
                                0x00, 0x02, 0x00, 0x26, 0x00};
  string_view expected_str((const char*)expected, 11);
  ASSERT_EQ(data->substr(0, 11), expected_str);
}

TEST_F(FontHelperTest, GvarSharedTupleCount) {
  auto count = FontHelper::GvarSharedTupleCount(roboto_vf.get());
  ASSERT_TRUE(count.ok()) << count.status();
  ASSERT_EQ(*count, 6);
}

TEST_F(FontHelperTest, GvarSharedTupleChecksum) {
  auto checksum = FontHelper::GvarSharedTupleChecksum(roboto_vf.get());
  ASSERT_TRUE(checksum.ok()) << checksum.status();
  ASSERT_EQ(*checksum, 0x56ADBE3852392412);

  checksum = FontHelper::GvarSharedTupleChecksum(roboto_vf_abcd.get());
  ASSERT_TRUE(checksum.ok()) << checksum.status();
  ASSERT_EQ(*checksum, 0x56ADBE3852392412);
}

TEST_F(FontHelperTest, GvarData_NotFound) {
  auto data = FontHelper::GvarData(roboto_vf.get(), 1300);
  ASSERT_TRUE(absl::IsNotFound(data.status())) << data.status();
}

TEST_F(FontHelperTest, Loca) {
  auto s = FontHelper::Loca(roboto_ab.get());
  ASSERT_TRUE(s.ok()) << s.status();
  hb_blob_unique_ptr loca_blob = make_hb_blob(
      hb_face_reference_table(roboto_ab.get(), HB_TAG('l', 'o', 'c', 'a')));
  uint32_t length = 0;
  EXPECT_EQ(s->data(), hb_blob_get_data(loca_blob.get(), &length));
  EXPECT_EQ(s->size(), length);

  s = FontHelper::Loca(noto_sans_jp_otf.get());
  ASSERT_TRUE(absl::IsNotFound(s.status())) << s.status();
}

TEST_F(FontHelperTest, GidToUnicodeMap) {
  auto map = FontHelper::GidToUnicodeMap(roboto_ab.get());

  absl::flat_hash_map<uint32_t, uint32_t> expected = {
      {69, 0x61},
      {70, 0x62},
  };

  ASSERT_EQ(map, expected);
}

TEST_F(FontHelperTest, GetTags) {
  auto s = FontHelper::GetTags(roboto_ab.get());
  ASSERT_TRUE(s.contains(FontHelper::kLoca));
  ASSERT_TRUE(s.contains(FontHelper::kGlyf));
  ASSERT_FALSE(s.contains(FontHelper::kCFF));

  s = FontHelper::GetTags(noto_sans_jp_otf.get());
  ASSERT_FALSE(s.contains(FontHelper::kLoca));
  ASSERT_FALSE(s.contains(FontHelper::kGlyf));
  ASSERT_TRUE(s.contains(FontHelper::kCFF));
}

TEST_F(FontHelperTest, GetOrderedTags) {
  auto s = FontHelper::ToStrings(FontHelper::GetOrderedTags(roboto_ab.get()));
  EXPECT_EQ(s[0], "gasp");
  EXPECT_EQ(s[1], "maxp");
  EXPECT_EQ(s[16], "glyf");
  EXPECT_EQ(s[17], "fpgm");
}

TEST_F(FontHelperTest, GetFeatureTags) {
  auto tags = FontHelper::GetFeatureTags(roboto.get());

  // GSUB
  EXPECT_TRUE(tags.contains(HB_TAG('c', '2', 's', 'c')));
  EXPECT_TRUE(tags.contains(HB_TAG('l', 'i', 'g', 'a')));
  EXPECT_TRUE(tags.contains(HB_TAG('t', 'n', 'u', 'm')));

  // GPOS
  EXPECT_TRUE(tags.contains(HB_TAG('c', 'p', 's', 'p')));
  EXPECT_TRUE(tags.contains(HB_TAG('k', 'e', 'r', 'n')));
}

TEST_F(FontHelperTest, GetNonDefaultFeatureTags) {
  auto tags = FontHelper::GetNonDefaultFeatureTags(roboto.get());

  // GSUB
  EXPECT_TRUE(tags.contains(HB_TAG('c', '2', 's', 'c')));
  EXPECT_FALSE(tags.contains(HB_TAG('l', 'i', 'g', 'a')));
  EXPECT_TRUE(tags.contains(HB_TAG('t', 'n', 'u', 'm')));

  // GPOS
  EXPECT_TRUE(tags.contains(HB_TAG('c', 'p', 's', 'p')));
  EXPECT_FALSE(tags.contains(HB_TAG('k', 'e', 'r', 'n')));
}

TEST_F(FontHelperTest, GetDesignSpace) {
  auto ds = FontHelper::GetDesignSpace(roboto_vf.get());
  ASSERT_TRUE(ds.ok()) << ds.status();

  flat_hash_map<hb_tag_t, AxisRange> expected = {
      {HB_TAG('w', 'g', 'h', 't'), *AxisRange::Range(100, 900)},
      {HB_TAG('w', 'd', 't', 'h'), *AxisRange::Range(75, 100)},
  };

  EXPECT_EQ(*ds, expected);
}

TEST_F(FontHelperTest, GetDesignSpace_NonVf) {
  auto ds = FontHelper::GetDesignSpace(roboto.get());
  ASSERT_TRUE(ds.ok()) << ds.status();

  flat_hash_map<hb_tag_t, AxisRange> expected = {};

  EXPECT_EQ(*ds, expected);
}

TEST_F(FontHelperTest, ApplyIftbTableOrdering) {
  hb_subset_input_t* input = hb_subset_input_create_or_fail();
  hb_subset_input_keep_everything(input);

  hb_face_unique_ptr subset =
      make_hb_face(hb_subset_or_fail(roboto_ab.get(), input));
  hb_subset_input_destroy(input);
  FontHelper::ApplyIftbTableOrdering(subset.get());

  hb_blob_unique_ptr blob = make_hb_blob(hb_face_reference_blob(subset.get()));
  hb_face_unique_ptr subset_concrete =
      make_hb_face(hb_face_create(blob.get(), 0));

  auto s =
      FontHelper::ToStrings(FontHelper::GetOrderedTags(subset_concrete.get()));
  EXPECT_EQ(s[s.size() - 2], "glyf");
  EXPECT_EQ(s[s.size() - 1], "loca");
}

TEST_F(FontHelperTest, ToString) {
  ASSERT_EQ("glyf", FontHelper::ToString(HB_TAG('g', 'l', 'y', 'f')));
  ASSERT_EQ("abCD", FontHelper::ToString(HB_TAG('a', 'b', 'C', 'D')));
}

TEST_F(FontHelperTest, BuildFont) {
  absl::flat_hash_map<hb_tag_t, std::string> tables = {
      {HB_TAG('a', 'b', 'c', 'd'), "table_1"},
      {HB_TAG('d', 'e', 'f', 'g'), "table_2"},
  };
  auto font = FontHelper::BuildFont(tables);

  hb_face_unique_ptr face = font.face();
  auto table_1 = FontHelper::TableData(face.get(), HB_TAG('a', 'b', 'c', 'd'));
  auto table_2 = FontHelper::TableData(face.get(), HB_TAG('d', 'e', 'f', 'g'));

  ASSERT_EQ(table_1.str(), "table_1");
  ASSERT_EQ(table_2.str(), "table_2");
}

// TODO test BuildFont...

}  // namespace common
