#include "ift/proto/ift_table.h"

#include <cstdio>
#include <cstring>
#include <optional>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "common/compat_id.h"
#include "common/font_data.h"
#include "common/font_helper.h"
#include "common/hb_set_unique_ptr.h"
#include "common/sparse_bit_set.h"
#include "gtest/gtest.h"
#include "ift/proto/IFT.pb.h"
#include "ift/proto/format_2_patch_map.h"

using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::Status;
using common::CompatId;
using common::FontData;
using common::FontHelper;
using common::hb_blob_unique_ptr;
using common::hb_face_unique_ptr;
using common::hb_set_unique_ptr;
using common::make_hb_blob;
using common::make_hb_face;
using common::make_hb_set;
using common::SparseBitSet;
using ift::proto::GLYPH_KEYED;
using ift::proto::TABLE_KEYED_PARTIAL;

namespace ift::proto {

class IFTTableTest : public ::testing::Test {
 protected:
  IFTTableTest()
      : roboto_ab(make_hb_face(nullptr)), iftb(make_hb_face(nullptr)) {
    sample.SetUrlTemplate("fonts/go/here");
    sample.SetId({1, 2, 3, 4});
    sample.GetPatchMap().AddEntry({30, 32}, 1, TABLE_KEYED_PARTIAL);
    sample.GetPatchMap().AddEntry({55, 56, 57}, 2, GLYPH_KEYED);

    sample_with_extensions = sample;
    sample_with_extensions.GetPatchMap().AddEntry(
        {77, 78}, 3,
        TABLE_KEYED_PARTIAL);  // TODO XXXXX we don't track extensions here
                               // anymore.

    overlap_sample = sample;
    overlap_sample.GetPatchMap().AddEntry({55}, 3, TABLE_KEYED_PARTIAL);

    complex_ids.SetUrlTemplate("fonts/go/here");
    complex_ids.GetPatchMap().AddEntry({0}, 0, TABLE_KEYED_PARTIAL);
    complex_ids.GetPatchMap().AddEntry({5}, 5, TABLE_KEYED_PARTIAL);
    complex_ids.GetPatchMap().AddEntry({2}, 2, TABLE_KEYED_PARTIAL);
    complex_ids.GetPatchMap().AddEntry({4}, 4, TABLE_KEYED_PARTIAL);

    hb_blob_unique_ptr blob = make_hb_blob(hb_blob_create_from_file(
        "common/testdata/Roboto-Regular.ab.ttf"));
    roboto_ab = make_hb_face(hb_face_create(blob.get(), 0));

    blob = make_hb_blob(
        hb_blob_create_from_file("ift/testdata/NotoSansJP-Regular.iftb.ttf"));
    FontData font_data(std::move(blob));

    std::string copy = font_data.string();
    copy[0] = 'O';
    copy[1] = 'T';
    copy[2] = 'T';
    copy[3] = 'O';

    font_data.copy(copy);
    iftb = font_data.face();
  }

  hb_face_unique_ptr roboto_ab;
  hb_face_unique_ptr iftb;
  IFTTable empty;
  IFTTable sample;
  IFTTable sample_with_extensions;
  IFTTable overlap_sample;
  IFTTable complex_ids;
};

TEST_F(IFTTableTest, AddToFont) {
  auto font = IFTTable::AddToFont(roboto_ab.get(), sample, std::nullopt);
  ASSERT_TRUE(font.ok()) << font.status();

  hb_face_unique_ptr face = font->face();
  hb_blob_unique_ptr blob = make_hb_blob(
      hb_face_reference_table(face.get(), HB_TAG('I', 'F', 'T', ' ')));

  FontData data(blob.get());
  

  std::string expected = *Format2PatchMap::Serialize(sample);
  FontData expected_data(expected);

  ASSERT_EQ(data, expected_data);

  auto original_tag_order =
      FontHelper::ToStrings(FontHelper::GetOrderedTags(roboto_ab.get()));
  auto new_tag_order =
      FontHelper::ToStrings(FontHelper::GetOrderedTags(face.get()));

  new_tag_order.erase(
      std::find(new_tag_order.begin(), new_tag_order.end(), "IFT "));

  EXPECT_EQ(original_tag_order, new_tag_order);
}

TEST_F(IFTTableTest, AddToFont_WithExtension) {
  auto font =
      IFTTable::AddToFont(roboto_ab.get(), sample, &sample_with_extensions);
  ASSERT_TRUE(font.ok()) << font.status();
  hb_face_unique_ptr face = font->face();

  FontData ift_table =
      FontHelper::TableData(face.get(), HB_TAG('I', 'F', 'T', ' '));
  FontData iftx_table =
      FontHelper::TableData(face.get(), HB_TAG('I', 'F', 'T', 'X'));

  FontData expected_ift(*Format2PatchMap::Serialize(sample));
  FontData expected_iftx(*Format2PatchMap::Serialize(sample_with_extensions));
  ASSERT_EQ(ift_table, expected_ift);
  ASSERT_EQ(iftx_table, expected_iftx);

  auto original_tag_order =
      FontHelper::ToStrings(FontHelper::GetOrderedTags(roboto_ab.get()));
  auto new_tag_order =
      FontHelper::ToStrings(FontHelper::GetOrderedTags(face.get()));

  new_tag_order.erase(
      std::find(new_tag_order.begin(), new_tag_order.end(), "IFT "));
  new_tag_order.erase(
      std::find(new_tag_order.begin(), new_tag_order.end(), "IFTX"));

  EXPECT_EQ(original_tag_order, new_tag_order);
}

TEST_F(IFTTableTest, AddToFont_IftbConversion) {
  auto font = IFTTable::AddToFont(iftb.get(), sample, std::nullopt, true);
  ASSERT_TRUE(font.ok()) << font.status();

  hb_face_unique_ptr face = font->face();

  FontData ift_table =
      FontHelper::TableData(face.get(), HB_TAG('I', 'F', 'T', ' '));
  FontData expected_ift(*Format2PatchMap::Serialize(sample));
  ASSERT_EQ(ift_table, expected_ift);

  auto expected_tags = FontHelper::GetTags(iftb.get());
  auto new_tags = FontHelper::GetTags(face.get());
  expected_tags.erase(FontHelper::kIFTB);
  expected_tags.insert(FontHelper::kIFT);

  EXPECT_EQ(expected_tags, new_tags);

  auto ordered_tags =
      FontHelper::ToStrings(FontHelper::GetOrderedTags(face.get()));
  EXPECT_EQ(ordered_tags[ordered_tags.size() - 3], "IFT ");
  EXPECT_EQ(ordered_tags[ordered_tags.size() - 2], "glyf");
  EXPECT_EQ(ordered_tags[ordered_tags.size() - 1], "loca");
}

TEST_F(IFTTableTest, AddToFont_IftbConversionRoboto) {
  auto font = IFTTable::AddToFont(roboto_ab.get(), sample, std::nullopt, true);
  ASSERT_TRUE(font.ok()) << font.status();

  hb_face_unique_ptr face = font->face();

  FontData ift_table =
      FontHelper::TableData(face.get(), HB_TAG('I', 'F', 'T', ' '));
  FontData expected_ift(*Format2PatchMap::Serialize(sample));
  ASSERT_EQ(ift_table, expected_ift);

  auto expected_tags = FontHelper::GetTags(roboto_ab.get());
  auto new_tags = FontHelper::GetTags(face.get());
  expected_tags.insert(FontHelper::kIFT);

  EXPECT_EQ(expected_tags, new_tags);

  auto ordered_tags =
      FontHelper::ToStrings(FontHelper::GetOrderedTags(face.get()));
  EXPECT_EQ(ordered_tags[ordered_tags.size() - 3], "IFT ");
  EXPECT_EQ(ordered_tags[ordered_tags.size() - 2], "glyf");
  EXPECT_EQ(ordered_tags[ordered_tags.size() - 1], "loca");
}

TEST_F(IFTTableTest, GetId) { ASSERT_EQ(sample.GetId(), CompatId(1, 2, 3, 4)); }

TEST_F(IFTTableTest, GetId_None) {
  ASSERT_EQ(empty.GetId(), CompatId());
}

TEST_F(IFTTableTest, SetId_Good) {
  IFTTable table;
  table.SetId({5, 2, 3, 4});
  ASSERT_EQ(table.GetId(), CompatId(5, 2, 3, 4));
}

}  // namespace ift::proto
