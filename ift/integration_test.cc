#include <cstdint>
#include <optional>
#include <string>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "common/axis_range.h"
#include "common/font_data.h"
#include "common/font_helper.h"
#include "common/hb_set_unique_ptr.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "hb.h"
#include "ift/client/fontations_client.h"
#include "ift/encoder/condition.h"
#include "ift/encoder/encoder.h"
#include "ift/encoder/subset_definition.h"
#include "ift/proto/patch_map.h"
#include "ift/testdata/test_segments.h"

using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using common::AxisRange;
using common::FontData;
using common::FontHelper;
using common::hb_blob_unique_ptr;
using common::hb_face_unique_ptr;
using common::hb_set_unique_ptr;
using common::make_hb_blob;
using common::make_hb_face;
using common::make_hb_set;
using ift::client::Extend;
using ift::client::ExtendWithDesignSpace;
using ift::encoder::Condition;
using ift::encoder::Encoder;
using ift::encoder::SubsetDefinition;
using ift::proto::PatchEncoding;
using ift::proto::PatchMap;
using ift::testdata::TestFeatureSegment1;
using ift::testdata::TestFeatureSegment2;
using ift::testdata::TestFeatureSegment3;
using ift::testdata::TestFeatureSegment4;
using ift::testdata::TestFeatureSegment5;
using ift::testdata::TestFeatureSegment6;
using ift::testdata::TestSegment1;
using ift::testdata::TestSegment2;
using ift::testdata::TestSegment3;
using ift::testdata::TestSegment4;
using ift::testdata::TestVfSegment1;
using ift::testdata::TestVfSegment2;
using ift::testdata::TestVfSegment3;
using ift::testdata::TestVfSegment4;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::IsSupersetOf;
using ::testing::Not;

namespace ift {

constexpr hb_tag_t kWdth = HB_TAG('w', 'd', 't', 'h');
constexpr hb_tag_t kWght = HB_TAG('w', 'g', 'h', 't');

class IntegrationTest : public ::testing::Test {
 protected:
  IntegrationTest() {
    // Noto Sans JP
    auto blob = make_hb_blob(
        hb_blob_create_from_file("ift/testdata/NotoSansJP-Regular.subset.ttf"));
    noto_sans_jp_.set(blob.get());

    // Noto Sans JP VF
    blob = make_hb_blob(
        hb_blob_create_from_file("ift/testdata/NotoSansJP[wght].subset.ttf"));
    noto_sans_vf_.set(blob.get());

    // Feature Test
    blob = make_hb_blob(hb_blob_create_from_file(
        "ift/testdata/NotoSansJP-Regular.feature-test.ttf"));
    feature_test_.set(blob.get());

    blob = make_hb_blob(
        hb_blob_create_from_file("common/testdata/Roboto[wdth,wght].ttf"));
    roboto_vf_.set(blob.get());
  }

  StatusOr<btree_set<uint32_t>> InitEncoderForMixedMode(Encoder& encoder) {
    auto face = noto_sans_jp_.face();

    hb_set_unique_ptr init = make_hb_set();
    hb_set_add_range(init.get(), 0, hb_face_get_glyph_count(face.get()) - 1);
    hb_set_unique_ptr excluded = make_hb_set();
    hb_set_add_sorted_array(excluded.get(), testdata::TEST_SEGMENT_1,
                            std::size(testdata::TEST_SEGMENT_1));
    hb_set_add_sorted_array(excluded.get(), testdata::TEST_SEGMENT_2,
                            std::size(testdata::TEST_SEGMENT_2));
    hb_set_add_sorted_array(excluded.get(), testdata::TEST_SEGMENT_3,
                            std::size(testdata::TEST_SEGMENT_3));
    hb_set_add_sorted_array(excluded.get(), testdata::TEST_SEGMENT_4,
                            std::size(testdata::TEST_SEGMENT_4));
    hb_set_subtract(init.get(), excluded.get());
    auto init_segment = common::to_btree_set(init.get());

    encoder.SetFace(face.get());

    auto sc = encoder.AddGlyphDataPatch(0, init_segment);
    sc.Update(encoder.AddGlyphDataPatch(1, TestSegment1()));
    sc.Update(encoder.AddGlyphDataPatch(2, TestSegment2()));
    sc.Update(encoder.AddGlyphDataPatch(3, TestSegment3()));
    sc.Update(encoder.AddGlyphDataPatch(4, TestSegment4()));

    if (!sc.ok()) {
      return sc;
    }

    return init_segment;
  }

  StatusOr<btree_set<uint32_t>> InitEncoderForVfMixedMode(Encoder& encoder) {
    auto face = noto_sans_vf_.face();
    encoder.SetFace(face.get());

    hb_set_unique_ptr init = make_hb_set();
    hb_set_add_range(init.get(), 0, hb_face_get_glyph_count(face.get()) - 1);
    hb_set_unique_ptr excluded = make_hb_set();
    hb_set_add_sorted_array(excluded.get(), testdata::TEST_VF_SEGMENT_1,
                            std::size(testdata::TEST_VF_SEGMENT_1));
    hb_set_add_sorted_array(excluded.get(), testdata::TEST_VF_SEGMENT_2,
                            std::size(testdata::TEST_VF_SEGMENT_2));
    hb_set_add_sorted_array(excluded.get(), testdata::TEST_VF_SEGMENT_3,
                            std::size(testdata::TEST_VF_SEGMENT_3));
    hb_set_add_sorted_array(excluded.get(), testdata::TEST_VF_SEGMENT_4,
                            std::size(testdata::TEST_VF_SEGMENT_4));
    hb_set_subtract(init.get(), excluded.get());
    auto init_segment = common::to_btree_set(init.get());

    auto sc = encoder.AddGlyphDataPatch(0, init_segment);
    sc.Update(encoder.AddGlyphDataPatch(1, TestVfSegment1()));
    sc.Update(encoder.AddGlyphDataPatch(2, TestVfSegment2()));
    sc.Update(encoder.AddGlyphDataPatch(3, TestVfSegment3()));
    sc.Update(encoder.AddGlyphDataPatch(4, TestVfSegment4()));
    return init_segment;
  }

  StatusOr<btree_set<uint32_t>> InitEncoderForMixedModeFeatureTest(
      Encoder& encoder) {
    auto face = feature_test_.face();
    encoder.SetFace(face.get());

    hb_set_unique_ptr init = make_hb_set();
    hb_set_add_range(init.get(), 0, hb_face_get_glyph_count(face.get()) - 1);
    hb_set_unique_ptr excluded = make_hb_set();
    hb_set_add_sorted_array(excluded.get(), testdata::TEST_FEATURE_SEGMENT_1,
                            std::size(testdata::TEST_FEATURE_SEGMENT_1));
    hb_set_add_sorted_array(excluded.get(), testdata::TEST_FEATURE_SEGMENT_2,
                            std::size(testdata::TEST_FEATURE_SEGMENT_2));
    hb_set_add_sorted_array(excluded.get(), testdata::TEST_FEATURE_SEGMENT_3,
                            std::size(testdata::TEST_FEATURE_SEGMENT_3));
    hb_set_add_sorted_array(excluded.get(), testdata::TEST_FEATURE_SEGMENT_4,
                            std::size(testdata::TEST_FEATURE_SEGMENT_4));
    hb_set_add_sorted_array(excluded.get(), testdata::TEST_FEATURE_SEGMENT_5,
                            std::size(testdata::TEST_FEATURE_SEGMENT_5));
    hb_set_add_sorted_array(excluded.get(), testdata::TEST_FEATURE_SEGMENT_6,
                            std::size(testdata::TEST_FEATURE_SEGMENT_6));
    hb_set_subtract(init.get(), excluded.get());
    auto init_segment = common::to_btree_set(init.get());

    auto sc = encoder.AddGlyphDataPatch(0, init_segment);
    sc.Update(encoder.AddGlyphDataPatch(1, TestFeatureSegment1()));
    sc.Update(encoder.AddGlyphDataPatch(2, TestFeatureSegment2()));
    sc.Update(encoder.AddGlyphDataPatch(3, TestFeatureSegment3()));
    sc.Update(encoder.AddGlyphDataPatch(4, TestFeatureSegment4()));
    sc.Update(encoder.AddGlyphDataPatch(5, TestFeatureSegment5()));
    sc.Update(encoder.AddGlyphDataPatch(6, TestFeatureSegment6()));
    return init_segment;
  }

  Status InitEncoderForTableKeyed(Encoder& encoder) {
    auto face = noto_sans_jp_.face();
    encoder.SetFace(face.get());
    return absl::OkStatus();
  }

  Status InitEncoderForVf(Encoder& encoder) {
    auto face = roboto_vf_.face();
    encoder.SetFace(face.get());
    return absl::OkStatus();
  }

  bool GvarHasLongOffsets(const FontData& font) {
    auto face = font.face();
    auto gvar_data =
        FontHelper::TableData(face.get(), HB_TAG('g', 'v', 'a', 'r'));
    if (gvar_data.size() < 16) {
      return false;
    }
    uint8_t flags_1 = gvar_data.str().at(15);
    return flags_1 == 0x01;
  }

  FontData noto_sans_jp_;
  FontData noto_sans_vf_;

  FontData feature_test_;

  FontData roboto_vf_;

  uint32_t chunk0_cp = 0x47;
  uint32_t chunk1_cp = 0xb7;
  uint32_t chunk2_cp = 0xb2;
  uint32_t chunk3_cp = 0xeb;
  uint32_t chunk4_cp = 0xa8;

  uint32_t chunk0_gid = 40;
  uint32_t chunk1_gid = 117;
  uint32_t chunk2_gid = 112;
  uint32_t chunk2_gid_non_cmapped = 900;
  uint32_t chunk3_gid = 169;
  uint32_t chunk4_gid = 103;

  static constexpr hb_tag_t kVrt3 = HB_TAG('v', 'r', 't', '3');
};

bool GlyphDataMatches(hb_face_t* a, hb_face_t* b, uint32_t codepoint) {
  uint32_t gid_a, gid_b;

  hb_font_t* font_a = hb_font_create(a);
  hb_font_t* font_b = hb_font_create(b);
  bool a_present = hb_font_get_nominal_glyph(font_a, codepoint, &gid_a);
  bool b_present = hb_font_get_nominal_glyph(font_b, codepoint, &gid_b);
  hb_font_destroy(font_a);
  hb_font_destroy(font_b);

  if (!a_present && !b_present) {
    return true;
  }

  if (a_present != b_present) {
    return false;
  }

  auto a_data = FontHelper::GlyfData(a, gid_a);
  auto b_data = FontHelper::GlyfData(b, gid_b);
  return *a_data == *b_data;
}

bool GvarDataMatches(hb_face_t* a, hb_face_t* b, uint32_t codepoint,
                     uint32_t ignore_count) {
  uint32_t gid_a, gid_b;

  hb_font_t* font_a = hb_font_create(a);
  hb_font_t* font_b = hb_font_create(b);
  bool a_present = hb_font_get_nominal_glyph(font_a, codepoint, &gid_a);
  bool b_present = hb_font_get_nominal_glyph(font_b, codepoint, &gid_b);
  hb_font_destroy(font_a);
  hb_font_destroy(font_b);

  if (!a_present && !b_present) {
    return true;
  }

  if (a_present != b_present) {
    return false;
  }

  auto a_data = FontHelper::GvarData(a, gid_a);
  auto b_data = FontHelper::GvarData(b, gid_b);

  return a_data->substr(ignore_count) == b_data->substr(ignore_count);
}

// TODO(garretrieger): full expansion test.
// TODO(garretrieger): test of a woff2 encoded IFT font.

TEST_F(IntegrationTest, TableKeyedOnly) {
  Encoder encoder;
  auto sc = InitEncoderForTableKeyed(encoder);
  ASSERT_TRUE(sc.ok()) << sc;

  sc = encoder.SetBaseSubset(flat_hash_set<uint32_t>{0x41, 0x42, 0x43});
  encoder.AddNonGlyphDataSegment(flat_hash_set<uint32_t>{0x45, 0x46, 0x47});
  encoder.AddNonGlyphDataSegment(flat_hash_set<uint32_t>{0x48, 0x49, 0x4A});
  encoder.AddNonGlyphDataSegment(flat_hash_set<uint32_t>{0x4B, 0x4C, 0x4D});
  encoder.AddNonGlyphDataSegment(flat_hash_set<uint32_t>{0x4E, 0x4F, 0x50});
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = encoder.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();

  auto encoded_face = encoding->init_font.face();
  auto codepoints = FontHelper::ToCodepointsSet(encoded_face.get());
  ASSERT_TRUE(codepoints.contains(0x41));
  ASSERT_FALSE(codepoints.contains(0x45));
  ASSERT_FALSE(codepoints.contains(0x48));
  ASSERT_FALSE(codepoints.contains(0x4B));
  ASSERT_FALSE(codepoints.contains(0x4E));

  auto extended = Extend(*encoding, {0x49});
  ASSERT_TRUE(extended.ok()) << extended.status();

  auto extended_face = extended->face();
  codepoints = FontHelper::ToCodepointsSet(extended_face.get());
  ASSERT_TRUE(codepoints.contains(0x41));
  ASSERT_FALSE(codepoints.contains(0x45));
  ASSERT_TRUE(codepoints.contains(0x48));
  ASSERT_TRUE(codepoints.contains(0x49));
  ASSERT_FALSE(codepoints.contains(0x4B));
  ASSERT_FALSE(codepoints.contains(0x4E));

  auto original_face = noto_sans_jp_.face();
  GlyphDataMatches(original_face.get(), extended_face.get(), 0x41);
  GlyphDataMatches(original_face.get(), extended_face.get(), 0x48);
  GlyphDataMatches(original_face.get(), extended_face.get(), 0x49);
}

TEST_F(IntegrationTest, TableKeyedMultiple) {
  Encoder encoder;
  auto sc = InitEncoderForTableKeyed(encoder);
  ASSERT_TRUE(sc.ok()) << sc;

  sc = encoder.SetBaseSubset(flat_hash_set<uint32_t>{0x41, 0x42, 0x43});
  encoder.AddNonGlyphDataSegment(flat_hash_set<uint32_t>{0x45, 0x46, 0x47});
  encoder.AddNonGlyphDataSegment(flat_hash_set<uint32_t>{0x48, 0x49, 0x4A});
  encoder.AddNonGlyphDataSegment(flat_hash_set<uint32_t>{0x4B, 0x4C, 0x4D});
  encoder.AddNonGlyphDataSegment(flat_hash_set<uint32_t>{0x4E, 0x4F, 0x50});
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = encoder.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();

  auto encoded_face = encoding->init_font.face();
  auto codepoints = FontHelper::ToCodepointsSet(encoded_face.get());
  ASSERT_TRUE(codepoints.contains(0x41));
  ASSERT_FALSE(codepoints.contains(0x45));
  ASSERT_FALSE(codepoints.contains(0x48));
  ASSERT_FALSE(codepoints.contains(0x4B));
  ASSERT_FALSE(codepoints.contains(0x4E));

  auto extended = Extend(*encoding, {0x49, 0x4F});
  ASSERT_TRUE(extended.ok()) << extended.status();
  auto extended_face = extended->face();

  codepoints = FontHelper::ToCodepointsSet(extended_face.get());
  ASSERT_TRUE(codepoints.contains(0x41));
  ASSERT_FALSE(codepoints.contains(0x45));
  ASSERT_TRUE(codepoints.contains(0x48));
  ASSERT_FALSE(codepoints.contains(0x4B));
  ASSERT_TRUE(codepoints.contains(0x4E));

  auto original_face = noto_sans_jp_.face();
  GlyphDataMatches(original_face.get(), extended_face.get(), 0x41);
  GlyphDataMatches(original_face.get(), extended_face.get(), 0x45);
  GlyphDataMatches(original_face.get(), extended_face.get(), 0x48);
  GlyphDataMatches(original_face.get(), extended_face.get(), 0x4E);
}

TEST_F(IntegrationTest, TableKeyedWithOverlaps) {
  Encoder encoder;
  auto sc = InitEncoderForTableKeyed(encoder);
  ASSERT_TRUE(sc.ok()) << sc;

  sc = encoder.SetBaseSubset(flat_hash_set<uint32_t>{0x41, 0x42, 0x43});
  encoder.AddNonGlyphDataSegment(flat_hash_set<uint32_t>{
      0x45, 0x46, 0x47, 0x48});  // 0x48 is in two subsets
  encoder.AddNonGlyphDataSegment(flat_hash_set<uint32_t>{0x48, 0x49, 0x4A});
  encoder.AddNonGlyphDataSegment(flat_hash_set<uint32_t>{0x4B, 0x4C, 0x4D});
  encoder.AddNonGlyphDataSegment(flat_hash_set<uint32_t>{0x4E, 0x4F, 0x50});
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = encoder.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();

  auto encoded_face = encoding->init_font.face();
  auto codepoints = FontHelper::ToCodepointsSet(encoded_face.get());
  ASSERT_TRUE(codepoints.contains(0x41));
  ASSERT_FALSE(codepoints.contains(0x45));
  ASSERT_FALSE(codepoints.contains(0x48));
  ASSERT_FALSE(codepoints.contains(0x4B));
  ASSERT_FALSE(codepoints.contains(0x4E));

  auto extended = Extend(*encoding, {0x48});
  ASSERT_TRUE(extended.ok()) << extended.status();

  auto extended_face = extended->face();
  codepoints = FontHelper::ToCodepointsSet(extended_face.get());
  ASSERT_TRUE(codepoints.contains(0x41));
  ASSERT_TRUE(codepoints.contains(0x48));
  auto original_face = noto_sans_jp_.face();

  // Extending for 0x48 should grab one and only one of the two possible
  // subsets, which specific one is client specific we just care that only one
  // was applied.
  if (codepoints.contains(0x45)) {
    GlyphDataMatches(original_face.get(), extended_face.get(), 0x45);
    ASSERT_FALSE(codepoints.contains(0x49));
  } else {
    ASSERT_TRUE(codepoints.contains(0x49));
    GlyphDataMatches(original_face.get(), extended_face.get(), 0x49);
  }
  ASSERT_FALSE(codepoints.contains(0x4B));
  ASSERT_FALSE(codepoints.contains(0x4E));

  GlyphDataMatches(original_face.get(), extended_face.get(), 0x41);
  GlyphDataMatches(original_face.get(), extended_face.get(), 0x48);
}

TEST_F(IntegrationTest, TableKeyed_DesignSpaceAugmentation_IgnoresDesignSpace) {
  Encoder encoder;
  auto sc = InitEncoderForVf(encoder);
  ASSERT_TRUE(sc.ok()) << sc;

  SubsetDefinition def{'a', 'b', 'c'};
  def.design_space[kWdth] = AxisRange::Point(100.0f);
  sc = encoder.SetBaseSubsetFromDef(def);

  encoder.AddNonGlyphDataSegment(flat_hash_set<uint32_t>{'d', 'e', 'f'});
  encoder.AddNonGlyphDataSegment(flat_hash_set<uint32_t>{'h', 'i', 'j'});
  encoder.AddDesignSpaceSegment({{kWdth, *AxisRange::Range(75.0f, 100.0f)}});
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = encoder.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();
  auto encoded_face = encoding->init_font.face();

  auto codepoints = FontHelper::ToCodepointsSet(encoded_face.get());
  ASSERT_THAT(codepoints, IsSupersetOf({'a', 'b', 'c'}));
  ASSERT_THAT(codepoints, AllOf(Not(Contains('d')), Not(Contains('e')),
                                Not(Contains('f')), Not(Contains('h')),
                                Not(Contains('i')), Not(Contains('j'))));

  auto ds = FontHelper::GetDesignSpace(encoded_face.get());
  flat_hash_map<hb_tag_t, AxisRange> expected_ds{
      {kWght, *AxisRange::Range(100, 900)},
  };
  ASSERT_EQ(*ds, expected_ds);

  auto extended = Extend(*encoding, {'e'});
  ASSERT_TRUE(extended.ok()) << extended.status();
  auto extended_face = extended->face();

  ds = FontHelper::GetDesignSpace(extended_face.get());
  expected_ds = {
      {kWght, *AxisRange::Range(100, 900)},
  };
  ASSERT_EQ(*ds, expected_ds);

  codepoints = FontHelper::ToCodepointsSet(extended_face.get());
  ASSERT_THAT(codepoints, IsSupersetOf({'a', 'b', 'c', 'd', 'e', 'f'}));
  ASSERT_THAT(codepoints, AllOf(Not(Contains('h')), Not(Contains('i')),
                                Not(Contains('j'))));
}

TEST_F(IntegrationTest, SharedBrotli_DesignSpaceAugmentation) {
  Encoder encoder;
  auto sc = InitEncoderForVf(encoder);
  ASSERT_TRUE(sc.ok()) << sc;

  SubsetDefinition def{'a', 'b', 'c'};
  def.design_space[kWdth] = AxisRange::Point(100.0f);
  sc = encoder.SetBaseSubsetFromDef(def);

  encoder.AddNonGlyphDataSegment(flat_hash_set<uint32_t>{'d', 'e', 'f'});
  encoder.AddNonGlyphDataSegment(flat_hash_set<uint32_t>{'h', 'i', 'j'});
  encoder.AddDesignSpaceSegment({{kWdth, *AxisRange::Range(75.0f, 100.0f)}});
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = encoder.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();
  auto encoded_face = encoding->init_font.face();

  auto codepoints = FontHelper::ToCodepointsSet(encoded_face.get());
  ASSERT_THAT(codepoints, IsSupersetOf({'a', 'b', 'c'}));
  ASSERT_THAT(codepoints, AllOf(Not(Contains('d')), Not(Contains('e')),
                                Not(Contains('f')), Not(Contains('h')),
                                Not(Contains('i')), Not(Contains('j'))));

  auto ds = FontHelper::GetDesignSpace(encoded_face.get());
  flat_hash_map<hb_tag_t, AxisRange> expected_ds{
      {kWght, *AxisRange::Range(100, 900)},
  };
  ASSERT_EQ(*ds, expected_ds);

  auto extended = ExtendWithDesignSpace(
      *encoding, {'b'}, {},
      {{HB_TAG('w', 'd', 't', 'h'), AxisRange::Point(80)}});
  ASSERT_TRUE(extended.ok()) << extended.status();
  auto extended_face = extended->face();

  ds = FontHelper::GetDesignSpace(extended_face.get());
  expected_ds = {
      {kWght, *AxisRange::Range(100, 900)},
      {kWdth, *AxisRange::Range(75, 100)},
  };
  ASSERT_EQ(*ds, expected_ds);

  codepoints = FontHelper::ToCodepointsSet(extended_face.get());
  ASSERT_THAT(codepoints, IsSupersetOf({'a', 'b', 'c'}));
  ASSERT_THAT(codepoints, AllOf(Not(Contains('d')), Not(Contains('e')),
                                Not(Contains('f')), Not(Contains('h')),
                                Not(Contains('i')), Not(Contains('j'))));

  // Try extending the updated font again.
  encoding->init_font.shallow_copy(*extended);
  extended = Extend(*encoding, {'e'});
  ASSERT_TRUE(extended.ok()) << extended.status();
  extended_face = extended->face();

  codepoints = FontHelper::ToCodepointsSet(extended_face.get());
  ASSERT_THAT(codepoints, IsSupersetOf({'a', 'b', 'c', 'd', 'e', 'f'}));

  ds = FontHelper::GetDesignSpace(extended_face.get());
  expected_ds = {
      {kWght, *AxisRange::Range(100, 900)},
      {kWdth, *AxisRange::Range(75, 100)},
  };
  ASSERT_EQ(*ds, expected_ds);
}

TEST_F(IntegrationTest, MixedMode) {
  Encoder encoder;
  auto init_gids = InitEncoderForMixedMode(encoder);
  ASSERT_TRUE(init_gids.ok()) << init_gids.status();

  auto face = noto_sans_jp_.face();
  auto base_segment = FontHelper::GidsToUnicodes(face.get(), *init_gids);

  // target paritions: {{0, 1}, {2}, {3, 4}}
  auto segment_0 = FontHelper::GidsToUnicodes(face.get(), *init_gids);
  auto segment_1 = FontHelper::GidsToUnicodes(face.get(), TestSegment1());
  auto segment_2 = FontHelper::GidsToUnicodes(face.get(), TestSegment2());
  auto segment_3 = FontHelper::GidsToUnicodes(face.get(), TestSegment3());
  auto segment_4 = FontHelper::GidsToUnicodes(face.get(), TestSegment4());

  flat_hash_set<uint32_t> base;
  base.insert(segment_0.begin(), segment_0.end());
  base.insert(segment_1.begin(), segment_1.end());
  auto sc = encoder.SetBaseSubset(base);

  encoder.AddNonGlyphDataSegment(segment_2);

  auto segment = segment_3;
  segment.insert(segment_4.begin(), segment_4.end());
  encoder.AddNonGlyphDataSegment(segment);
  ASSERT_TRUE(sc.ok()) << sc;

  // Setup activations for 2 through 4 (1 is init)
  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_2), 2)));
  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_3), 3)));
  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_4), 4)));

  auto encoding = encoder.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();
  auto encoded_face = encoding->init_font.face();

  ASSERT_TRUE(FontHelper::GlyfData(encoded_face.get(), chunk2_gid_non_cmapped)
                  ->empty());

  auto codepoints = FontHelper::ToCodepointsSet(encoded_face.get());
  ASSERT_TRUE(codepoints.contains(chunk0_cp));
  ASSERT_TRUE(codepoints.contains(chunk1_cp));
  ASSERT_FALSE(codepoints.contains(chunk2_cp));
  ASSERT_FALSE(codepoints.contains(chunk3_cp));
  ASSERT_FALSE(codepoints.contains(chunk4_cp));

  auto extended = Extend(*encoding, {chunk3_cp, chunk4_cp});
  ASSERT_TRUE(extended.ok()) << extended.status();
  auto extended_face = extended->face();

  codepoints = FontHelper::ToCodepointsSet(extended_face.get());
  ASSERT_TRUE(codepoints.contains(chunk0_cp));
  ASSERT_TRUE(codepoints.contains(chunk1_cp));
  ASSERT_FALSE(codepoints.contains(chunk2_cp));
  ASSERT_TRUE(codepoints.contains(chunk3_cp));
  ASSERT_TRUE(codepoints.contains(chunk4_cp));

  ASSERT_TRUE(!FontHelper::GlyfData(extended_face.get(), chunk0_gid)->empty());
  ASSERT_TRUE(!FontHelper::GlyfData(extended_face.get(), chunk1_gid)->empty());
  ASSERT_FALSE(!FontHelper::GlyfData(extended_face.get(), chunk2_gid)->empty());
  ASSERT_FALSE(
      !FontHelper::GlyfData(extended_face.get(), chunk2_gid_non_cmapped)
           ->empty());
  ASSERT_TRUE(!FontHelper::GlyfData(extended_face.get(), chunk3_gid)->empty());
  ASSERT_TRUE(!FontHelper::GlyfData(extended_face.get(), chunk4_gid)->empty());

  auto original_face = noto_sans_jp_.face();
  GlyphDataMatches(original_face.get(), extended_face.get(), chunk0_gid);
  GlyphDataMatches(original_face.get(), extended_face.get(), chunk1_gid);
  GlyphDataMatches(original_face.get(), extended_face.get(), chunk3_gid);
  GlyphDataMatches(original_face.get(), extended_face.get(), chunk4_gid);
}

TEST_F(IntegrationTest, MixedMode_OptionalFeatureTags) {
  Encoder encoder;
  auto init_gids = InitEncoderForMixedModeFeatureTest(encoder);
  ASSERT_TRUE(init_gids.ok()) << init_gids.status();

  // target paritions: {{0}, {1}, {2}, {3}, {4}}
  // With optional feature chunks for vrt3:
  //   1, 2 -> 5
  //   4    -> 6
  auto face = feature_test_.face();
  auto segment_0 = FontHelper::GidsToUnicodes(face.get(), *init_gids);
  auto segment_1 = FontHelper::GidsToUnicodes(face.get(), TestSegment1());
  auto segment_2 = FontHelper::GidsToUnicodes(face.get(), TestSegment2());
  auto segment_3 = FontHelper::GidsToUnicodes(face.get(), TestSegment3());
  auto segment_4 = FontHelper::GidsToUnicodes(face.get(), TestSegment4());

  auto sc = encoder.SetBaseSubset(segment_0);

  encoder.AddNonGlyphDataSegment(segment_1);
  encoder.AddNonGlyphDataSegment(segment_2);
  encoder.AddNonGlyphDataSegment(segment_3);
  encoder.AddNonGlyphDataSegment(segment_4);

  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_1), 1)));
  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_2), 2)));
  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_3), 3)));
  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_4), 4)));

  {
    Condition condition;
    condition.child_conditions = {0};
    condition.subset_definition.feature_tags = {kVrt3};
    condition.activated_patch_id = 5;
    sc.Update(encoder.AddGlyphDataPatchCondition(condition));
  }

  {
    Condition condition;
    condition.child_conditions = {1};
    condition.subset_definition.feature_tags = {kVrt3};
    condition.activated_patch_id = 5;
    sc.Update(encoder.AddGlyphDataPatchCondition(condition));
  }

  {
    Condition condition;
    condition.child_conditions = {3};
    condition.subset_definition.feature_tags = {kVrt3};
    condition.activated_patch_id = 6;
    sc.Update(encoder.AddGlyphDataPatchCondition(condition));
  }

  encoder.AddFeatureGroupSegment({kVrt3});
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = encoder.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();
  auto encoded_face = encoding->init_font.face();

  auto codepoints = FontHelper::ToCodepointsSet(encoded_face.get());
  ASSERT_TRUE(codepoints.contains(chunk0_cp));
  ASSERT_FALSE(codepoints.contains(chunk1_cp));
  ASSERT_FALSE(codepoints.contains(chunk2_cp));
  ASSERT_FALSE(codepoints.contains(chunk3_cp));
  ASSERT_FALSE(codepoints.contains(chunk4_cp));

  // Ext 1 - extend to {chunk2_cp}
  auto extended = Extend(*encoding, {chunk2_cp});
  ASSERT_TRUE(extended.ok()) << extended.status();
  auto extended_face = extended->face();

  auto feature_tags = FontHelper::GetFeatureTags(extended_face.get());
  ASSERT_FALSE(feature_tags.contains(kVrt3));

  static constexpr uint32_t chunk2_gid = 816;
  static constexpr uint32_t chunk4_gid = 800;
  static constexpr uint32_t chunk5_gid = 989;
  static constexpr uint32_t chunk6_gid = 932;
  ASSERT_FALSE(FontHelper::GlyfData(extended_face.get(), chunk2_gid)->empty());
  ASSERT_TRUE(FontHelper::GlyfData(extended_face.get(), chunk5_gid)->empty());

  // Ext 2 - extend to {kVrt3}
  encoding->init_font.shallow_copy(*extended);
  extended = ExtendWithDesignSpace(*encoding, {chunk2_cp}, {kVrt3}, {});
  ASSERT_TRUE(extended.ok()) << extended.status();
  extended_face = extended->face();

  feature_tags = FontHelper::GetFeatureTags(extended_face.get());
  ASSERT_TRUE(feature_tags.contains(kVrt3));
  ASSERT_FALSE(FontHelper::GlyfData(extended_face.get(), chunk2_gid)->empty());
  ASSERT_TRUE(FontHelper::GlyfData(extended_face.get(), chunk4_gid)->empty());
  ASSERT_FALSE(FontHelper::GlyfData(extended_face.get(), chunk5_gid)->empty());
  ASSERT_TRUE(FontHelper::GlyfData(extended_face.get(), chunk6_gid)->empty());

  // Ext 3 - extend to chunk4_cp + kVrt3
  encoding->init_font.shallow_copy(*extended);
  extended =
      ExtendWithDesignSpace(*encoding, {chunk2_cp, chunk4_cp}, {kVrt3}, {});
  ASSERT_TRUE(extended.ok()) << extended.status();
  extended_face = extended->face();

  ASSERT_FALSE(FontHelper::GlyfData(extended_face.get(), chunk2_gid)->empty());
  ASSERT_FALSE(FontHelper::GlyfData(extended_face.get(), chunk4_gid)->empty());
  ASSERT_FALSE(FontHelper::GlyfData(extended_face.get(), chunk5_gid)->empty());
  ASSERT_FALSE(FontHelper::GlyfData(extended_face.get(), chunk6_gid)->empty());
}

TEST_F(IntegrationTest, MixedMode_CompositeConditions) {
  Encoder encoder;
  auto init_gids = InitEncoderForMixedMode(encoder);
  ASSERT_TRUE(init_gids.ok()) << init_gids.status();

  auto face = noto_sans_jp_.face();
  auto segment_1 = FontHelper::GidsToUnicodes(face.get(), TestSegment1());
  auto segment_2 = FontHelper::GidsToUnicodes(face.get(), TestSegment2());
  auto segment_3 = FontHelper::GidsToUnicodes(face.get(), TestSegment3());
  auto segment_4 = FontHelper::GidsToUnicodes(face.get(), TestSegment4());
  flat_hash_set<uint32_t> all;
  all.insert(segment_1.begin(), segment_1.end());
  all.insert(segment_2.begin(), segment_2.end());
  all.insert(segment_3.begin(), segment_3.end());
  all.insert(segment_4.begin(), segment_4.end());

  // target paritions: {}, {{1}, {2}, {3, 4}}
  auto sc = encoder.SetBaseSubset(flat_hash_set<uint32_t>{});
  encoder.AddNonGlyphDataSegment(all);
  ASSERT_TRUE(sc.ok()) << sc;

  // Setup some composite activation conditions
  {
    // 0
    Condition condition;
    condition.subset_definition = SubsetDefinition::Codepoints(segment_1);
    sc.Update(encoder.AddGlyphDataPatchCondition(condition));
  }

  {
    // 1
    Condition condition;
    condition.subset_definition = SubsetDefinition::Codepoints(segment_2);
    sc.Update(encoder.AddGlyphDataPatchCondition(condition));
  }

  {
    // 2
    Condition condition;
    condition.subset_definition = SubsetDefinition::Codepoints(segment_3);
    sc.Update(encoder.AddGlyphDataPatchCondition(condition));
  }

  {
    // 3
    Condition condition;
    condition.child_conditions = {0, 1};  // (1 OR 2)
    sc.Update(encoder.AddGlyphDataPatchCondition(condition));
  }

  {
    // 4
    Condition condition;
    condition.conjunctive = true;
    condition.child_conditions = {3, 2};  // (1 OR 2) AND 3
    condition.activated_patch_id = 4;
    sc.Update(encoder.AddGlyphDataPatchCondition(condition));
  }

  {
    // 5
    Condition condition;
    condition.child_conditions = {1, 2};  // (2 OR 3)
    condition.activated_patch_id = std::nullopt;
    sc.Update(encoder.AddGlyphDataPatchCondition(condition));
  }

  {
    // 6
    Condition condition;
    condition.child_conditions = {0, 5};  // 1 AND (2 OR 3)
    condition.conjunctive = true;
    condition.activated_patch_id = 3;
    sc.Update(encoder.AddGlyphDataPatchCondition(condition));
  }

  auto encoding = encoder.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();
  auto encoded_face = encoding->init_font.face();

  {
    // No conditions satisfied.
    auto extended = Extend(*encoding, {chunk1_cp});
    ASSERT_TRUE(extended.ok()) << extended.status();
    auto extended_face = extended->face();
    ASSERT_TRUE(FontHelper::GlyfData(extended_face.get(), chunk1_gid)->empty());
    ASSERT_TRUE(FontHelper::GlyfData(extended_face.get(), chunk2_gid)->empty());
    ASSERT_TRUE(FontHelper::GlyfData(extended_face.get(), chunk3_gid)->empty());
    ASSERT_TRUE(FontHelper::GlyfData(extended_face.get(), chunk4_gid)->empty());
  }

  {
    // (1 OR 2) AND 3 satisfied, chunk 4 loaded
    auto extended = Extend(*encoding, {chunk2_cp, chunk3_cp});
    ASSERT_TRUE(extended.ok()) << extended.status();
    auto extended_face = extended->face();
    ASSERT_TRUE(FontHelper::GlyfData(extended_face.get(), chunk1_gid)->empty());
    ASSERT_TRUE(FontHelper::GlyfData(extended_face.get(), chunk2_gid)->empty());
    ASSERT_TRUE(FontHelper::GlyfData(extended_face.get(), chunk3_gid)->empty());
    ASSERT_FALSE(
        FontHelper::GlyfData(extended_face.get(), chunk4_gid)->empty());
  }

  {
    // 1 AND (2 OR 3) 3 satisfied, chunk 3 loaded
    auto extended = Extend(*encoding, {chunk1_cp, chunk2_cp});
    ASSERT_TRUE(extended.ok()) << extended.status();
    auto extended_face = extended->face();
    ASSERT_TRUE(FontHelper::GlyfData(extended_face.get(), chunk1_gid)->empty());
    ASSERT_TRUE(FontHelper::GlyfData(extended_face.get(), chunk2_gid)->empty());
    ASSERT_FALSE(
        FontHelper::GlyfData(extended_face.get(), chunk3_gid)->empty());
    ASSERT_TRUE(FontHelper::GlyfData(extended_face.get(), chunk4_gid)->empty());
  }

  {
    // both conditions satisfied chunk 3 and 4 loaded
    auto extended = Extend(*encoding, {chunk1_cp, chunk2_cp, chunk3_cp});
    ASSERT_TRUE(extended.ok()) << extended.status();
    auto extended_face = extended->face();
    ASSERT_TRUE(FontHelper::GlyfData(extended_face.get(), chunk1_gid)->empty());
    ASSERT_TRUE(FontHelper::GlyfData(extended_face.get(), chunk2_gid)->empty());
    ASSERT_FALSE(
        FontHelper::GlyfData(extended_face.get(), chunk3_gid)->empty());
    ASSERT_FALSE(
        FontHelper::GlyfData(extended_face.get(), chunk4_gid)->empty());
  }
}

TEST_F(IntegrationTest, MixedMode_LocaLenChange) {
  Encoder encoder;
  auto init_gids = InitEncoderForMixedMode(encoder);
  ASSERT_TRUE(init_gids.ok()) << init_gids.status();

  auto face = noto_sans_jp_.face();
  auto segment_0 = FontHelper::GidsToUnicodes(face.get(), *init_gids);
  auto segment_1 = FontHelper::GidsToUnicodes(face.get(), TestSegment1());
  auto segment_2 = FontHelper::GidsToUnicodes(face.get(), TestSegment2());
  auto segment_3 = FontHelper::GidsToUnicodes(face.get(), TestSegment3());
  auto segment_4 = FontHelper::GidsToUnicodes(face.get(), TestSegment4());

  // target paritions: {{0}, {1}, {2}, {3}, {4}}
  auto sc = encoder.SetBaseSubset(segment_0);
  encoder.AddNonGlyphDataSegment(segment_1);
  encoder.AddNonGlyphDataSegment(segment_2);
  encoder.AddNonGlyphDataSegment(segment_3);
  encoder.AddNonGlyphDataSegment(segment_4);

  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_1), 1)));
  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_2), 2)));
  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_3), 3)));
  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_4), 4)));
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = encoder.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();
  auto encoded_face = encoding->init_font.face();

  auto codepoints = FontHelper::ToCodepointsSet(encoded_face.get());
  ASSERT_TRUE(codepoints.contains(chunk0_cp));
  ASSERT_FALSE(codepoints.contains(chunk1_cp));
  ASSERT_FALSE(codepoints.contains(chunk2_cp));
  ASSERT_FALSE(codepoints.contains(chunk3_cp));
  ASSERT_FALSE(codepoints.contains(chunk4_cp));

  // ### Phase 1 ###
  auto extended = Extend(*encoding, {chunk3_cp});
  ASSERT_TRUE(extended.ok()) << extended.status();
  auto extended_face = extended->face();

  uint32_t gid_count_1 = hb_face_get_glyph_count(encoded_face.get());
  uint32_t gid_count_2 = hb_face_get_glyph_count(extended_face.get());

  // ### Phase 2 ###
  encoding->init_font.shallow_copy(*extended);
  extended = Extend(*encoding, {chunk2_cp, chunk3_cp});
  ASSERT_TRUE(extended.ok()) << extended.status();
  extended_face = extended->face();

  uint32_t gid_count_3 = hb_face_get_glyph_count(extended_face.get());

  // ### Checks ###

  // To avoid loca len change the encoder ensures that a full len
  // loca exists in the base font. So gid count should be consistent
  // at each point
  ASSERT_EQ(gid_count_1, gid_count_2);
  ASSERT_EQ(gid_count_2, gid_count_3);

  codepoints = FontHelper::ToCodepointsSet(extended_face.get());
  ASSERT_TRUE(codepoints.contains(chunk0_cp));
  ASSERT_FALSE(codepoints.contains(chunk1_cp));
  ASSERT_TRUE(codepoints.contains(chunk2_cp));
  ASSERT_TRUE(codepoints.contains(chunk3_cp));
  ASSERT_FALSE(codepoints.contains(chunk4_cp));

  ASSERT_TRUE(!FontHelper::GlyfData(extended_face.get(), chunk0_gid)->empty());
  ASSERT_FALSE(!FontHelper::GlyfData(extended_face.get(), chunk1_gid)->empty());
  ASSERT_TRUE(!FontHelper::GlyfData(extended_face.get(), chunk2_gid)->empty());
  ASSERT_TRUE(!FontHelper::GlyfData(extended_face.get(), chunk3_gid)->empty());
  ASSERT_FALSE(!FontHelper::GlyfData(extended_face.get(), chunk4_gid)->empty());
  ASSERT_TRUE(
      !FontHelper::GlyfData(extended_face.get(), gid_count_3 - 1)->empty());
}

TEST_F(IntegrationTest, MixedMode_Complex) {
  Encoder encoder;
  auto init_gids = InitEncoderForMixedMode(encoder);
  ASSERT_TRUE(init_gids.ok()) << init_gids.status();

  auto face = noto_sans_jp_.face();
  auto segment_0 = FontHelper::GidsToUnicodes(face.get(), *init_gids);
  auto segment_1 = FontHelper::GidsToUnicodes(face.get(), TestSegment1());
  auto segment_2 = FontHelper::GidsToUnicodes(face.get(), TestSegment2());
  auto segment_3 = FontHelper::GidsToUnicodes(face.get(), TestSegment3());
  auto segment_4 = FontHelper::GidsToUnicodes(face.get(), TestSegment4());

  // target paritions: {{0}, {1, 2}, {3, 4}}
  auto sc = encoder.SetBaseSubset(segment_0);
  auto segment_1_and_2 = segment_1;
  segment_1_and_2.insert(segment_2.begin(), segment_2.end());
  encoder.AddNonGlyphDataSegment(segment_1_and_2);
  auto segment_3_and_4 = segment_3;
  segment_3_and_4.insert(segment_4.begin(), segment_4.end());
  encoder.AddNonGlyphDataSegment(segment_3_and_4);

  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_1), 1)));
  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_2), 2)));
  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_3), 3)));
  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_4), 4)));
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = encoder.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();
  auto encoded_face = encoding->init_font.face();

  // Phase 1
  auto extended = Extend(*encoding, {chunk1_cp});
  ASSERT_TRUE(extended.ok()) << extended.status();
  auto extended_face = extended->face();

  // Phase 2
  encoding->init_font.shallow_copy(*extended);
  extended = Extend(*encoding, {chunk1_cp, chunk3_cp});
  ASSERT_TRUE(extended.ok()) << extended.status();
  extended_face = extended->face();

  // Check the results
  auto codepoints = FontHelper::ToCodepointsSet(extended_face.get());
  ASSERT_TRUE(codepoints.contains(chunk0_cp));
  ASSERT_TRUE(codepoints.contains(chunk1_cp));
  ASSERT_TRUE(codepoints.contains(chunk2_cp));
  ASSERT_TRUE(codepoints.contains(chunk3_cp));
  ASSERT_TRUE(codepoints.contains(chunk4_cp));

  ASSERT_TRUE(!FontHelper::GlyfData(extended_face.get(), chunk0_gid)->empty());
  ASSERT_TRUE(!FontHelper::GlyfData(extended_face.get(), chunk1_gid)->empty());
  ASSERT_FALSE(!FontHelper::GlyfData(extended_face.get(), chunk2_gid)->empty());
  ASSERT_TRUE(!FontHelper::GlyfData(extended_face.get(), chunk3_gid)->empty());
  ASSERT_FALSE(!FontHelper::GlyfData(extended_face.get(), chunk4_gid)->empty());
}

TEST_F(IntegrationTest, MixedMode_SequentialDependentPatches) {
  Encoder encoder;
  auto init_gids = InitEncoderForMixedMode(encoder);
  ASSERT_TRUE(init_gids.ok()) << init_gids.status();

  auto face = noto_sans_jp_.face();
  auto segment_0 = FontHelper::GidsToUnicodes(face.get(), *init_gids);
  auto segment_1 = FontHelper::GidsToUnicodes(face.get(), TestSegment1());
  auto segment_2 = FontHelper::GidsToUnicodes(face.get(), TestSegment2());
  auto segment_3 = FontHelper::GidsToUnicodes(face.get(), TestSegment3());
  auto segment_4 = FontHelper::GidsToUnicodes(face.get(), TestSegment4());

  // target paritions: {{0, 1}, {2}, {3}, {4}}
  btree_set<uint32_t> segment_0_and_1 = segment_0;
  segment_0_and_1.insert(segment_1.begin(), segment_1.end());
  auto sc = encoder.SetBaseSubset(segment_0_and_1);
  encoder.AddNonGlyphDataSegment(segment_2);
  encoder.AddNonGlyphDataSegment(segment_3);
  encoder.AddNonGlyphDataSegment(segment_4);

  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_2), 2)));
  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_3), 3)));
  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_4), 4)));
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = encoder.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();
  auto encoded_face = encoding->init_font.face();

  auto extended = Extend(*encoding, {chunk3_cp, chunk4_cp});
  ASSERT_TRUE(extended.ok()) << extended.status();
  auto extended_face = extended->face();

  auto codepoints = FontHelper::ToCodepointsSet(extended_face.get());
  ASSERT_TRUE(codepoints.contains(chunk0_cp));
  ASSERT_TRUE(codepoints.contains(chunk1_cp));
  ASSERT_FALSE(codepoints.contains(chunk2_cp));
  ASSERT_TRUE(codepoints.contains(chunk3_cp));
  ASSERT_TRUE(codepoints.contains(chunk4_cp));
}

TEST_F(IntegrationTest, MixedMode_DesignSpaceAugmentation) {
  Encoder encoder;
  auto init_gids = InitEncoderForVfMixedMode(encoder);
  ASSERT_TRUE(init_gids.ok()) << init_gids.status();

  auto face = noto_sans_vf_.face();
  auto segment_0 = FontHelper::GidsToUnicodes(face.get(), *init_gids);
  auto segment_1_gids = TestVfSegment1();
  auto segment_1 = FontHelper::GidsToUnicodes(face.get(), segment_1_gids);
  auto segment_2 = FontHelper::GidsToUnicodes(face.get(), TestVfSegment2());
  auto segment_3 = FontHelper::GidsToUnicodes(face.get(), TestVfSegment3());
  auto segment_4 = FontHelper::GidsToUnicodes(face.get(), TestVfSegment4());

  // target paritions: {0, 1}, {2}, {3, 4} + add wght axis
  SubsetDefinition base_def;
  base_def.codepoints.insert(segment_0.begin(), segment_0.end());
  base_def.codepoints.insert(segment_1.begin(), segment_1.end());
  base_def.design_space = {{kWght, AxisRange::Point(100)}};
  auto sc = encoder.SetBaseSubsetFromDef(base_def);

  encoder.AddNonGlyphDataSegment(segment_2);
  auto segment_3_and_4 = segment_3;
  segment_3_and_4.insert(segment_4.begin(), segment_4.end());
  encoder.AddNonGlyphDataSegment(segment_3_and_4);
  encoder.AddDesignSpaceSegment({{kWght, *AxisRange::Range(100, 900)}});

  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_2), 2)));
  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_3), 3)));
  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_4), 4)));
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = encoder.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();
  auto encoded_face = encoding->init_font.face();

  // Phase 1: non VF augmentation.
  auto extended = Extend(*encoding, {chunk3_cp, chunk4_cp});
  ASSERT_TRUE(extended.ok()) << extended.status();
  auto extended_face = extended->face();

  // Phase 2: VF augmentation.
  encoding->init_font.shallow_copy(*extended);
  extended = ExtendWithDesignSpace(*encoding, {chunk3_cp, chunk4_cp}, {},
                                   {{kWght, *AxisRange::Range(100, 900)}});
  ASSERT_TRUE(extended.ok()) << extended.status();
  extended_face = extended->face();

  ASSERT_TRUE(GvarHasLongOffsets(*extended));
  ASSERT_GT(FontHelper::GvarData(extended_face.get(), chunk0_gid)->size(), 0);
  ASSERT_GT(FontHelper::GvarData(extended_face.get(), chunk1_gid)->size(), 0);
  ASSERT_EQ(FontHelper::GvarData(extended_face.get(), chunk2_gid)->size(), 0);
  ASSERT_GT(FontHelper::GvarData(extended_face.get(), chunk3_gid)->size(), 0);
  ASSERT_GT(FontHelper::GvarData(extended_face.get(), chunk4_gid)->size(), 0);

  auto orig_face = noto_sans_vf_.face();
  // The instancing processes changes some of the flags on the gvar data section
  // so ignore diffs in the first 7 bytes
  ASSERT_TRUE(
      GvarDataMatches(orig_face.get(), extended_face.get(), chunk3_cp, 7));

  // Phase 3: add more codepoints to trigger additional table keyed patch.
  //          should not clobber previously loaded gvar data since we aren't
  //          changing design space.
  encoding->init_font.shallow_copy(*extended);
  extended = ExtendWithDesignSpace(*encoding, {chunk2_cp}, {},
                                   {{kWght, *AxisRange::Range(100, 900)}});
  ASSERT_TRUE(extended.ok()) << extended.status();
  extended_face = extended->face();

  ASSERT_TRUE(GvarHasLongOffsets(*extended));
  ASSERT_GT(FontHelper::GvarData(extended_face.get(), chunk0_gid)->size(), 0);
  ASSERT_GT(FontHelper::GvarData(extended_face.get(), chunk1_gid)->size(), 0);
  ASSERT_GT(FontHelper::GvarData(extended_face.get(), chunk2_gid)->size(), 0);
  ASSERT_GT(FontHelper::GvarData(extended_face.get(), chunk3_gid)->size(), 0);
  ASSERT_GT(FontHelper::GvarData(extended_face.get(), chunk4_gid)->size(), 0);
}

TEST_F(IntegrationTest, MixedMode_DesignSpaceAugmentation_DropsUnusedPatches) {
  Encoder encoder;
  auto init_gids = InitEncoderForVfMixedMode(encoder);
  ASSERT_TRUE(init_gids.ok()) << init_gids.status();

  auto face = noto_sans_vf_.face();
  auto segment_0 = FontHelper::GidsToUnicodes(face.get(), *init_gids);
  auto segment_1 = FontHelper::GidsToUnicodes(face.get(), TestVfSegment1());
  auto segment_2 = FontHelper::GidsToUnicodes(face.get(), TestVfSegment2());
  auto segment_3 = FontHelper::GidsToUnicodes(face.get(), TestVfSegment3());
  auto segment_4 = FontHelper::GidsToUnicodes(face.get(), TestVfSegment4());

  // target paritions: {{0, 1}, {2}, {3, 4}} + add wght axis
  SubsetDefinition base_def;
  base_def.codepoints.insert(segment_0.begin(), segment_0.end());
  base_def.codepoints.insert(segment_1.begin(), segment_1.end());
  base_def.design_space = {{kWght, AxisRange::Point(100)}};
  auto sc = encoder.SetBaseSubsetFromDef(base_def);
  encoder.AddNonGlyphDataSegment(segment_2);
  auto segment_3_and_4 = segment_3;
  segment_3_and_4.insert(segment_4.begin(), segment_4.end());
  encoder.AddNonGlyphDataSegment(segment_3_and_4);
  encoder.AddDesignSpaceSegment({{kWght, *AxisRange::Range(100, 900)}});

  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_2), 2)));
  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_3), 3)));
  sc.Update(encoder.AddGlyphDataPatchCondition(
      Condition::SimpleCondition(SubsetDefinition::Codepoints(segment_4), 4)));

  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = encoder.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();
  auto encoded_face = encoding->init_font.face();

  btree_set<std::string> fetched_uris;
  auto extended = ExtendWithDesignSpace(*encoding, {chunk3_cp, chunk4_cp}, {},
                                        {{kWght, *AxisRange::Range(100, 900)}},
                                        &fetched_uris);

  // correspond to ids 3, 4, 6, d
  btree_set<std::string> expected_uris{"0O.ift_tk",   "1K.ift_tk",
                                       "1_0C.ift_gk", "1_0G.ift_gk",
                                       "2_0C.ift_gk", "2_0G.ift_gk"};

  ASSERT_EQ(fetched_uris, expected_uris);

  // TODO check the patches that were used by looking at ift_extend output
  ASSERT_TRUE(extended.ok()) << extended.status();
  auto extended_face = extended->face();

  ASSERT_GT(FontHelper::GvarData(extended_face.get(), chunk0_gid)->size(), 0);
  ASSERT_GT(FontHelper::GvarData(extended_face.get(), chunk1_gid)->size(), 0);
  ASSERT_EQ(FontHelper::GvarData(extended_face.get(), chunk2_gid)->size(), 0);
  ASSERT_GT(FontHelper::GvarData(extended_face.get(), chunk3_gid)->size(), 0);
  ASSERT_GT(FontHelper::GvarData(extended_face.get(), chunk4_gid)->size(), 0);
}

}  // namespace ift
