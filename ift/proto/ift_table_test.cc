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
#include "common/sparse_bit_set.h"
#include "gtest/gtest.h"
#include "ift/proto/format_2_patch_map.h"
#include "ift/proto/patch_encoding.h"

using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::Status;
using common::CompatId;
using common::FontData;
using common::FontHelper;
using common::hb_blob_unique_ptr;
using common::hb_face_unique_ptr;
using common::make_hb_blob;
using common::make_hb_face;
using common::SparseBitSet;
using ift::proto::GLYPH_KEYED;
using ift::proto::TABLE_KEYED_PARTIAL;

namespace ift::proto {

class IFTTableTest : public ::testing::Test {
 protected:
  IFTTableTest()
      : roboto_ab(make_hb_face(nullptr)), noto_sans_jp(make_hb_face(nullptr)) {
    sample.SetUrlTemplate("fonts/go/here");
    sample.SetId({1, 2, 3, 4});
    auto sc = sample.GetPatchMap().AddEntry({30, 32}, 1, TABLE_KEYED_PARTIAL);
    sc.Update(sample.GetPatchMap().AddEntry({55, 56, 57}, 2, GLYPH_KEYED));
    assert(sc.ok());

    sample_with_extensions = sample;
    sc = sample_with_extensions.GetPatchMap().AddEntry(
        {77, 78}, 3,
        TABLE_KEYED_PARTIAL);  // TODO XXXXX we don't track extensions here
                               // anymore.

    overlap_sample = sample;
    sc.Update(
        overlap_sample.GetPatchMap().AddEntry({55}, 3, TABLE_KEYED_PARTIAL));

    complex_ids.SetUrlTemplate("fonts/go/here");
    sc.Update(complex_ids.GetPatchMap().AddEntry({0}, 0, TABLE_KEYED_PARTIAL));
    sc.Update(complex_ids.GetPatchMap().AddEntry({5}, 5, TABLE_KEYED_PARTIAL));
    sc.Update(complex_ids.GetPatchMap().AddEntry({2}, 2, TABLE_KEYED_PARTIAL));
    sc.Update(complex_ids.GetPatchMap().AddEntry({4}, 4, TABLE_KEYED_PARTIAL));
    assert(sc.ok());

    hb_blob_unique_ptr blob = make_hb_blob(
        hb_blob_create_from_file("common/testdata/Roboto-Regular.ab.ttf"));
    roboto_ab = make_hb_face(hb_face_create(blob.get(), 0));

    blob = make_hb_blob(
        hb_blob_create_from_file("common/testdata/NotoSansJP-Regular.otf"));
    noto_sans_jp = make_hb_face(hb_face_create(blob.get(), 0));
  }

  hb_face_unique_ptr roboto_ab;
  hb_face_unique_ptr noto_sans_jp;
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

  std::string expected =
      *Format2PatchMap::Serialize(sample, std::nullopt, std::nullopt);
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

  FontData expected_ift(
      *Format2PatchMap::Serialize(sample, std::nullopt, std::nullopt));
  FontData expected_iftx(*Format2PatchMap::Serialize(
      sample_with_extensions, std::nullopt, std::nullopt));
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

TEST_F(IFTTableTest, AddToFont_WithExtensionAndCharStringsOffset) {
  auto font =
      IFTTable::AddToFont(noto_sans_jp.get(), sample, &sample_with_extensions);
  ASSERT_TRUE(font.ok()) << font.status();
  hb_face_unique_ptr face = font->face();

  FontData ift_table =
      FontHelper::TableData(face.get(), HB_TAG('I', 'F', 'T', ' '));
  FontData iftx_table =
      FontHelper::TableData(face.get(), HB_TAG('I', 'F', 'T', 'X'));

  FontData expected_ift(
      *Format2PatchMap::Serialize(sample, 0xa7ed, std::nullopt));
  FontData expected_iftx(*Format2PatchMap::Serialize(
      sample_with_extensions, std::nullopt, std::nullopt));
  ASSERT_EQ(ift_table, expected_ift);
  ASSERT_EQ(iftx_table, expected_iftx);
}

TEST_F(IFTTableTest, GetId) { ASSERT_EQ(sample.GetId(), CompatId(1, 2, 3, 4)); }

TEST_F(IFTTableTest, GetId_None) { ASSERT_EQ(empty.GetId(), CompatId()); }

TEST_F(IFTTableTest, SetId_Good) {
  IFTTable table;
  table.SetId({5, 2, 3, 4});
  ASSERT_EQ(table.GetId(), CompatId(5, 2, 3, 4));
}

}  // namespace ift::proto
