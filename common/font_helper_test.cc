#include "common/font_helper.h"

#include <cstdint>

#include "absl/container/flat_hash_map.h"
#include "common/font_data.h"
#include "gtest/gtest.h"
#include "hb-subset.h"

using absl::flat_hash_map;
using absl::string_view;

namespace common {

class FontHelperTest : public ::testing::Test {
 protected:
  FontHelperTest()
      : noto_sans_jp_otf(make_hb_face(nullptr)),
        noto_sans_ift_ttf(make_hb_face(nullptr)),
        roboto_ab(make_hb_face(nullptr)) {
    hb_blob_unique_ptr blob = make_hb_blob(hb_blob_create_from_file(
        "patch_subset/testdata/Roboto-Regular.ab.ttf"));
    roboto_ab = make_hb_face(hb_face_create(blob.get(), 0));

    blob = make_hb_blob(hb_blob_create_from_file(
        "patch_subset/testdata/NotoSansJP-Regular.otf"));
    noto_sans_jp_otf = make_hb_face(hb_face_create(blob.get(), 0));

    blob = make_hb_blob(
        hb_blob_create_from_file("ift/testdata/NotoSansJP-Regular.ift.ttf"));
    noto_sans_ift_ttf = make_hb_face(hb_face_create(blob.get(), 0));
  }

  hb_face_unique_ptr noto_sans_jp_otf;
  hb_face_unique_ptr noto_sans_ift_ttf;
  hb_face_unique_ptr roboto_ab;
};

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

  data = FontHelper::GlyfData(roboto_ab.get(), 71);
  ASSERT_TRUE(absl::IsInvalidArgument(data.status())) << data.status();
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

  // TODO
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
