#include "ift/encoder/encoder.h"

#include <stdlib.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "common/axis_range.h"
#include "common/binary_patch.h"
#include "common/brotli_binary_patch.h"
#include "common/font_data.h"
#include "common/font_helper.h"
#include "common/int_set.h"
#include "gtest/gtest.h"
#include "ift/client/fontations_client.h"
#include "ift/encoder/subset_definition.h"
#include "ift/proto/ift_table.h"
#include "ift/proto/patch_encoding.h"
#include "ift/proto/patch_map.h"
#include "ift/testdata/test_segments.h"

using absl::btree_map;
using absl::btree_set;
using absl::flat_hash_map;
using absl::Span;
using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using absl::string_view;
using common::AxisRange;
using common::BinaryPatch;
using common::BrotliBinaryPatch;
using common::CodepointSet;
using common::FontData;
using common::FontHelper;
using common::GlyphSet;
using common::IntSet;
using common::make_hb_set;
using ift::client::ToGraph;
using ift::proto::DEFAULT_ENCODING;
using ift::proto::GLYPH_KEYED;
using ift::proto::IFTTable;
using ift::proto::PatchEncoding;
using ift::proto::PatchMap;
using ift::proto::TABLE_KEYED_FULL;
using ift::proto::TABLE_KEYED_PARTIAL;
using ift::testdata::TestSegment1;
using ift::testdata::TestSegment2;
using ift::testdata::TestSegment3;
using ift::testdata::TestSegment4;

namespace ift::encoder {

typedef btree_map<std::string, btree_set<std::string>> graph;
constexpr hb_tag_t kWght = HB_TAG('w', 'g', 'h', 't');
constexpr hb_tag_t kWdth = HB_TAG('w', 'd', 't', 'h');

class EncoderTest : public ::testing::Test {
 protected:
  EncoderTest() {
    font = from_file("common/testdata/Roboto-Regular.abcd.ttf");
    full_font = from_file("common/testdata/Roboto-Regular.ttf");
    woff2_font = from_file("common/testdata/Roboto-Regular.abcd.woff2");
    vf_font = from_file("common/testdata/Roboto[wdth,wght].ttf");
    noto_sans_jp = from_file("ift/testdata/NotoSansJP-Regular.subset.ttf");

    auto face = noto_sans_jp.face();
    GlyphSet init;
    init.insert_range(0, hb_face_get_glyph_count(face.get()) - 1);

    GlyphSet excluded;
    excluded.insert_sorted_array(testdata::TEST_SEGMENT_1);
    excluded.insert_sorted_array(testdata::TEST_SEGMENT_2);
    excluded.insert_sorted_array(testdata::TEST_SEGMENT_3);
    excluded.insert_sorted_array(testdata::TEST_SEGMENT_4);

    init.subtract(excluded);

    segment_0_gids = init;
    segment_1_gids = TestSegment1();
    segment_2_gids = TestSegment2();
    segment_3_gids = TestSegment3();
    segment_4_gids = TestSegment4();

    segment_0_cps = FontHelper::GidsToUnicodes(face.get(), segment_0_gids);
    segment_1_cps = FontHelper::GidsToUnicodes(face.get(), segment_1_gids);
    segment_2_cps = FontHelper::GidsToUnicodes(face.get(), segment_2_gids);
    segment_3_cps = FontHelper::GidsToUnicodes(face.get(), segment_3_gids);
    segment_4_cps = FontHelper::GidsToUnicodes(face.get(), segment_4_gids);
  }

  FontData font;
  FontData full_font;
  FontData woff2_font;
  FontData vf_font;
  FontData noto_sans_jp;

  GlyphSet segment_0_gids;
  GlyphSet segment_1_gids;
  GlyphSet segment_2_gids;
  GlyphSet segment_3_gids;
  GlyphSet segment_4_gids;

  CodepointSet segment_0_cps;
  CodepointSet segment_1_cps;
  CodepointSet segment_2_cps;
  CodepointSet segment_3_cps;
  CodepointSet segment_4_cps;

  uint32_t chunk0_cp = 0x47;
  uint32_t chunk1_cp = 0xb7;
  uint32_t chunk2_cp = 0xb2;
  uint32_t chunk3_cp = 0xeb;
  uint32_t chunk4_cp = 0xa8;

  FontData from_file(const char* filename) {
    hb_blob_t* blob = hb_blob_create_from_file_or_fail(filename);
    if (!blob) {
      assert(false);
    }
    FontData result(blob);
    hb_blob_destroy(blob);
    return result;
  }

  std::string GetVarInfo(const FontData& font_data) {
    auto face = font_data.face();
    constexpr uint32_t max_axes = 5;
    hb_ot_var_axis_info_t info[max_axes];

    uint32_t count = max_axes;
    hb_ot_var_get_axis_infos(face.get(), 0, &count, info);

    std::string result = "";
    bool first = true;
    for (uint32_t i = 0; i < count; i++) {
      std::string tag = FontHelper::ToString(info[i].tag);
      float min = info[i].min_value;
      float max = info[i].max_value;
      if (!first) {
        result = StrCat(result, ";");
      }
      first = false;
      result = StrCat(result, tag, "[", min, ",", max, "]");
    }

    return result;
  }
};

StatusOr<bool> PatchHasGvar(const flat_hash_map<std::string, FontData>& patches,
                            const std::string& url) {
  auto it = patches.find(url);
  if (it == patches.end()) {
    return absl::NotFoundError(
        StrCat("Patch ", url, " not found in encoding output."));
  }

  const auto& font_data = it->second;
  return font_data.str().find("gvar") != std::string::npos;
}

// TODO(garretrieger): additional tests:
// - rejects duplicate glyph data segment ids.

TEST_F(EncoderTest, OutgoingEdges) {
  Encoder encoder;
  encoder.AddNonGlyphDataSegment(IntSet{1, 2});
  encoder.AddNonGlyphDataSegment(IntSet{3, 4});
  encoder.AddNonGlyphDataSegment(IntSet{5, 6});
  encoder.AddNonGlyphDataSegment(IntSet{7, 8});

  SubsetDefinition s1{1, 2};
  SubsetDefinition s2{3, 4};
  SubsetDefinition s3{5, 6};
  SubsetDefinition s4{7, 8};

  auto combos = encoder.OutgoingEdges(s2, 1);
  std::vector<Encoder::Edge> expected = {{s1}, {s3}, {s4}};
  ASSERT_EQ(combos, expected);

  combos = encoder.OutgoingEdges({1}, 1);
  expected = {{{2}}, {s2}, {s3}, {s4}};
  ASSERT_EQ(combos, expected);

  combos = encoder.OutgoingEdges(s1, 2);
  expected = {// l1
              {{3, 4}},
              {{5, 6}},
              {{7, 8}},

              // l2
              {{3, 4}, {5, 6}},
              {{3, 4}, {7, 8}},
              {{5, 6}, {7, 8}}};
  ASSERT_EQ(combos, expected);

  combos = encoder.OutgoingEdges(s1, 3);
  expected = {// l1
              {{3, 4}},
              {{5, 6}},
              {{7, 8}},

              // l2
              {{3, 4}, {5, 6}},
              {{3, 4}, {7, 8}},
              {{5, 6}, {7, 8}},

              // l3
              {{3, 4}, {5, 6}, {7, 8}}};
  ASSERT_EQ(combos, expected);

  combos = encoder.OutgoingEdges({1, 3, 5, 7}, 3);
  expected = {// l1
              {{2}},
              {{4}},
              {{6}},
              {{8}},

              // l2
              {{2}, {4}},
              {{2}, {6}},
              {{2}, {8}},
              {{4}, {6}},
              {{4}, {8}},
              {{6}, {8}},

              // l3
              {{2}, {4}, {6}},
              {{2}, {4}, {8}},
              {{2}, {6}, {8}},
              {{4}, {6}, {8}}};
  ASSERT_EQ(combos, expected);
}

TEST_F(EncoderTest, OutgoingEdges_DesignSpace_PointToRange) {
  SubsetDefinition base{1, 2};
  base.design_space[kWght] = AxisRange::Point(300);

  Encoder encoder;
  encoder.AddNonGlyphDataSegment(IntSet{3, 4});
  encoder.AddDesignSpaceSegment({{kWght, *AxisRange::Range(300, 400)}});

  SubsetDefinition s1{3, 4};

  SubsetDefinition s2{};
  s2.design_space[kWght] = *AxisRange::Range(300, 400);

  auto combos = encoder.OutgoingEdges(base, 2);
  std::vector<Encoder::Edge> expected = {{s1}, {s2}, {s1, s2}};
  ASSERT_EQ(combos, expected);
}

TEST_F(EncoderTest, OutgoingEdges_DesignSpace_AddAxis_1) {
  SubsetDefinition base{1, 2};
  base.design_space[kWght] = *AxisRange::Range(200, 500);

  Encoder encoder;
  encoder.AddNonGlyphDataSegment(IntSet{3, 4});
  encoder.AddDesignSpaceSegment({{kWdth, *AxisRange::Range(300, 400)}});

  SubsetDefinition s1{3, 4};

  SubsetDefinition s2{};
  s2.design_space[kWdth] = *AxisRange::Range(300, 400);

  auto combos = encoder.OutgoingEdges(base, 2);
  std::vector<Encoder::Edge> expected = {{s1}, {s2}, {s1, s2}};
  ASSERT_EQ(combos, expected);
}

TEST_F(EncoderTest, OutgoingEdges_DesignSpace_AddAxis_OverlappingAxisRange) {
  SubsetDefinition base{1, 2};
  base.design_space[kWght] = *AxisRange::Range(200, 500);

  Encoder encoder;
  encoder.AddNonGlyphDataSegment(IntSet{3, 4});
  encoder.AddDesignSpaceSegment({
      {kWght, *AxisRange::Range(300, 700)},
      {kWdth, *AxisRange::Range(300, 400)},
  });

  SubsetDefinition s1{3, 4};

  SubsetDefinition s2{};
  // TODO(garretrieger): since the current subtract implementation is limited
  //   we don't support partially subtracting a range. Once support is
  //   available this case can be updated to check wght range is partially
  //   subtracted instead of being ignored.
  s2.design_space[kWdth] = *AxisRange::Range(300, 400);

  auto combos = encoder.OutgoingEdges(base, 2);
  std::vector<Encoder::Edge> expected = {{s1}, {s2}, {s1, s2}};
  ASSERT_EQ(combos, expected);
}

// TODO(garretrieger): Once the union implementation is updated to
//  support unioning the same axis add tests for that.

TEST_F(EncoderTest, OutgoingEdges_DesignSpace_AddAxis_MergeSpace) {
  SubsetDefinition base{1, 2};
  base.design_space[kWght] = AxisRange::Point(300);
  base.design_space[kWdth] = AxisRange::Point(75);

  Encoder encoder;
  encoder.AddDesignSpaceSegment({
      {kWght, *AxisRange::Range(300, 700)},
  });
  encoder.AddDesignSpaceSegment({
      {kWdth, *AxisRange::Range(50, 100)},
  });

  SubsetDefinition s1{};
  s1.design_space[kWght] = *AxisRange::Range(300, 700);

  SubsetDefinition s2{};
  s2.design_space[kWdth] = *AxisRange::Range(50, 100);

  auto combos = encoder.OutgoingEdges(base, 2);
  std::vector<Encoder::Edge> expected = {{s1}, {s2}, {s1, s2}};
  ASSERT_EQ(combos, expected);
}

TEST_F(EncoderTest, MissingFace) {
  Encoder encoder;
  auto s1 = encoder.AddGlyphDataPatch(1, segment_1_gids);
  ASSERT_TRUE(absl::IsFailedPrecondition(s1)) << s1;

  auto s3 = encoder.Encode();
  ASSERT_TRUE(absl::IsFailedPrecondition(s3.status())) << s3.status();
}

TEST_F(EncoderTest, GlyphDataSegments_GidsNotInFace) {
  Encoder encoder;
  {
    hb_face_t* face = font.reference_face();
    encoder.SetFace(face);
    hb_face_destroy(face);
  }

  auto s = encoder.AddGlyphDataPatch(1, segment_1_gids);
  ASSERT_TRUE(absl::IsInvalidArgument(s)) << s;
}

TEST_F(EncoderTest, DontClobberBaseSubset) {
  Encoder encoder;
  {
    hb_face_t* face = noto_sans_jp.reference_face();
    encoder.SetFace(face);
    hb_face_destroy(face);
  }

  auto s = encoder.AddGlyphDataPatch(1, segment_1_gids);
  ASSERT_TRUE(s.ok()) << s;

  s = encoder.SetBaseSubset(IntSet{});
  ASSERT_TRUE(s.ok()) << s;

  s = encoder.SetBaseSubset(IntSet{1});
  ASSERT_TRUE(s.ok()) << s;

  s = encoder.SetBaseSubset(IntSet{});
  ASSERT_TRUE(absl::IsFailedPrecondition(s)) << s;
}

TEST_F(EncoderTest, Encode_OneSubset) {
  Encoder encoder;
  hb_face_t* face = font.reference_face();
  encoder.SetFace(face);

  auto s = encoder.SetBaseSubset(IntSet{'a', 'd'});
  ASSERT_TRUE(s.ok()) << s;
  auto encoding = encoder.Encode();
  hb_face_destroy(face);

  ASSERT_TRUE(encoding.ok()) << encoding.status();

  graph g;
  auto sc = ToGraph(*encoding, g);
  ASSERT_TRUE(sc.ok()) << sc;

  graph expected{{"ad", {}}};
  ASSERT_EQ(g, expected);
}

TEST_F(EncoderTest, Encode_TwoSubsets) {
  IntSet s1 = {'b', 'c'};
  Encoder encoder;
  hb_face_t* face = font.reference_face();
  encoder.SetFace(face);
  auto s = encoder.SetBaseSubset(IntSet{'a', 'd'});
  ASSERT_TRUE(s.ok()) << s;
  encoder.AddNonGlyphDataSegment(s1);

  auto encoding = encoder.Encode();
  hb_face_destroy(face);

  ASSERT_TRUE(encoding.ok()) << encoding.status();

  graph g;
  auto sc = ToGraph(*encoding, g);
  ASSERT_TRUE(sc.ok()) << sc;

  graph expected{{"ad", {"abcd"}}, {"abcd", {}}};
  ASSERT_EQ(g, expected);
}

TEST_F(EncoderTest, Encode_TwoSubsetsAndOptionalFeature) {
  IntSet s1 = {'B', 'C'};
  Encoder encoder;
  hb_face_t* face = full_font.reference_face();
  encoder.SetFace(face);
  auto s = encoder.SetBaseSubset(IntSet{'A', 'D'});
  ASSERT_TRUE(s.ok()) << s;
  encoder.AddNonGlyphDataSegment(s1);
  encoder.AddFeatureGroupSegment({HB_TAG('c', '2', 's', 'c')});

  auto encoding = encoder.Encode();
  hb_face_destroy(face);

  ASSERT_TRUE(encoding.ok()) << encoding.status();

  graph g;
  auto sc = ToGraph(*encoding, g);
  ASSERT_TRUE(sc.ok()) << sc;

  graph expected{
      {"AD", {"ABCD", "AD|c2sc"}},
      {"AD|c2sc", {"ABCD|c2sc"}},
      {"ABCD", {"ABCD|c2sc"}},
      {"ABCD|c2sc", {}},
  };
  ASSERT_EQ(g, expected);
}

TEST_F(EncoderTest, Encode_ThreeSubsets) {
  IntSet s1 = {'b'};
  IntSet s2 = {'c'};
  Encoder encoder;
  hb_face_t* face = font.reference_face();
  encoder.SetFace(face);
  auto s = encoder.SetBaseSubset(IntSet{'a'});
  ASSERT_TRUE(s.ok()) << s;
  encoder.AddNonGlyphDataSegment(s1);
  encoder.AddNonGlyphDataSegment(s2);

  auto encoding = encoder.Encode();
  hb_face_destroy(face);

  ASSERT_TRUE(encoding.ok()) << encoding.status();
  ASSERT_EQ(encoding->patches.size(), 4);

  graph g;
  auto sc = ToGraph(*encoding, g);
  ASSERT_TRUE(sc.ok()) << sc;

  graph expected{
      {"a", {"ab", "ac"}},
      {"ab", {"abc"}},
      {"ac", {"abc"}},
      {"abc", {}},
  };
  ASSERT_EQ(g, expected);
}

TEST_F(EncoderTest, Encode_ThreeSubsets_WithOverlaps) {
  IntSet s1 = {'b', 'c'};
  IntSet s2 = {'b', 'd'};
  Encoder encoder;
  hb_face_t* face = font.reference_face();
  encoder.SetFace(face);
  auto s = encoder.SetBaseSubset(IntSet{'a'});
  ASSERT_TRUE(s.ok()) << s;
  encoder.AddNonGlyphDataSegment(s1);
  encoder.AddNonGlyphDataSegment(s2);

  auto encoding = encoder.Encode();
  hb_face_destroy(face);

  ASSERT_TRUE(encoding.ok()) << encoding.status();
  ASSERT_EQ(encoding->patches.size(), 4);

  graph g;
  auto sc = ToGraph(*encoding, g);
  ASSERT_TRUE(sc.ok()) << sc;

  graph expected{
      {"a", {"abc", "abd"}},
      {"abc", {"abcd"}},
      {"abd", {"abcd"}},
      {"abcd", {}},
  };
  ASSERT_EQ(g, expected);
}

TEST_F(EncoderTest, Encode_ThreeSubsets_VF) {
  Encoder encoder;
  hb_face_t* face = vf_font.reference_face();
  encoder.SetFace(face);

  SubsetDefinition base_def{'a'};
  base_def.design_space[kWdth] = AxisRange::Point(100.0f);
  auto s = encoder.SetBaseSubsetFromDef(base_def);
  ASSERT_TRUE(s.ok()) << s;

  encoder.AddNonGlyphDataSegment(IntSet{'b'});
  encoder.AddDesignSpaceSegment({{kWdth, *AxisRange::Range(75.0f, 100.0f)}});

  auto encoding = encoder.Encode();
  hb_face_destroy(face);

  ASSERT_TRUE(encoding.ok()) << encoding.status();
  ASSERT_EQ(encoding->patches.size(), 4);

  graph g;
  auto sc = ToGraph(*encoding, g);
  ASSERT_TRUE(sc.ok()) << sc;

  graph expected{
      {"a|wght[100..900]",
       {"ab|wght[100..900]", "a|wght[100..900],wdth[75..100]"}},
      {"ab|wght[100..900]", {"ab|wght[100..900],wdth[75..100]"}},
      {"a|wght[100..900],wdth[75..100]", {"ab|wght[100..900],wdth[75..100]"}},
      {"ab|wght[100..900],wdth[75..100]", {}},
  };
  ASSERT_EQ(g, expected);
}

TEST_F(EncoderTest, Encode_ThreeSubsets_Mixed) {
  Encoder encoder;
  {
    hb_face_t* face = noto_sans_jp.reference_face();
    encoder.SetFace(face);
    hb_face_destroy(face);
  }

  auto s = encoder.AddGlyphDataPatch(0, segment_0_gids);
  s.Update(encoder.AddGlyphDataPatch(1, segment_1_gids));
  s.Update(encoder.AddGlyphDataPatch(2, segment_2_gids));
  s.Update(encoder.AddGlyphDataPatch(3, segment_3_gids));
  s.Update(encoder.AddGlyphDataPatch(4, segment_4_gids));
  ASSERT_TRUE(s.ok()) << s;

  s.Update(encoder.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_3_cps, 3, PatchEncoding::GLYPH_KEYED)));
  s.Update(encoder.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_4_cps, 4, PatchEncoding::GLYPH_KEYED)));

  IntSet base_subset;
  base_subset.insert(segment_0_cps.begin(), segment_0_cps.end());
  base_subset.insert(segment_1_cps.begin(), segment_1_cps.end());
  base_subset.insert(segment_2_cps.begin(), segment_2_cps.end());
  s.Update(encoder.SetBaseSubset(base_subset));

  IntSet extension_segment;
  extension_segment.insert(segment_3_cps.begin(), segment_3_cps.end());
  extension_segment.insert(segment_4_cps.begin(), segment_4_cps.end());
  encoder.AddNonGlyphDataSegment(extension_segment);

  ASSERT_TRUE(s.ok()) << s;

  auto encoding = encoder.Encode();

  ASSERT_TRUE(encoding.ok()) << encoding.status();
  auto face = encoding->init_font.face();
  auto cps = FontHelper::ToCodepointsSet(face.get());
  ASSERT_TRUE(cps.contains(chunk0_cp));
  ASSERT_TRUE(cps.contains(chunk1_cp));
  ASSERT_TRUE(cps.contains(chunk2_cp));
  ASSERT_FALSE(cps.contains(chunk3_cp));
  ASSERT_FALSE(cps.contains(chunk4_cp));

  ASSERT_EQ(encoding->patches.size(), 3);

  // TODO(garretrieger): check the glyph keyed mapping entries in the base and
  // check
  //  they are unmodified in derived fonts.
  // TODO(garretrieger): apply a glyph keyed patch and then check that you
  //  can still form the graph with derived fonts containing the
  //  modified glyf, loca, and IFT table.

  {
    auto face = encoding->init_font.face();
    auto iftx_data =
        FontHelper::TableData(face.get(), HB_TAG('I', 'F', 'T', 'X'));
    ASSERT_FALSE(iftx_data.empty());
  }

  // expected patches:
  // - segment 3 (glyph keyed)
  // - segment 4 (glyph keyed)
  // - shared brotli to (segment 3 + 4)
  // TODO(garretrieger): Check graph instead
}

TEST_F(EncoderTest, Encode_ThreeSubsets_Mixed_VF) {
  Encoder encoder;
  {
    hb_face_t* face = vf_font.reference_face();
    encoder.SetFace(face);
    hb_face_destroy(face);
  }

  auto s = encoder.AddGlyphDataPatch(0, {37, 38, 39, 40});
  s.Update(encoder.AddGlyphDataPatch(1, {41, 42, 43, 44}));
  ASSERT_TRUE(s.ok()) << s;

  s.Update(encoder.AddGlyphDataPatchCondition(PatchMap::Entry(
      {0x41, 0x42, 0x43, 0x44}, 0, PatchEncoding::GLYPH_KEYED)));
  s.Update(encoder.AddGlyphDataPatchCondition(PatchMap::Entry(
      {0x45, 0x46, 0x47, 0x48}, 1, PatchEncoding::GLYPH_KEYED)));

  SubsetDefinition base_subset;
  base_subset.design_space[kWdth] = AxisRange::Point(100.0f);
  base_subset.design_space[kWght] = AxisRange::Point(300.0f);
  s.Update(encoder.SetBaseSubsetFromDef(base_subset));

  IntSet extension_segment = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48};
  encoder.AddNonGlyphDataSegment(extension_segment);
  encoder.AddDesignSpaceSegment({{kWght, *AxisRange::Range(200.0f, 700.0f)}});

  ASSERT_TRUE(s.ok()) << s;

  auto encoding = encoder.Encode();

  graph g;
  auto sc = ToGraph(*encoding, g, true);
  ASSERT_TRUE(sc.ok()) << sc;

  graph expected{
      {"", {"ABCDEFGH|08.ift_tk", "|wght[200..700]|0C.ift_tk"}},
      {"ABCDEFGH", {"ABCDEFGH|wght[200..700]|0G.ift_tk"}},
      {"ABCDEFGH|wght[200..700]", {}},
      {"|wght[200..700]", {"ABCDEFGH|wght[200..700]|0K.ift_tk"}},
  };
  ASSERT_EQ(g, expected);

  // Patches that don't modify variation space should not modify gvar:
  auto has_gvar = PatchHasGvar(encoding->patches, "08.ift_tk");
  ASSERT_TRUE(has_gvar.ok()) << has_gvar.status();
  ASSERT_FALSE(*has_gvar);

  has_gvar = PatchHasGvar(encoding->patches, "0K.ift_tk");
  ASSERT_TRUE(has_gvar.ok()) << has_gvar.status();
  ASSERT_FALSE(*has_gvar);

  // Patches that modify variation space should replace gvar:
  has_gvar = PatchHasGvar(encoding->patches, "0G.ift_tk");
  ASSERT_TRUE(has_gvar.ok()) << has_gvar.status();
  ASSERT_TRUE(*has_gvar);
}

TEST_F(EncoderTest, Encode_ThreeSubsets_Mixed_WithFeatureMappings) {
  Encoder encoder;
  {
    hb_face_t* face = noto_sans_jp.reference_face();
    encoder.SetFace(face);
    hb_face_destroy(face);
  }

  auto s = encoder.AddGlyphDataPatch(0, segment_0_gids);
  s.Update(encoder.AddGlyphDataPatch(1, segment_1_gids));
  s.Update(encoder.AddGlyphDataPatch(2, segment_2_gids));
  s.Update(encoder.AddGlyphDataPatch(3, segment_3_gids));
  s.Update(encoder.AddGlyphDataPatch(4, segment_4_gids));

  s.Update(encoder.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_2_cps, 2, PatchEncoding::GLYPH_KEYED)));
  s.Update(encoder.AddGlyphDataPatchCondition(
      PatchMap::Entry(segment_3_cps, 3, PatchEncoding::GLYPH_KEYED)));

  PatchMap::Entry feature(segment_3_cps, 4, PatchEncoding::GLYPH_KEYED);
  feature.coverage.features = {HB_TAG('c', 'c', 'm', 'p')};
  s.Update(encoder.AddGlyphDataPatchCondition(feature));

  ASSERT_TRUE(s.ok()) << s;

  // Partitions {0, 1}, {2, 3, 4}, +ccmp
  IntSet base_subset;
  base_subset.insert(segment_0_cps.begin(), segment_0_cps.end());
  base_subset.insert(segment_1_cps.begin(), segment_1_cps.end());
  s.Update(encoder.SetBaseSubset(base_subset));

  IntSet extension_segment;
  extension_segment.insert(segment_2_cps.begin(), segment_2_cps.end());
  extension_segment.insert(segment_3_cps.begin(), segment_3_cps.end());
  extension_segment.insert(segment_4_cps.begin(), segment_4_cps.end());
  encoder.AddNonGlyphDataSegment(extension_segment);
  encoder.AddFeatureGroupSegment({HB_TAG('c', 'c', 'm', 'p')});
  ASSERT_TRUE(s.ok()) << s;

  auto encoding = encoder.Encode();
  ASSERT_TRUE(encoding.ok()) << encoding.status();

  ASSERT_EQ(encoding->patches.size(), 7);

  // expected patches:
  // - segment 2 (glyph keyed)
  // - segment 3 (glyph keyed)
  // - segment 4 (glyph keyed), triggered by ccmap + segment 3
  // - table keyed patches...
  // TODO(garretrieger): Check graph instead
}

TEST_F(EncoderTest, Encode_FourSubsets) {
  IntSet s1 = {'b'};
  IntSet s2 = {'c'};
  IntSet s3 = {'d'};
  Encoder encoder;
  hb_face_t* face = font.reference_face();
  encoder.SetFace(face);
  auto s = encoder.SetBaseSubset(IntSet{'a'});
  ASSERT_TRUE(s.ok()) << s;
  encoder.AddNonGlyphDataSegment(s1);
  encoder.AddNonGlyphDataSegment(s2);
  encoder.AddNonGlyphDataSegment(s3);

  auto encoding = encoder.Encode();
  hb_face_destroy(face);

  ASSERT_TRUE(encoding.ok()) << encoding.status();
  ASSERT_EQ(encoding->patches.size(), 12);

  graph g;
  auto sc = ToGraph(*encoding, g);
  ASSERT_TRUE(sc.ok()) << sc;

  graph expected{
      {"a", {"ab", "ac", "ad"}}, {"ab", {"abc", "abd"}}, {"ac", {"abc", "acd"}},
      {"ad", {"abd", "acd"}},    {"abc", {"abcd"}},      {"abd", {"abcd"}},
      {"acd", {"abcd"}},         {"abcd", {}},
  };
  ASSERT_EQ(g, expected);
}

TEST_F(EncoderTest, Encode_FourSubsets_WithJumpAhead) {
  IntSet s1 = {'b'};
  IntSet s2 = {'c'};
  IntSet s3 = {'d'};
  Encoder encoder;
  hb_face_t* face = font.reference_face();
  encoder.SetFace(face);
  auto s = encoder.SetBaseSubset(IntSet{'a'});
  ASSERT_TRUE(s.ok()) << s;
  encoder.AddNonGlyphDataSegment(s1);
  encoder.AddNonGlyphDataSegment(s2);
  encoder.AddNonGlyphDataSegment(s3);
  encoder.SetJumpAhead(2);

  auto encoding = encoder.Encode();
  hb_face_destroy(face);

  ASSERT_TRUE(encoding.ok()) << encoding.status();
  ASSERT_EQ(encoding->patches.size(), 18);

  graph g;
  auto sc = ToGraph(*encoding, g);
  ASSERT_TRUE(sc.ok()) << sc;

  graph expected{
      {"a", {"ab", "ac", "ad", "abc", "abd", "acd"}},
      {"ab", {"abc", "abd", "abcd"}},
      {"ac", {"abc", "acd", "abcd"}},
      {"ad", {"abd", "acd", "abcd"}},
      {"abc", {"abcd"}},
      {"abd", {"abcd"}},
      {"acd", {"abcd"}},
      {"abcd", {}},
  };
  ASSERT_EQ(g, expected);
}

TEST_F(EncoderTest, Encode_FourSubsets_WithJumpAhead_AndPreload) {
  IntSet s1 = {'b'};
  IntSet s2 = {'c'};
  IntSet s3 = {'d'};
  Encoder encoder;
  hb_face_t* face = font.reference_face();
  encoder.SetFace(face);
  auto s = encoder.SetBaseSubset(IntSet{'a'});
  ASSERT_TRUE(s.ok()) << s;
  encoder.AddNonGlyphDataSegment(s1);
  encoder.AddNonGlyphDataSegment(s2);
  encoder.AddNonGlyphDataSegment(s3);
  encoder.SetJumpAhead(2);
  encoder.SetUsePreloadLists(true);

  auto encoding = encoder.Encode();
  hb_face_destroy(face);

  ASSERT_TRUE(encoding.ok()) << encoding.status();
  ASSERT_EQ(encoding->patches.size(), 12);

  graph g;
  auto sc = ToGraph(*encoding, g);
  ASSERT_TRUE(sc.ok()) << sc;

  // When preload lists are used all edges only move one subset at a time
  // (with the multi subset jumps covered by preloading).
  graph expected{
      {"a", {"ab", "ac", "ad"}}, {"ab", {"abc", "abd"}}, {"ac", {"abc", "acd"}},
      {"ad", {"abd", "acd"}},    {"abc", {"abcd"}},      {"abd", {"abcd"}},
      {"acd", {"abcd"}},         {"abcd", {}},
  };
  ASSERT_EQ(g, expected);
}

void ClearCompatIdFromFormat2(uint8_t* data) {
  for (uint32_t index = 5; index < (5 + 16); index++) {
    data[index] = 0;
  }
}

TEST_F(EncoderTest, Encode_ComplicatedActivationConditions) {
  Encoder encoder;
  hb_face_t* face = font.reference_face();
  encoder.SetFace(face);

  auto s = encoder.SetBaseSubset(IntSet{});
  s.Update(encoder.AddGlyphDataPatch(1, {69}));  // a
  s.Update(encoder.AddGlyphDataPatch(2, {70}));  // b
  s.Update(encoder.AddGlyphDataPatch(3, {71}));  // c
  s.Update(encoder.AddGlyphDataPatch(4, {72}));  // d
  s.Update(encoder.AddGlyphDataPatch(5, {50}));
  s.Update(encoder.AddGlyphDataPatch(6, {60}));

  encoder.AddNonGlyphDataSegment(IntSet{'a', 'b', 'c', 'd'});

  // 0
  s.Update(encoder.AddGlyphDataPatchCondition(
      PatchMap::Entry({'b'}, 2, PatchEncoding::GLYPH_KEYED)));

  // 1
  s.Update(encoder.AddGlyphDataPatchCondition(
      PatchMap::Entry({'c'}, 4, PatchEncoding::GLYPH_KEYED)));

  {
    // 2
    PatchMap::Entry condition({'a'}, 5, PatchEncoding::GLYPH_KEYED);
    condition.ignored = true;
    s.Update(encoder.AddGlyphDataPatchCondition(condition));
  }
  {
    // 3
    PatchMap::Entry condition({'d'}, 6, PatchEncoding::GLYPH_KEYED);
    condition.ignored = true;
    s.Update(encoder.AddGlyphDataPatchCondition(condition));
  }

  {
    // 4
    PatchMap::Entry condition;
    condition.coverage.child_indices = {1, 2};
    condition.patch_indices = {5};
    s.Update(encoder.AddGlyphDataPatchCondition(condition));
  }

  {
    // 5
    PatchMap::Entry condition;
    condition.ignored = true;
    condition.patch_indices = {6};
    condition.coverage.child_indices = {0, 3};
    s.Update(encoder.AddGlyphDataPatchCondition(condition));
  }

  {
    // 5
    PatchMap::Entry condition;
    condition.patch_indices = {6};
    condition.coverage.child_indices = {4, 5};
    condition.coverage.conjunctive = true;
    s.Update(encoder.AddGlyphDataPatchCondition(condition));
  }

  ASSERT_TRUE(s.ok()) << s;
  auto encoding = encoder.Encode();
  hb_face_destroy(face);

  ASSERT_TRUE(encoding.ok()) << encoding.status();
  auto encoded_face = encoding->init_font.face();

  auto ift_table =
      FontHelper::TableData(encoded_face.get(), HB_TAG('I', 'F', 'T', 'X'));
  std::string ift_table_copy = ift_table.string();
  ClearCompatIdFromFormat2((uint8_t*)ift_table_copy.data());
  ift_table.copy(ift_table_copy);

  // a = gid69 = cp97
  // b = gid70 = cp98
  // c = gid71 = cp99
  // d = gid72 = cp100
  uint8_t expected_format2[] = {
      0x02,                    // format
      0x00, 0x00, 0x00, 0x00,  // reserved
      0x0, 0x0, 0x0, 0x0,      // compat id[0]
      0x0, 0x0, 0x0, 0x0,      // compat id[1]
      0x0, 0x0, 0x0, 0x0,      // compat id[2]
      0x0, 0x0, 0x0, 0x0,      // compat id[3]
      0x03,                    // default patch format = glyph keyed
      0x00, 0x00, 0x07,        // entry count = 7
      0x00, 0x00, 0x00, 0x30,  // entries offset
      0x00, 0x00, 0x00, 0x00,  // string data offset (NULL)
      0x00, 0x0D,              // uri template length
      0x31, 0x5f, 0x7b, 0x69,  // uri template
      0x64, 0x7d, 0x2e, 0x69,  // uri template
      0x66, 0x74, 0x5f, 0x67,  // uri template
      0x6b,                    // uri template

      // entry[0] {{2}} -> 2,
      0b00010100,        // format (id delta, code points no bias)
      0x00, 0x00, 0x02,  // delta +1, id = 2
      0x11, 0x42, 0x41,  // sparse set {b}

      // entry[1] {{3}} -> 4
      0b00010100,        // format (id delta, code points no bias)
      0x00, 0x00, 0x02,  // delta +1, id = 4
      0x11, 0x42, 0x81,  // sparse set {c}

      // entry[2] {{1}} ignored
      0b01010000,        // format (ignored, code poitns no bias)
      0x11, 0x42, 0x21,  // sparse set {a}

      // entry[3] {{4}} ignored
      0b01010000,        // format (ignored, code poitns no bias)
      0x11, 0x42, 0x12,  // sparse set {d}

      // entry[4] {{1 OR 3}} -> 5
      0b00000110,        // format (copy indices, id delta)
      0x02,              // copy mode union, count 2
      0x00, 0x00, 0x01,  // copy entry[1] 'c'
      0x00, 0x00, 0x02,  // copy entry[2] 'a'
      0xff, 0xff, 0xfc,  // delta -2, id = 5

      // entry[5] {{2 OR 4}} ignored
      0b01000010,        // format (ignored, copy indices)
      0x02,              // copy mode union, count 2
      0x00, 0x00, 0x00,  // copy entry[0] 'b'
      0x00, 0x00, 0x03,  // copy entry[3] 'd'

      // entry[6] {{1 OR 3} AND {2 OR 4}} -> 6
      0b00000110,        // format (copy indices, id delta)
      0x82,              // copy mode append, count 2
      0x00, 0x00, 0x04,  // copy entry[4] {1 OR 3}
      0x00, 0x00, 0x05,  // copy entry[5] {2 OR 4}
      0xff, 0xff, 0xfe   // delta -1, id = 6
  };

  ASSERT_EQ(ift_table.span(), absl::Span<const uint8_t>(expected_format2, 100));
}

TEST_F(EncoderTest, RoundTripWoff2) {
  auto ttf = Encoder::RoundTripWoff2(font.str());
  ASSERT_TRUE(ttf.ok()) << ttf.status();

  ASSERT_GT(ttf->size(), 4);
  uint8_t true_type_tag[] = {0, 1, 0, 0};
  ASSERT_EQ(true_type_tag, Span<const uint8_t>((const uint8_t*)ttf->data(), 4));
}

TEST_F(EncoderTest, RoundTripWoff2_Fails) {
  auto ttf = Encoder::RoundTripWoff2(woff2_font.str());
  ASSERT_TRUE(absl::IsInternal(ttf.status())) << ttf.status();
}

}  // namespace ift::encoder
