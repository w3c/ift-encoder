#include "ift/encoder/glyph_segmentation.h"

#include "common/font_data.h"
#include "gtest/gtest.h"

using absl::btree_set;
using common::FontData;
using common::hb_face_unique_ptr;
using common::make_hb_face;

namespace ift::encoder {

class GlyphSegmentationTest : public ::testing::Test {
 protected:
  GlyphSegmentationTest()
      : roboto(make_hb_face(nullptr)),
        noto_nastaliq_urdu(make_hb_face(nullptr)) {
    roboto = from_file("common/testdata/Roboto-Regular.ttf");
    noto_nastaliq_urdu =
        from_file("common/testdata/NotoNastaliqUrdu.subset.ttf");
  }

  hb_face_unique_ptr from_file(const char* filename) {
    hb_blob_t* blob = hb_blob_create_from_file_or_fail(filename);
    if (!blob) {
      assert(false);
    }
    FontData result(blob);
    hb_blob_destroy(blob);
    return result.face();
  }

  hb_face_unique_ptr roboto;
  hb_face_unique_ptr noto_nastaliq_urdu;
};

TEST_F(GlyphSegmentationTest, SimpleSegmentation) {
  auto segmentation = GlyphSegmentation::CodepointToGlyphSegments(
      roboto.get(), {'a'}, {{'b'}, {'c'}});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<btree_set<hb_codepoint_t>> expected_segments = {{'b'}, {'c'}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid69 }
p0: { gid70 }
p1: { gid71 }
if (s0) then p0
if (s1) then p1
)");
}

TEST_F(GlyphSegmentationTest, AndCondition) {
  auto segmentation = GlyphSegmentation::CodepointToGlyphSegments(
      roboto.get(), {'a'}, {{'f'}, {'i'}});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<btree_set<hb_codepoint_t>> expected_segments = {{'f'}, {'i'}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid69 }
p0: { gid74 }
p1: { gid77 }
p2: { gid444, gid446 }
if (s0) then p0
if (s1) then p1
if (s0 AND s1) then p2
)");
}

TEST_F(GlyphSegmentationTest, OrCondition) {
  auto segmentation = GlyphSegmentation::CodepointToGlyphSegments(
      roboto.get(), {'a'}, {{0xc1}, {0x106}});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<btree_set<hb_codepoint_t>> expected_segments = {{0xc1}, {0x106}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid69 }
p0: { gid37, gid640 }
p1: { gid39, gid700 }
p2: { gid117 }
if (s0) then p0
if (s1) then p1
if ((s0 OR s1)) then p2
)");
}

TEST_F(GlyphSegmentationTest, MergeBase_ViaConditions) {
  // {e, f} is too small, the merger should select {i, l} to merge since
  // there is a dependency between these two. The result should have no
  // conditional patches since ligatures will have been brought into the merged
  // {e, f, i, l}
  auto segmentation = GlyphSegmentation::CodepointToGlyphSegments(
      roboto.get(), {},
      {{'a', 'b', 'd'}, {'e', 'f'}, {'j', 'k', 'm', 'n'}, {'i', 'l'}}, 370);
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<btree_set<hb_codepoint_t>> expected_segments = {
      {'a', 'b', 'd'}, {'e', 'f', 'i', 'l'}, {'j', 'k', 'm', 'n'}, {}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid69, gid70, gid72 }
p1: { gid73, gid74, gid77, gid80, gid444, gid445, gid446, gid447 }
p2: { gid78, gid79, gid81, gid82 }
if (s0) then p0
if (s1) then p1
if (s2) then p2
)");
}

TEST_F(GlyphSegmentationTest, MergeBases) {
  // {e, f} is too smal, since no conditional patches exist it should merge with
  // the next available base which is {'j', 'k'}
  auto segmentation = GlyphSegmentation::CodepointToGlyphSegments(
      roboto.get(), {},
      {{'a', 'b', 'd'}, {'e', 'f'}, {'j', 'k'}, {'m', 'n', 'o', 'p'}}, 370);
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<btree_set<hb_codepoint_t>> expected_segments = {
      {'a', 'b', 'd'},
      {'e', 'f', 'j', 'k'},
      {},
      {'m', 'n', 'o', 'p'},
  };
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid69, gid70, gid72 }
p1: { gid73, gid74, gid78, gid79 }
p2: { gid81, gid82, gid83, gid84 }
if (s0) then p0
if (s1) then p1
if (s3) then p2
)");
}

TEST_F(GlyphSegmentationTest, MergeBases_MaxSize) {
  // {e, f} is too small, since no conditional patches exist it will merge with
  // the next available base which is {'m', 'n', 'o', 'p'}. However that patch
  // is too large, so the next one {j, k} will actually be chosen.
  auto segmentation = GlyphSegmentation::CodepointToGlyphSegments(
      roboto.get(), {},
      {{'a', 'b', 'd'}, {'e', 'f'}, {'m', 'n', 'o', 'p'}, {'j', 'k'}}, 370,
      700);
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<btree_set<hb_codepoint_t>> expected_segments = {
      {'a', 'b', 'd'}, {'e', 'f', 'j', 'k'}, {'m', 'n', 'o', 'p'}, {}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid69, gid70, gid72 }
p1: { gid73, gid74, gid78, gid79 }
p2: { gid81, gid82, gid83, gid84 }
if (s0) then p0
if (s1) then p1
if (s2) then p2
)");
}

TEST_F(GlyphSegmentationTest, MixedAndOr) {
  auto segmentation = GlyphSegmentation::CodepointToGlyphSegments(
      roboto.get(), {'a'}, {{'f', 0xc1}, {'i', 0x106}});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<btree_set<hb_codepoint_t>> expected_segments = {{'f', 0xc1},
                                                              {'i', 0x106}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid69 }
p0: { gid37, gid74, gid640 }
p1: { gid39, gid77, gid700 }
p2: { gid444, gid446 }
p3: { gid117 }
if (s0) then p0
if (s1) then p1
if ((s0 OR s1)) then p3
if (s0 AND s1) then p2
)");
}

TEST_F(GlyphSegmentationTest, UnmappedGlyphs_FallbackSegment) {
  auto segmentation = GlyphSegmentation::CodepointToGlyphSegments(
      noto_nastaliq_urdu.get(), {}, {{0x62a}, {0x62b}, {0x62c}, {0x62d}});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  ASSERT_EQ(segmentation->UnmappedGlyphs().size(), 12);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid3, gid9, gid155 }
p1: { gid4, gid10, gid156 }
p2: { gid5, gid6, gid11, gid157 }
p3: { gid158 }
p4: { gid12, gid13, gid24, gid30, gid38, gid39, gid57, gid59, gid62, gid68, gid139, gid140, gid153, gid172 }
p5: { gid47, gid64, gid73, gid74, gid75, gid76, gid77, gid83, gid111, gid149, gid174, gid190, gid191 }
p6: { gid14, gid33, gid60, gid91, gid112, gid145, gid152 }
if (s0) then p0
if (s1) then p1
if (s2) then p2
if (s3) then p3
if ((s0 OR s1)) then p4
if ((s2 OR s3)) then p6
if ((s0 OR s1 OR s2 OR s3)) then p5
)");
}

// TODO(garretrieger): add test where or_set glyphs are moved back to unmapped
// due to found "additional conditions".

}  // namespace ift::encoder