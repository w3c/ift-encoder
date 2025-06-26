#include <cstdint>
#include <string>

#include "absl/strings/str_cat.h"
#include "common/axis_range.h"
#include "common/font_data.h"
#include "common/font_helper.h"
#include "common/int_set.h"
#include "common/try.h"
#include "common/woff2.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "hb.h"
#include "ift/client/fontations_client.h"
#include "ift/encoder/compiler.h"
#include "ift/encoder/subset_definition.h"
#include "ift/proto/patch_encoding.h"
#include "ift/proto/patch_map.h"
#include "ift/testdata/test_segments.h"

using absl::btree_set;
using absl::flat_hash_map;
using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using common::AxisRange;
using common::FontData;
using common::FontHelper;
using common::GlyphSet;
using common::hb_blob_unique_ptr;
using common::hb_face_unique_ptr;
using common::IntSet;
using common::make_hb_blob;
using common::make_hb_face;
using common::make_hb_set;
using ift::client::Extend;
using ift::client::ExtendWithDesignSpace;
using ift::encoder::Compiler;
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

    blob = make_hb_blob(
        hb_blob_create_from_file("common/testdata/NotoSansJP-Regular.otf"));
    noto_sans_jp_cff_.set(blob.get());

    blob = make_hb_blob(
        hb_blob_create_from_file("common/testdata/NotoSansJP-VF.subset.otf"));
    noto_sans_jp_cff2_.set(blob.get());

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

  StatusOr<GlyphSet> InitEncoderForMixedMode(Compiler& compiler) {
    auto face = noto_sans_jp_.face();

    GlyphSet init;
    init.insert_range(0, hb_face_get_glyph_count(face.get()) - 1);

    GlyphSet excluded;
    excluded.insert_sorted_array(testdata::TEST_SEGMENT_1);
    excluded.insert_sorted_array(testdata::TEST_SEGMENT_2);
    excluded.insert_sorted_array(testdata::TEST_SEGMENT_3);
    excluded.insert_sorted_array(testdata::TEST_SEGMENT_4);

    init.subtract(excluded);

    compiler.SetFace(face.get());

    auto sc = compiler.AddGlyphDataPatch(0, init);
    sc.Update(compiler.AddGlyphDataPatch(1, TestSegment1()));
    sc.Update(compiler.AddGlyphDataPatch(2, TestSegment2()));
    sc.Update(compiler.AddGlyphDataPatch(3, TestSegment3()));
    sc.Update(compiler.AddGlyphDataPatch(4, TestSegment4()));

    if (!sc.ok()) {
      return sc;
    }

    return init;
  }

  Status InitEncoderForMixedModeCff(Compiler& compiler) {
    auto face = noto_sans_jp_cff_.face();
    compiler.SetFace(face.get());

    auto sc = compiler.AddGlyphDataPatch(1, {34, 35, 46, 47});   // A, B, M, N
    sc.Update(compiler.AddGlyphDataPatch(2, {41, 42, 43, 59}));  // H, I, J, Z

    if (!sc.ok()) {
      return sc;
    }

    return absl::OkStatus();
  }

  Status InitEncoderForMixedModeCff2(Compiler& compiler) {
    auto face = noto_sans_jp_cff2_.face();
    compiler.SetFace(face.get());

    TRYV(compiler.AddGlyphDataPatch(1, {34, 35, 36}));      // A, B, C
    TRYV(compiler.AddGlyphDataPatch(2, {46, 47, 49, 59}));  // M, N, P, Z

    return absl::OkStatus();
  }

  StatusOr<GlyphSet> InitEncoderForVfMixedMode(Compiler& compiler) {
    auto face = noto_sans_vf_.face();
    compiler.SetFace(face.get());

    GlyphSet init;
    init.insert_range(0, hb_face_get_glyph_count(face.get()) - 1);

    GlyphSet excluded;
    excluded.insert_sorted_array(testdata::TEST_VF_SEGMENT_1);
    excluded.insert_sorted_array(testdata::TEST_VF_SEGMENT_2);
    excluded.insert_sorted_array(testdata::TEST_VF_SEGMENT_3);
    excluded.insert_sorted_array(testdata::TEST_VF_SEGMENT_4);

    init.subtract(excluded);

    auto sc = compiler.AddGlyphDataPatch(0, init);
    sc.Update(compiler.AddGlyphDataPatch(1, TestVfSegment1()));
    sc.Update(compiler.AddGlyphDataPatch(2, TestVfSegment2()));
    sc.Update(compiler.AddGlyphDataPatch(3, TestVfSegment3()));
    sc.Update(compiler.AddGlyphDataPatch(4, TestVfSegment4()));
    return init;
  }

  StatusOr<GlyphSet> InitEncoderForMixedModeFeatureTest(Compiler& compiler) {
    auto face = feature_test_.face();
    compiler.SetFace(face.get());

    GlyphSet init;
    init.insert_range(0, hb_face_get_glyph_count(face.get()) - 1);

    GlyphSet excluded;
    excluded.insert_sorted_array(testdata::TEST_FEATURE_SEGMENT_1);
    excluded.insert_sorted_array(testdata::TEST_FEATURE_SEGMENT_2);
    excluded.insert_sorted_array(testdata::TEST_FEATURE_SEGMENT_3);
    excluded.insert_sorted_array(testdata::TEST_FEATURE_SEGMENT_4);
    excluded.insert_sorted_array(testdata::TEST_FEATURE_SEGMENT_5);
    excluded.insert_sorted_array(testdata::TEST_FEATURE_SEGMENT_6);

    init.subtract(excluded);

    auto sc = compiler.AddGlyphDataPatch(0, init);
    sc.Update(compiler.AddGlyphDataPatch(1, TestFeatureSegment1()));
    sc.Update(compiler.AddGlyphDataPatch(2, TestFeatureSegment2()));
    sc.Update(compiler.AddGlyphDataPatch(3, TestFeatureSegment3()));
    sc.Update(compiler.AddGlyphDataPatch(4, TestFeatureSegment4()));
    sc.Update(compiler.AddGlyphDataPatch(5, TestFeatureSegment5()));
    sc.Update(compiler.AddGlyphDataPatch(6, TestFeatureSegment6()));
    return init;
  }

  Status InitEncoderForTableKeyed(Compiler& compiler) {
    auto face = noto_sans_jp_.face();
    compiler.SetFace(face.get());
    return absl::OkStatus();
  }

  Status InitEncoderForVf(Compiler& compiler) {
    auto face = roboto_vf_.face();
    compiler.SetFace(face.get());
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
  FontData noto_sans_jp_cff_;
  FontData noto_sans_jp_cff2_;
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
  Compiler compiler;
  auto sc = InitEncoderForTableKeyed(compiler);
  ASSERT_TRUE(sc.ok()) << sc;

  sc = compiler.SetInitSubset(IntSet{0x41, 0x42, 0x43});
  compiler.AddNonGlyphDataSegment(IntSet{0x45, 0x46, 0x47});
  compiler.AddNonGlyphDataSegment(IntSet{0x48, 0x49, 0x4A});
  compiler.AddNonGlyphDataSegment(IntSet{0x4B, 0x4C, 0x4D});
  compiler.AddNonGlyphDataSegment(IntSet{0x4E, 0x4F, 0x50});
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = compiler.Encode();
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

TEST_F(IntegrationTest, TableKeyed_CodepointsAndFeatureSegment) {
  Compiler compiler;
  auto sc = InitEncoderForVf(compiler);
  ASSERT_TRUE(sc.ok()) << sc;

  sc = compiler.SetInitSubset(IntSet{0x41, 0x42, 0x43});

  SubsetDefinition s1{0x45, 0x46, 0x47};
  s1.feature_tags = {HB_TAG('s', 'm', 'c', 'p')};
  compiler.AddNonGlyphDataSegment(s1);

  SubsetDefinition s2{0x48};
  s2.feature_tags = {HB_TAG('d', 'l', 'i', 'g')};
  compiler.AddNonGlyphDataSegment(s2);

  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = compiler.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();

  auto encoded_face = encoding->init_font.face();
  auto codepoints = FontHelper::ToCodepointsSet(encoded_face.get());
  ASSERT_TRUE(codepoints.contains(0x41));
  ASSERT_FALSE(codepoints.contains(0x45));

  // The entry should trigger on {0x45, 0x46, 0x47} or smcp.
  auto extended =
      ExtendWithDesignSpace(*encoding, {}, {HB_TAG('s', 'm', 'c', 'p')}, {});
  ASSERT_TRUE(extended.ok()) << extended.status();

  auto extended_face = extended->face();
  codepoints = FontHelper::ToCodepointsSet(extended_face.get());
  ASSERT_TRUE(codepoints.contains(0x41));
  ASSERT_TRUE(codepoints.contains(0x45));
  ASSERT_FALSE(codepoints.contains(0x48));
  ASSERT_FALSE(codepoints.contains(0x49));

  auto original_face = noto_sans_jp_.face();
  GlyphDataMatches(original_face.get(), extended_face.get(), 0x41);
  GlyphDataMatches(original_face.get(), extended_face.get(), 0x45);

  // The entry should trigger on {0x48} or dlig.
  encoding->init_font.shallow_copy(*extended);
  extended = ExtendWithDesignSpace(*encoding, {0x48}, {}, {});
  ASSERT_TRUE(extended.ok()) << extended.status();

  extended_face = extended->face();
  codepoints = FontHelper::ToCodepointsSet(extended_face.get());
  ASSERT_TRUE(codepoints.contains(0x41));
  ASSERT_TRUE(codepoints.contains(0x45));
  ASSERT_TRUE(codepoints.contains(0x48));
  ASSERT_FALSE(codepoints.contains(0x49));

  GlyphDataMatches(original_face.get(), extended_face.get(), 0x41);
  GlyphDataMatches(original_face.get(), extended_face.get(), 0x45);
  GlyphDataMatches(original_face.get(), extended_face.get(), 0x48);
}

TEST_F(IntegrationTest, TableKeyedOnly_Woff2Encoded) {
  Compiler compiler;
  auto sc = InitEncoderForTableKeyed(compiler);
  ASSERT_TRUE(sc.ok()) << sc;

  sc = compiler.SetInitSubset(IntSet{0x41, 0x42, 0x43});
  compiler.AddNonGlyphDataSegment(IntSet{0x45, 0x46, 0x47});
  compiler.AddNonGlyphDataSegment(IntSet{0x48, 0x49, 0x4A});
  compiler.AddNonGlyphDataSegment(IntSet{0x4B, 0x4C, 0x4D});
  compiler.AddNonGlyphDataSegment(IntSet{0x4E, 0x4F, 0x50});
  ASSERT_TRUE(sc.ok()) << sc;

  compiler.SetWoff2Encode(true);

  auto encoding = compiler.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();

  auto woff2_decoded = common::Woff2::DecodeWoff2(encoding->init_font.str());
  ASSERT_TRUE(woff2_decoded.ok()) << woff2_decoded.status();
  encoding->init_font = std::move(*woff2_decoded);

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
  Compiler compiler;
  auto sc = InitEncoderForTableKeyed(compiler);
  ASSERT_TRUE(sc.ok()) << sc;

  sc = compiler.SetInitSubset(IntSet{0x41, 0x42, 0x43});
  compiler.AddNonGlyphDataSegment(IntSet{0x45, 0x46, 0x47});
  compiler.AddNonGlyphDataSegment(IntSet{0x48, 0x49, 0x4A});
  compiler.AddNonGlyphDataSegment(IntSet{0x4B, 0x4C, 0x4D});
  compiler.AddNonGlyphDataSegment(IntSet{0x4E, 0x4F, 0x50});
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = compiler.Encode();
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

TEST_F(IntegrationTest, TableKeyed_JumpAheadAndPreloadLists) {
  Compiler compiler;
  auto sc = InitEncoderForTableKeyed(compiler);
  ASSERT_TRUE(sc.ok()) << sc;

  sc = compiler.SetInitSubset(IntSet{0x41, 0x42, 0x43});
  compiler.AddNonGlyphDataSegment(IntSet{0x45, 0x46, 0x47});
  compiler.AddNonGlyphDataSegment(IntSet{0x48, 0x49, 0x4A});
  compiler.AddNonGlyphDataSegment(IntSet{0x4B, 0x4C, 0x4D});
  compiler.AddNonGlyphDataSegment(IntSet{0x4E, 0x4F, 0x50});
  compiler.SetJumpAhead(3);
  compiler.SetUsePrefetchLists(true);
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = compiler.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();

  auto encoded_face = encoding->init_font.face();
  auto codepoints = FontHelper::ToCodepointsSet(encoded_face.get());
  ASSERT_TRUE(codepoints.contains(0x41));
  ASSERT_FALSE(codepoints.contains(0x45));
  ASSERT_FALSE(codepoints.contains(0x48));
  ASSERT_FALSE(codepoints.contains(0x4B));
  ASSERT_FALSE(codepoints.contains(0x4E));

  // With preload lists we will load 3 table keyed patches in parallel in one
  // round trip.
  auto extended = Extend(*encoding, {0x49, 0x4C, 0x4F}, 1, 3);
  ASSERT_TRUE(extended.ok()) << extended.status();
  auto extended_face = extended->face();

  codepoints = FontHelper::ToCodepointsSet(extended_face.get());
  ASSERT_TRUE(codepoints.contains(0x41));
  ASSERT_FALSE(codepoints.contains(0x45));
  ASSERT_TRUE(codepoints.contains(0x48));
  ASSERT_TRUE(codepoints.contains(0x4B));
  ASSERT_TRUE(codepoints.contains(0x4E));

  auto original_face = noto_sans_jp_.face();
  GlyphDataMatches(original_face.get(), extended_face.get(), 0x41);
  GlyphDataMatches(original_face.get(), extended_face.get(), 0x45);
  GlyphDataMatches(original_face.get(), extended_face.get(), 0x48);
  GlyphDataMatches(original_face.get(), extended_face.get(), 0x4B);
  GlyphDataMatches(original_face.get(), extended_face.get(), 0x4E);
}

TEST_F(IntegrationTest, TableKeyedWithOverlaps) {
  Compiler compiler;
  auto sc = InitEncoderForTableKeyed(compiler);
  ASSERT_TRUE(sc.ok()) << sc;

  sc = compiler.SetInitSubset(IntSet{0x41, 0x42, 0x43});
  compiler.AddNonGlyphDataSegment(
      IntSet{0x45, 0x46, 0x47, 0x48});  // 0x48 is in two subsets
  compiler.AddNonGlyphDataSegment(IntSet{0x48, 0x49, 0x4A});
  compiler.AddNonGlyphDataSegment(IntSet{0x4B, 0x4C, 0x4D});
  compiler.AddNonGlyphDataSegment(IntSet{0x4E, 0x4F, 0x50});
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = compiler.Encode();
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
  Compiler compiler;
  auto sc = InitEncoderForVf(compiler);
  ASSERT_TRUE(sc.ok()) << sc;

  SubsetDefinition def{'a', 'b', 'c'};
  def.design_space[kWdth] = AxisRange::Point(100.0f);
  sc = compiler.SetInitSubsetFromDef(def);

  compiler.AddNonGlyphDataSegment(IntSet{'d', 'e', 'f'});
  compiler.AddNonGlyphDataSegment(IntSet{'h', 'i', 'j'});
  compiler.AddDesignSpaceSegment({{kWdth, *AxisRange::Range(75.0f, 100.0f)}});
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = compiler.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();
  auto encoded_face = encoding->init_font.face();

  auto codepoints = FontHelper::ToCodepointsSet(encoded_face.get());
  btree_set<uint32_t> codepoints_btree;
  codepoints_btree.insert(codepoints.begin(), codepoints.end());
  ASSERT_THAT(codepoints_btree, IsSupersetOf({'a', 'b', 'c'}));
  ASSERT_THAT(codepoints_btree, AllOf(Not(Contains('d')), Not(Contains('e')),
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
  codepoints_btree.clear();
  codepoints_btree.insert(codepoints.begin(), codepoints.end());
  ASSERT_THAT(codepoints_btree, IsSupersetOf({'a', 'b', 'c', 'd', 'e', 'f'}));
  ASSERT_THAT(codepoints_btree, AllOf(Not(Contains('h')), Not(Contains('i')),
                                      Not(Contains('j'))));
}

TEST_F(IntegrationTest, SharedBrotli_DesignSpaceAugmentation) {
  Compiler compiler;
  auto sc = InitEncoderForVf(compiler);
  ASSERT_TRUE(sc.ok()) << sc;

  SubsetDefinition def{'a', 'b', 'c'};
  def.design_space[kWdth] = AxisRange::Point(100.0f);
  sc = compiler.SetInitSubsetFromDef(def);

  compiler.AddNonGlyphDataSegment(IntSet{'d', 'e', 'f'});
  compiler.AddNonGlyphDataSegment(IntSet{'h', 'i', 'j'});
  compiler.AddDesignSpaceSegment({{kWdth, *AxisRange::Range(75.0f, 100.0f)}});
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = compiler.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();
  auto encoded_face = encoding->init_font.face();

  auto codepoints = FontHelper::ToCodepointsSet(encoded_face.get());
  btree_set<uint32_t> codepoints_btree;
  codepoints_btree.insert(codepoints.begin(), codepoints.end());
  ASSERT_THAT(codepoints_btree, IsSupersetOf({'a', 'b', 'c'}));
  ASSERT_THAT(codepoints_btree, AllOf(Not(Contains('d')), Not(Contains('e')),
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
  codepoints_btree.clear();
  codepoints_btree.insert(codepoints.begin(), codepoints.end());
  ASSERT_THAT(codepoints_btree, IsSupersetOf({'a', 'b', 'c'}));
  ASSERT_THAT(codepoints_btree, AllOf(Not(Contains('d')), Not(Contains('e')),
                                      Not(Contains('f')), Not(Contains('h')),
                                      Not(Contains('i')), Not(Contains('j'))));

  // Try extending the updated font again.
  encoding->init_font.shallow_copy(*extended);
  extended = Extend(*encoding, {'e'});
  ASSERT_TRUE(extended.ok()) << extended.status();
  extended_face = extended->face();

  codepoints = FontHelper::ToCodepointsSet(extended_face.get());
  codepoints_btree.clear();
  codepoints_btree.insert(codepoints.begin(), codepoints.end());
  ASSERT_THAT(codepoints_btree, IsSupersetOf({'a', 'b', 'c', 'd', 'e', 'f'}));

  ds = FontHelper::GetDesignSpace(extended_face.get());
  expected_ds = {
      {kWght, *AxisRange::Range(100, 900)},
      {kWdth, *AxisRange::Range(75, 100)},
  };
  ASSERT_EQ(*ds, expected_ds);
}

TEST_F(IntegrationTest, MixedMode) {
  Compiler compiler;
  auto init_gids = InitEncoderForMixedMode(compiler);
  ASSERT_TRUE(init_gids.ok()) << init_gids.status();

  auto face = noto_sans_jp_.face();
  auto base_segment = FontHelper::GidsToUnicodes(face.get(), *init_gids);

  // target paritions: {{0, 1}, {2}, {3, 4}}
  auto segment_0 = FontHelper::GidsToUnicodes(face.get(), *init_gids);
  auto segment_1 = FontHelper::GidsToUnicodes(face.get(), TestSegment1());
  auto segment_2 = FontHelper::GidsToUnicodes(face.get(), TestSegment2());
  auto segment_3 = FontHelper::GidsToUnicodes(face.get(), TestSegment3());
  auto segment_4 = FontHelper::GidsToUnicodes(face.get(), TestSegment4());

  IntSet base;
  base.insert(segment_0.begin(), segment_0.end());
  base.insert(segment_1.begin(), segment_1.end());
  auto sc = compiler.SetInitSubset(base);

  compiler.AddNonGlyphDataSegment(segment_2);

  auto segment = segment_3;
  segment.insert(segment_4.begin(), segment_4.end());
  compiler.AddNonGlyphDataSegment(segment);
  ASSERT_TRUE(sc.ok()) << sc;

  // Setup activations for 2 through 4 (1 is init)
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_2, 2, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_3, 3, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_4, 4, PatchEncoding::GLYPH_KEYED)));

  auto encoding = compiler.Encode();
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

TEST_F(IntegrationTest, MixedMode_Woff2Encoded) {
  Compiler compiler;
  auto init_gids = InitEncoderForMixedMode(compiler);
  ASSERT_TRUE(init_gids.ok()) << init_gids.status();

  auto face = noto_sans_jp_.face();
  auto base_segment = FontHelper::GidsToUnicodes(face.get(), *init_gids);

  // target paritions: {{0, 1}, {2}, {3, 4}}
  auto segment_0 = FontHelper::GidsToUnicodes(face.get(), *init_gids);
  auto segment_1 = FontHelper::GidsToUnicodes(face.get(), TestSegment1());
  auto segment_2 = FontHelper::GidsToUnicodes(face.get(), TestSegment2());
  auto segment_3 = FontHelper::GidsToUnicodes(face.get(), TestSegment3());
  auto segment_4 = FontHelper::GidsToUnicodes(face.get(), TestSegment4());

  IntSet base;
  base.insert(segment_0.begin(), segment_0.end());
  base.insert(segment_1.begin(), segment_1.end());
  auto sc = compiler.SetInitSubset(base);

  compiler.AddNonGlyphDataSegment(segment_2);

  auto segment = segment_3;
  segment.insert(segment_4.begin(), segment_4.end());
  compiler.AddNonGlyphDataSegment(segment);
  ASSERT_TRUE(sc.ok()) << sc;

  // Setup activations for 2 through 4 (1 is init)
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_2, 2, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_3, 3, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_4, 4, PatchEncoding::GLYPH_KEYED)));

  compiler.SetWoff2Encode(true);

  auto encoding = compiler.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();

  auto woff2_decoded = common::Woff2::DecodeWoff2(encoding->init_font.str());
  ASSERT_TRUE(woff2_decoded.ok()) << woff2_decoded.status();
  encoding->init_font = std::move(*woff2_decoded);
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
  Compiler compiler;
  auto init_gids = InitEncoderForMixedModeFeatureTest(compiler);
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

  auto sc = compiler.SetInitSubset(segment_0);

  compiler.AddNonGlyphDataSegment(segment_1);
  compiler.AddNonGlyphDataSegment(segment_2);
  compiler.AddNonGlyphDataSegment(segment_3);
  compiler.AddNonGlyphDataSegment(segment_4);

  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_1, 1, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_2, 2, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_3, 3, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_4, 4, PatchEncoding::GLYPH_KEYED)));

  {
    PatchMap::Entry entry;
    entry.coverage.child_indices = {0};
    entry.coverage.features = {kVrt3};
    entry.patch_indices.push_back(5);
    entry.encoding = PatchEncoding::GLYPH_KEYED;
    sc.Update(compiler.AddGlyphDataPatchCondition(entry));
  }

  {
    PatchMap::Entry entry;
    entry.coverage.child_indices = {1};
    entry.coverage.features = {kVrt3};
    entry.patch_indices.push_back(5);
    entry.encoding = PatchEncoding::GLYPH_KEYED;
    sc.Update(compiler.AddGlyphDataPatchCondition(entry));
  }

  {
    PatchMap::Entry entry;
    entry.coverage.child_indices = {3};
    entry.coverage.features = {kVrt3};
    entry.patch_indices.push_back(6);
    entry.encoding = PatchEncoding::GLYPH_KEYED;
    sc.Update(compiler.AddGlyphDataPatchCondition(entry));
  }

  compiler.AddFeatureGroupSegment({kVrt3});
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = compiler.Encode();
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
  Compiler compiler;
  auto init_gids = InitEncoderForMixedMode(compiler);
  ASSERT_TRUE(init_gids.ok()) << init_gids.status();

  auto face = noto_sans_jp_.face();
  auto segment_1 = FontHelper::GidsToUnicodes(face.get(), TestSegment1());
  auto segment_2 = FontHelper::GidsToUnicodes(face.get(), TestSegment2());
  auto segment_3 = FontHelper::GidsToUnicodes(face.get(), TestSegment3());
  auto segment_4 = FontHelper::GidsToUnicodes(face.get(), TestSegment4());
  IntSet all;
  all.insert(segment_1.begin(), segment_1.end());
  all.insert(segment_2.begin(), segment_2.end());
  all.insert(segment_3.begin(), segment_3.end());
  all.insert(segment_4.begin(), segment_4.end());

  // target paritions: {}, {{1}, {2}, {3, 4}}
  auto sc = compiler.SetInitSubset(IntSet{});
  compiler.AddNonGlyphDataSegment(all);
  ASSERT_TRUE(sc.ok()) << sc;

  // Setup some composite activation conditions
  {
    // 0
    PatchMap::Entry entry;
    entry.coverage.codepoints = segment_1;
    entry.patch_indices.push_back(0);
    entry.ignored = true;
    entry.encoding = PatchEncoding::GLYPH_KEYED;
    sc.Update(compiler.AddGlyphDataPatchCondition(entry));
  }

  {
    // 1
    PatchMap::Entry entry;
    entry.coverage.codepoints = segment_2;
    entry.patch_indices.push_back(0);
    entry.ignored = true;
    entry.encoding = PatchEncoding::GLYPH_KEYED;
    sc.Update(compiler.AddGlyphDataPatchCondition(entry));
  }

  {
    // 2
    PatchMap::Entry entry;
    entry.coverage.codepoints = segment_3;
    entry.patch_indices.push_back(0);
    entry.ignored = true;
    entry.encoding = PatchEncoding::GLYPH_KEYED;
    sc.Update(compiler.AddGlyphDataPatchCondition(entry));
  }

  {
    // 3
    PatchMap::Entry entry;
    entry.coverage.conjunctive = false;
    entry.coverage.child_indices = {0, 1};  // (1 OR 2)
    entry.patch_indices.push_back(0);
    entry.ignored = true;
    entry.encoding = PatchEncoding::GLYPH_KEYED;
    sc.Update(compiler.AddGlyphDataPatchCondition(entry));
  }

  {
    // 4
    PatchMap::Entry entry;
    entry.coverage.conjunctive = true;
    entry.coverage.child_indices = {3, 2};  // (1 OR 2) AND 3
    entry.patch_indices.push_back(4);
    entry.encoding = PatchEncoding::GLYPH_KEYED;
    sc.Update(compiler.AddGlyphDataPatchCondition(entry));
  }

  {
    // 5
    PatchMap::Entry entry;
    entry.coverage.conjunctive = false;
    entry.coverage.child_indices = {1, 2};  // (2 OR 3)
    entry.patch_indices.push_back(0);
    entry.ignored = true;
    entry.encoding = PatchEncoding::GLYPH_KEYED;
    sc.Update(compiler.AddGlyphDataPatchCondition(entry));
  }

  {
    // 6
    PatchMap::Entry entry;
    entry.coverage.child_indices = {0, 5};  // 1 AND (2 OR 3)
    entry.coverage.conjunctive = true;
    entry.patch_indices.push_back(3);
    entry.encoding = PatchEncoding::GLYPH_KEYED;
    sc.Update(compiler.AddGlyphDataPatchCondition(entry));
  }

  auto encoding = compiler.Encode();
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
  Compiler compiler;
  auto init_gids = InitEncoderForMixedMode(compiler);
  ASSERT_TRUE(init_gids.ok()) << init_gids.status();

  auto face = noto_sans_jp_.face();
  auto segment_0 = FontHelper::GidsToUnicodes(face.get(), *init_gids);
  auto segment_1 = FontHelper::GidsToUnicodes(face.get(), TestSegment1());
  auto segment_2 = FontHelper::GidsToUnicodes(face.get(), TestSegment2());
  auto segment_3 = FontHelper::GidsToUnicodes(face.get(), TestSegment3());
  auto segment_4 = FontHelper::GidsToUnicodes(face.get(), TestSegment4());

  // target paritions: {{0}, {1}, {2}, {3}, {4}}
  auto sc = compiler.SetInitSubset(segment_0);
  compiler.AddNonGlyphDataSegment(segment_1);
  compiler.AddNonGlyphDataSegment(segment_2);
  compiler.AddNonGlyphDataSegment(segment_3);
  compiler.AddNonGlyphDataSegment(segment_4);

  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_1, 1, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_2, 2, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_3, 3, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_4, 4, PatchEncoding::GLYPH_KEYED)));

  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = compiler.Encode();
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
  Compiler compiler;
  auto init_gids = InitEncoderForMixedMode(compiler);
  ASSERT_TRUE(init_gids.ok()) << init_gids.status();

  auto face = noto_sans_jp_.face();
  auto segment_0 = FontHelper::GidsToUnicodes(face.get(), *init_gids);
  auto segment_1 = FontHelper::GidsToUnicodes(face.get(), TestSegment1());
  auto segment_2 = FontHelper::GidsToUnicodes(face.get(), TestSegment2());
  auto segment_3 = FontHelper::GidsToUnicodes(face.get(), TestSegment3());
  auto segment_4 = FontHelper::GidsToUnicodes(face.get(), TestSegment4());

  // target paritions: {{0}, {1, 2}, {3, 4}}
  auto sc = compiler.SetInitSubset(segment_0);
  auto segment_1_and_2 = segment_1;
  segment_1_and_2.insert(segment_2.begin(), segment_2.end());
  compiler.AddNonGlyphDataSegment(segment_1_and_2);
  auto segment_3_and_4 = segment_3;
  segment_3_and_4.insert(segment_4.begin(), segment_4.end());
  compiler.AddNonGlyphDataSegment(segment_3_and_4);

  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_1, 1, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_2, 2, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_3, 3, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_4, 4, PatchEncoding::GLYPH_KEYED)));
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = compiler.Encode();
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
  Compiler compiler;
  auto init_gids = InitEncoderForMixedMode(compiler);
  ASSERT_TRUE(init_gids.ok()) << init_gids.status();

  auto face = noto_sans_jp_.face();
  auto segment_0 = FontHelper::GidsToUnicodes(face.get(), *init_gids);
  auto segment_1 = FontHelper::GidsToUnicodes(face.get(), TestSegment1());
  auto segment_2 = FontHelper::GidsToUnicodes(face.get(), TestSegment2());
  auto segment_3 = FontHelper::GidsToUnicodes(face.get(), TestSegment3());
  auto segment_4 = FontHelper::GidsToUnicodes(face.get(), TestSegment4());

  // target paritions: {{0, 1}, {2}, {3}, {4}}
  IntSet segment_0_and_1 = segment_0;
  segment_0_and_1.union_set(segment_1);
  auto sc = compiler.SetInitSubset(segment_0_and_1);
  compiler.AddNonGlyphDataSegment(segment_2);
  compiler.AddNonGlyphDataSegment(segment_3);
  compiler.AddNonGlyphDataSegment(segment_4);

  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_2, 2, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_3, 3, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_4, 4, PatchEncoding::GLYPH_KEYED)));
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = compiler.Encode();
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
  Compiler compiler;
  auto init_gids = InitEncoderForVfMixedMode(compiler);
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
  auto sc = compiler.SetInitSubsetFromDef(base_def);

  compiler.AddNonGlyphDataSegment(segment_2);
  auto segment_3_and_4 = segment_3;
  segment_3_and_4.insert(segment_4.begin(), segment_4.end());
  compiler.AddNonGlyphDataSegment(segment_3_and_4);
  compiler.AddDesignSpaceSegment({{kWght, *AxisRange::Range(100, 900)}});

  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_2, 2, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_3, 3, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_4, 4, PatchEncoding::GLYPH_KEYED)));
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = compiler.Encode();
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

TEST_F(IntegrationTest,
       MixedMode_DesignSpaceAugmentation_UsesFullInvalidation) {
  Compiler compiler;
  auto init_gids = InitEncoderForVfMixedMode(compiler);
  compiler.SetJumpAhead(2);
  compiler.SetUsePrefetchLists(false);
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
  auto sc = compiler.SetInitSubsetFromDef(base_def);

  compiler.AddNonGlyphDataSegment(segment_2);
  auto segment_3_and_4 = segment_3;
  segment_3_and_4.insert(segment_4.begin(), segment_4.end());
  compiler.AddNonGlyphDataSegment(segment_3_and_4);
  compiler.AddDesignSpaceSegment({{kWght, *AxisRange::Range(100, 900)}});

  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_2, 2, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_3, 3, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_4, 4, PatchEncoding::GLYPH_KEYED)));
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = compiler.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();
  auto encoded_face = encoding->init_font.face();

  // Request codepoint + axis augmentation
  btree_set<std::string> fetched_uris;
  auto extended = ExtendWithDesignSpace(
      *encoding, {chunk3_cp}, {}, {{kWght, *AxisRange::Range(100, 900)}},
      // only two patches and round trips should be needed: one to extend the
      // design space and a second to add glyph data
      &fetched_uris, 2, 2);
  ASSERT_TRUE(extended.ok()) << extended.status();
  auto extended_face = extended->face();

  auto expected_uris = btree_set<std::string>{"18.ift_tk", "2_0C.ift_gk"};
  ASSERT_EQ(fetched_uris, expected_uris);

  ASSERT_TRUE(GvarHasLongOffsets(*extended));
  ASSERT_GT(FontHelper::GvarData(extended_face.get(), chunk0_gid)->size(), 0);
  ASSERT_GT(FontHelper::GvarData(extended_face.get(), chunk1_gid)->size(), 0);
  ASSERT_EQ(FontHelper::GvarData(extended_face.get(), chunk2_gid)->size(), 0);
  ASSERT_GT(FontHelper::GvarData(extended_face.get(), chunk3_gid)->size(), 0);
  ASSERT_EQ(FontHelper::GvarData(extended_face.get(), chunk4_gid)->size(), 0);

  auto orig_face = noto_sans_vf_.face();
  // The instancing processes changes some of the flags on the gvar data section
  // so ignore diffs in the first 7 bytes
  ASSERT_TRUE(
      GvarDataMatches(orig_face.get(), extended_face.get(), chunk3_cp, 7));
}

TEST_F(IntegrationTest,
       MixedMode_DesignSpaceAugmentation_UsesFullInvalidationWithPreloadLists) {
  Compiler compiler;
  auto init_gids = InitEncoderForVfMixedMode(compiler);
  compiler.SetJumpAhead(2);
  compiler.SetUsePrefetchLists(true);
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
  auto sc = compiler.SetInitSubsetFromDef(base_def);

  compiler.AddNonGlyphDataSegment(segment_2);
  auto segment_3_and_4 = segment_3;
  segment_3_and_4.insert(segment_4.begin(), segment_4.end());
  compiler.AddNonGlyphDataSegment(segment_3_and_4);
  compiler.AddDesignSpaceSegment({{kWght, *AxisRange::Range(100, 900)}});

  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_2, 2, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_3, 3, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_4, 4, PatchEncoding::GLYPH_KEYED)));
  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = compiler.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();
  auto encoded_face = encoding->init_font.face();

  // Request codepoint + axis augmentation
  btree_set<std::string> fetched_uris;
  auto extended = ExtendWithDesignSpace(
      *encoding, {chunk3_cp}, {}, {{kWght, *AxisRange::Range(100, 900)}},
      // only three patches and round two trips should be needed:
      // one trip to extend the design space (split into 2 patches w/ preload)
      // and a second to add glyph data
      &fetched_uris, 2, 3);
  ASSERT_TRUE(extended.ok()) << extended.status();
  auto extended_face = extended->face();

  auto expected_uris =
      btree_set<std::string>{"0O.ift_tk", "18.ift_tk", "2_0C.ift_gk"};
  ASSERT_EQ(fetched_uris, expected_uris);

  ASSERT_TRUE(GvarHasLongOffsets(*extended));
  ASSERT_GT(FontHelper::GvarData(extended_face.get(), chunk0_gid)->size(), 0);
  ASSERT_GT(FontHelper::GvarData(extended_face.get(), chunk1_gid)->size(), 0);
  ASSERT_EQ(FontHelper::GvarData(extended_face.get(), chunk2_gid)->size(), 0);
  ASSERT_GT(FontHelper::GvarData(extended_face.get(), chunk3_gid)->size(), 0);
  ASSERT_EQ(FontHelper::GvarData(extended_face.get(), chunk4_gid)->size(), 0);

  auto orig_face = noto_sans_vf_.face();
  // The instancing processes changes some of the flags on the gvar data section
  // so ignore diffs in the first 7 bytes
  ASSERT_TRUE(
      GvarDataMatches(orig_face.get(), extended_face.get(), chunk3_cp, 7));
}

TEST_F(IntegrationTest, MixedMode_DesignSpaceAugmentation_DropsUnusedPatches) {
  Compiler compiler;
  auto init_gids = InitEncoderForVfMixedMode(compiler);
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
  auto sc = compiler.SetInitSubsetFromDef(base_def);
  compiler.AddNonGlyphDataSegment(segment_2);
  auto segment_3_and_4 = segment_3;
  segment_3_and_4.insert(segment_4.begin(), segment_4.end());
  compiler.AddNonGlyphDataSegment(segment_3_and_4);
  compiler.AddDesignSpaceSegment({{kWght, *AxisRange::Range(100, 900)}});

  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_2, 2, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_3, 3, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_4, 4, PatchEncoding::GLYPH_KEYED)));

  ASSERT_TRUE(sc.ok()) << sc;

  auto encoding = compiler.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();
  auto encoded_face = encoding->init_font.face();

  btree_set<std::string> fetched_uris;
  auto extended = ExtendWithDesignSpace(*encoding, {chunk3_cp, chunk4_cp}, {},
                                        {{kWght, *AxisRange::Range(100, 900)}},
                                        &fetched_uris);

  // correspond to ids 3, 4, 6, d
  btree_set<std::string> expected_uris{"0S.ift_tk", "20.ift_tk", "2_0C.ift_gk",
                                       "2_0G.ift_gk"};

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

StatusOr<FontData> desubroutinize(hb_face_t* font) {
  hb_subset_input_t* input = hb_subset_input_create_or_fail();
  if (!input) {
    return absl::InternalError("failed to create subset input.");
  }

  hb_subset_input_keep_everything(input);
  hb_subset_input_set_flags(
      input, hb_subset_input_get_flags(input) | HB_SUBSET_FLAGS_DESUBROUTINIZE);

  hb_face_t* subset = hb_subset_or_fail(font, input);
  hb_subset_input_destroy(input);

  if (!subset) {
    return absl::InternalError("subset operation failed.");
  }

  FontData result(subset);
  hb_face_destroy(subset);

  return result;
}

TEST_F(IntegrationTest, MixedMode_Cff) {
  Compiler compiler;
  auto sc = InitEncoderForMixedModeCff(compiler);
  ASSERT_TRUE(sc.ok()) << sc;

  ASSERT_TRUE(compiler.SetInitSubset(IntSet{}).ok());

  IntSet all_codepoints{'A', 'B', 'H', 'I', 'J', 'M', 'N', 'Z'};
  auto face = noto_sans_jp_cff_.face();
  compiler.AddNonGlyphDataSegment(all_codepoints);

  // Setup activations for patches 1 and 2
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry({'A', 'B', 'M', 'N'}, 1, PatchEncoding::GLYPH_KEYED)));
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry({'H', 'I', 'J', 'Z'}, 2, PatchEncoding::GLYPH_KEYED)));

  auto encoding = compiler.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();
  auto encoded_face = encoding->init_font.face();

  ASSERT_EQ(FontHelper::CffData(encoded_face.get(), 34).size(),
            1);  // empty glyphs in cff are one byte long
  ASSERT_EQ(FontHelper::CffData(encoded_face.get(), 43).size(),
            1);  // empty glyphs in cff are one byte long

  auto codepoints = FontHelper::ToCodepointsSet(encoded_face.get());
  ASSERT_TRUE(codepoints.empty());

  auto extended = Extend(*encoding, {'M'});
  ASSERT_TRUE(extended.ok()) << extended.status();
  auto extended_face = extended->face();

  // The encoder desubroutinizes CFF fonts, so generate a desubroutinized
  // copy of the input face to use for comparisons.
  auto desubroutinized = desubroutinize(face.get());
  ASSERT_TRUE(desubroutinized.ok()) << desubroutinized.status();
  auto desubroutinized_face = desubroutinized->face();

  codepoints = FontHelper::ToCodepointsSet(extended_face.get());
  ASSERT_EQ(codepoints, all_codepoints);

  // patch 2 gids not present
  ASSERT_EQ(FontHelper::CffData(extended_face.get(), 43).size(),
            1);  // empty glyphs in cff are one byte long

  // patch 1 gids present and match the desubroutinized face.
  ASSERT_EQ(FontHelper::CffData(extended_face.get(), 34).span(),
            FontHelper::CffData(desubroutinized_face.get(), 34).span());

  // Second extension
  encoding->init_font.shallow_copy(*extended);
  extended = Extend(*encoding, {'H'});
  ASSERT_TRUE(extended.ok()) << extended.status();
  extended_face = extended->face();

  ASSERT_GT(FontHelper::CffData(extended_face.get(), 43).size(), 1);
  ASSERT_EQ(FontHelper::CffData(extended_face.get(), 43).span(),
            FontHelper::CffData(desubroutinized_face.get(), 43).span());
  ASSERT_EQ(FontHelper::CffData(extended_face.get(), 34).span(),
            FontHelper::CffData(desubroutinized_face.get(), 34).span());
}

TEST_F(IntegrationTest, MixedMode_Cff2) {
  Compiler compiler;
  auto sc = InitEncoderForMixedModeCff2(compiler);
  ASSERT_TRUE(sc.ok()) << sc;

  ASSERT_TRUE(compiler.SetInitSubset(IntSet{}).ok());

  IntSet all_codepoints{'A', 'B', 'C', 'M', 'N', 'P', 'Z'};
  auto face = noto_sans_jp_cff2_.face();
  compiler.AddNonGlyphDataSegment(all_codepoints);

  // Setup activations for patches 1 and 2
  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry({'A', 'B', 'C'}, 1, PatchEncoding::GLYPH_KEYED)));

  sc.Update(compiler.AddGlyphDataPatchCondition(
      PatchMap::Entry({'M', 'N', 'P', 'Z'}, 2, PatchEncoding::GLYPH_KEYED)));

  auto encoding = compiler.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();
  auto encoded_face = encoding->init_font.face();

  ASSERT_TRUE(FontHelper::Cff2Data(encoded_face.get(), 34).empty());
  ASSERT_TRUE(FontHelper::Cff2Data(encoded_face.get(), 35).empty());
  ASSERT_TRUE(FontHelper::Cff2Data(encoded_face.get(), 47).empty());
  ASSERT_TRUE(FontHelper::Cff2Data(encoded_face.get(), 49).empty());

  auto codepoints = FontHelper::ToCodepointsSet(encoded_face.get());
  // Last gid (Z) is always included in initial font to force correct glyph
  // count in CFF/CFF2.
  ASSERT_EQ(codepoints, IntSet{'Z'});

  auto extended = Extend(*encoding, {'B'});
  ASSERT_TRUE(extended.ok()) << extended.status();
  auto extended_face = extended->face();

  // The encoder desubroutinizes CFF fonts, so generate a desubroutinized
  // copy of the input face to use for comparisons.
  auto desubroutinized = desubroutinize(face.get());
  ASSERT_TRUE(desubroutinized.ok()) << desubroutinized.status();
  auto desubroutinized_face = desubroutinized->face();

  codepoints = FontHelper::ToCodepointsSet(extended_face.get());
  ASSERT_EQ(codepoints, all_codepoints);

  // patch 2 gids not present
  ASSERT_TRUE(FontHelper::Cff2Data(extended_face.get(), 47).empty());
  ASSERT_TRUE(FontHelper::Cff2Data(extended_face.get(), 49).empty());

  // patch 1 gids present and match the desubroutinized face.
  ASSERT_FALSE(FontHelper::Cff2Data(extended_face.get(), 34).empty());
  ASSERT_EQ(FontHelper::Cff2Data(extended_face.get(), 34).span(),
            FontHelper::Cff2Data(desubroutinized_face.get(), 34).span());

  ASSERT_FALSE(FontHelper::Cff2Data(extended_face.get(), 35).empty());
  ASSERT_EQ(FontHelper::Cff2Data(extended_face.get(), 35).span(),
            FontHelper::Cff2Data(desubroutinized_face.get(), 35).span());

  // Second extension
  encoding->init_font.shallow_copy(*extended);
  extended = Extend(*encoding, {'P'});
  ASSERT_TRUE(extended.ok()) << extended.status();
  extended_face = extended->face();

  ASSERT_FALSE(FontHelper::Cff2Data(extended_face.get(), 47).empty());
  ASSERT_EQ(FontHelper::Cff2Data(extended_face.get(), 47).span(),
            FontHelper::Cff2Data(desubroutinized_face.get(), 47).span());

  ASSERT_FALSE(FontHelper::Cff2Data(extended_face.get(), 35).empty());
  ASSERT_EQ(FontHelper::Cff2Data(extended_face.get(), 35).span(),
            FontHelper::Cff2Data(desubroutinized_face.get(), 35).span());
}

}  // namespace ift
