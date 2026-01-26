#include "ift/encoder/closure_glyph_segmenter.h"

#include <optional>

#include "common/font_data.h"
#include "common/font_helper.h"
#include "common/int_set.h"
#include "gtest/gtest.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/merge_strategy.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/unicode_frequencies.h"
#include "ift/freq/unigram_probability_calculator.h"

using absl::btree_map;
using common::CodepointSet;
using common::FontData;
using common::hb_face_unique_ptr;
using common::IntSet;
using common::make_hb_face;
using common::SegmentSet;
using ift::freq::UnicodeFrequencies;
using ift::freq::UnigramProbabilityCalculator;

namespace ift::encoder {

// TODO XXXXX add full roboto type test which uses the alternate closure analysis modes.
// verify result is the same as pure closure.

class ClosureGlyphSegmenterTest : public ::testing::Test {
 protected:
  ClosureGlyphSegmenterTest()
      : roboto(make_hb_face(nullptr)),
        noto_nastaliq_urdu(make_hb_face(nullptr)),
        noto_sans_jp(make_hb_face(nullptr)),
        segmenter(8, 8, PATCH, CLOSURE_ONLY),
        segmenter_find_conditions(8, 8, FIND_CONDITIONS, CLOSURE_ONLY),
        segmenter_move_to_init_font(8, 8, MOVE_TO_INIT_FONT, CLOSURE_ONLY) {
    roboto = from_file("common/testdata/Roboto-Regular.ttf");
    noto_nastaliq_urdu =
        from_file("common/testdata/NotoNastaliqUrdu.subset.ttf");
    noto_sans_jp =
        from_file("common/testdata/NotoSansJP-Regular.ttf");
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
  hb_face_unique_ptr noto_sans_jp;
  ClosureGlyphSegmenter segmenter;
  ClosureGlyphSegmenter segmenter_find_conditions;
  ClosureGlyphSegmenter segmenter_move_to_init_font;
};

TEST_F(ClosureGlyphSegmenterTest, SimpleSegmentation) {
  auto segmentation =
      segmenter.CodepointToGlyphSegments(roboto.get(), {'a'}, {{'b'}, {'c'}});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<SubsetDefinition> expected_segments = {{'b'}, {'c'}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid69 }
p0: { gid70 }
p1: { gid71 }
if (s0) then p0
if (s1) then p1
)");
}

TEST_F(ClosureGlyphSegmenterTest, SimpleSegmentation_DefaultFeatures) {
  SubsetDefinition init;
  init.feature_tags.insert(HB_TAG('c', 'c', 'm', 'p'));
  init.codepoints = {'a'};
  auto segmentation =
      segmenter.CodepointToGlyphSegments(roboto.get(), init, {{'b'}, {'c'}});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<SubsetDefinition> expected_segments = {{'b'}, {'c'}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  // ccmp is a default feature and is already included so should not show up in
  // the segmentation.
  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid69 }
p0: { gid70 }
p1: { gid71 }
if (s0) then p0
if (s1) then p1
)");
}

TEST_F(ClosureGlyphSegmenterTest, InitSegmentExpansion) {
  SubsetDefinition init;
  init.codepoints = {0x7528};
  auto segmentation =
      segmenter.CodepointToGlyphSegments(noto_sans_jp.get(), init, {{0x2F64}});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  // 0x2F64 and 0x7528 both map to the same glyph, the expectation is that 0x2F64 get's
  // pulled into the initial font.
  ASSERT_EQ(segmentation->InitialFontSegment().codepoints, (CodepointSet {0x2F64, 0x7528}));

  std::vector<SubsetDefinition> expected_segments = {{}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid8759 }
)");
}

TEST_F(ClosureGlyphSegmenterTest, SimpleSegmentation_DropsUneededSegment) {
  auto segmentation = segmenter.CodepointToGlyphSegments(roboto.get(), {'a'},
                                                         {{'b'}, {'c'}, {'a'}});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  // Initial processing sees 'a' is in the init font and clears the segment.
  std::vector<SubsetDefinition> expected_segments = {{'b'}, {'c'}, {}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  // Optional segment with 'a' isn't need as it's already included in the init,
  // so the segmentation shouldn't include anything for it.
  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid69 }
p0: { gid70 }
p1: { gid71 }
if (s0) then p0
if (s1) then p1
)");
}

TEST_F(ClosureGlyphSegmenterTest,
       SimpleSegmentation_DropsUneededSegment_DefaultFeature) {
  SubsetDefinition ccmp;
  ccmp.feature_tags.insert(HB_TAG('c', 'c', 'm', 'p'));
  auto segmentation = segmenter.CodepointToGlyphSegments(roboto.get(), {'a'},
                                                         {{'b'}, {'c'}, ccmp});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<SubsetDefinition> expected_segments = {{'b'}, {'c'}, ccmp};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  // Optional segment with 'a' isn't need as it's already included in the init,
  // so the segmentation shouldn't include anything for it.
  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid69 }
p0: { gid70 }
p1: { gid71 }
if (s0) then p0
if (s1) then p1
)");
}

TEST_F(ClosureGlyphSegmenterTest, SegmentationWithPartialOverlap) {
  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {'a'}, {{'b', 'c'}, {'c', 'd'}});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<SubsetDefinition> expected_segments = {{'b', 'c'}, {'c', 'd'}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid69 }
p0: { gid70 }
p1: { gid72 }
p2: { gid71 }
if (s0) then p0
if (s1) then p1
if ((s0 OR s1)) then p2
)");
}

TEST_F(ClosureGlyphSegmenterTest, SegmentationWithFullOverlap) {
  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {'a'}, {{'b', 'c'}, {'b', 'c'}, {'d'}});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<SubsetDefinition> expected_segments = {
      {'b', 'c'}, {'b', 'c'}, {'d'}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid69 }
p0: { gid72 }
p1: { gid70, gid71 }
if (s2) then p0
if ((s0 OR s1)) then p1
)");
}

TEST_F(ClosureGlyphSegmenterTest, SegmentationWithAdditionalConditionOverlap) {
  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {'a'}, {{'f'}, {'i'}, {'f', 'i'}});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<SubsetDefinition> expected_segments = {{'f'}, {'i'}, {'f', 'i'}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid69 }
p0: { gid444, gid446 }
p1: { gid74 }
p2: { gid77 }
if ((s0 OR s2)) then p1
if ((s1 OR s2)) then p2
if ((s0 OR s1 OR s2)) then p0
)");
}

TEST_F(ClosureGlyphSegmenterTest, SegmentationWithFeatures) {
  SubsetDefinition smcp;
  smcp.feature_tags.insert(HB_TAG('s', 'm', 'c', 'p'));

  auto segmentation = segmenter.CodepointToGlyphSegments(roboto.get(), {'a'},
                                                         {{'b'}, {'c'}, smcp});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<SubsetDefinition> expected_segments = {{'b'}, {'c'}, smcp};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid69 }
p0: { gid70 }
p1: { gid71 }
p2: { gid563 }
p3: { gid562 }
p4: { gid561 }
if (s0) then p0
if (s1) then p1
if (s2) then p2
if (s0 AND s2) then p3
if (s1 AND s2) then p4
)");
}

TEST_F(ClosureGlyphSegmenterTest, SegmentationWithFeatures_DontMergeFeatures) {
  SubsetDefinition smcp;
  smcp.feature_tags.insert(HB_TAG('s', 'm', 'c', 'p'));

  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {'a'}, {{'b'}, {'c'}, smcp},
      MergeStrategy::Heuristic(10000));
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<SubsetDefinition> expected_segments = {{'b', 'c'}, {}, smcp};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid69 }
p0: { gid70, gid71 }
p1: { gid563 }
p2: { gid561, gid562 }
if (s0) then p0
if (s2) then p1
if (s0 AND s2) then p2
)");
}

TEST_F(ClosureGlyphSegmenterTest, AndCondition) {
  auto segmentation =
      segmenter.CodepointToGlyphSegments(roboto.get(), {'a'}, {{'f'}, {'i'}});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<SubsetDefinition> expected_segments = {{'f'}, {'i'}};
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

TEST_F(ClosureGlyphSegmenterTest, OrCondition) {
  auto segmentation = segmenter.CodepointToGlyphSegments(roboto.get(), {'a'},
                                                         {{0xc1}, {0x106}});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<SubsetDefinition> expected_segments = {{0xc1}, {0x106}};
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

TEST_F(ClosureGlyphSegmenterTest, MergeBase_ViaConditions) {
  // {e, f} is too small, the merger should select {i, l} to merge since
  // there is a dependency between these two. The result should have no
  // conditional patches since ligatures will have been brought into the merged
  // {e, f, i, l}
  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {},
      {{'a', 'b', 'd'}, {'e', 'f'}, {'j', 'k', 'm', 'n'}, {'i', 'l'}},
      MergeStrategy::Heuristic(370));
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<SubsetDefinition> expected_segments = {
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

TEST_F(ClosureGlyphSegmenterTest, MergeBases) {
  // {e, f} is too smal, since no conditional patches exist it should merge with
  // the next available base which is {'j', 'k'}
  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {},
      {{'a', 'b', 'd'}, {'e', 'f'}, {'j', 'k'}, {'m', 'n', 'o', 'p'}},
      MergeStrategy::Heuristic(370));
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<SubsetDefinition> expected_segments = {
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

TEST_F(ClosureGlyphSegmenterTest, MergeBases_MaxSize) {
  // {e, f} is too small, since no conditional patches exist it will merge with
  // the next available base which is {'m', 'n', 'o', 'p'}. However that patch
  // is too large, so the next one {j, k} will actually be chosen.
  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {},
      {{'a', 'b', 'd'}, {'e', 'f'}, {'m', 'n', 'o', 'p'}, {'j', 'k'}},
      MergeStrategy::Heuristic(370, 700));
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<SubsetDefinition> expected_segments = {
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

TEST_F(ClosureGlyphSegmenterTest, MixedAndOr) {
  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {'a'}, {{'f', 0xc1}, {'i', 0x106}});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<SubsetDefinition> expected_segments = {{'f', 0xc1}, {'i', 0x106}};
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

TEST_F(ClosureGlyphSegmenterTest, UnmappedGlyphs_FallbackSegment) {
  auto segmentation = segmenter.CodepointToGlyphSegments(
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

TEST_F(ClosureGlyphSegmenterTest, UnmappedGlyphs_FindConditions) {
  auto segmentation = segmenter_find_conditions.CodepointToGlyphSegments(
      noto_nastaliq_urdu.get(), {},
      {{0x20}, {0x62a}, {0x62b}, {0x62c}, {0x62d}}, std::nullopt);
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  ASSERT_TRUE(segmentation->UnmappedGlyphs().empty())
      << segmentation->UnmappedGlyphs().ToString();

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid1 }
p1: { gid3, gid9, gid155 }
p2: { gid4, gid10, gid156 }
p3: { gid5, gid6, gid11, gid157 }
p4: { gid158 }
p5: { gid12, gid13, gid24, gid30, gid38, gid39, gid57, gid59, gid62, gid68, gid139, gid140, gid153, gid172 }
p6: { gid47, gid64, gid73, gid74, gid75, gid76, gid77, gid83, gid111, gid149, gid174, gid190, gid191 }
p7: { gid14, gid33, gid60, gid91, gid112, gid145, gid152 }
if (s0) then p0
if (s1) then p1
if (s2) then p2
if (s3) then p3
if (s4) then p4
if ((s1 OR s2)) then p5
if ((s3 OR s4)) then p7
if ((s1 OR s2 OR s3 OR s4)) then p6
)");
}

TEST_F(ClosureGlyphSegmenterTest, UnmappedGlyphs_FindConditions_IsFallback) {
  // Here the found conditions are equal to the fallback segment, this ensures
  // everything works properly in this case.
  auto segmentation = segmenter_find_conditions.CodepointToGlyphSegments(
      noto_nastaliq_urdu.get(), {}, {{0x62a}, {0x62b}, {0x62c}, {0x62d}},
      std::nullopt);
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();
  ASSERT_TRUE(segmentation->UnmappedGlyphs().empty())
      << segmentation->UnmappedGlyphs().ToString();
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

TEST_F(ClosureGlyphSegmenterTest,
       UnmappedGlyphs_FallbackSegmentMovedToInitFont) {
  auto segmentation = segmenter_move_to_init_font.CodepointToGlyphSegments(
      noto_nastaliq_urdu.get(), {}, {{0x62a}, {0x62b}, {0x62c}, {0x62d}},
      btree_map<SegmentSet, MergeStrategy>{});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  ASSERT_EQ(segmentation->UnmappedGlyphs().size(), 0);

  ASSERT_EQ(
      segmentation->ToString(),
      R"(initial font: { gid0, gid47, gid64, gid73, gid74, gid75, gid76, gid77, gid83, gid111, gid149, gid174, gid190, gid191 }
p0: { gid3, gid9, gid155 }
p1: { gid4, gid10, gid156 }
p2: { gid5, gid6, gid11, gid157 }
p3: { gid158 }
p4: { gid12, gid13, gid24, gid30, gid38, gid39, gid57, gid59, gid62, gid68, gid139, gid140, gid153, gid172 }
p5: { gid14, gid33, gid60, gid91, gid112, gid145, gid152 }
if (s0) then p0
if (s1) then p1
if (s2) then p2
if (s3) then p3
if ((s0 OR s1)) then p4
if ((s2 OR s3)) then p5
)");
}

TEST_F(ClosureGlyphSegmenterTest, FullRoboto_WithFeatures) {
  auto codepoints = common::FontHelper::ToCodepointsSet(roboto.get());

  uint32_t num_segments = 412;
  uint32_t per_group = codepoints.size() / num_segments;
  uint32_t remainder = codepoints.size() % num_segments;

  std::vector<SubsetDefinition> segments;
  int i = 0;
  SubsetDefinition segment;
  for (uint32_t cp : codepoints) {
    segment.codepoints.insert(cp);

    uint32_t group_size = per_group + (remainder > 0 ? 1 : 0);
    if (++i % group_size == 0) {
      segments.push_back(segment);
      segment.Clear();
      if (remainder > 0) {
        remainder--;
      }
    }
  }

  SubsetDefinition smcp;
  smcp.feature_tags.insert(HB_TAG('s', 'm', 'c', 'p'));
  segments.push_back(smcp);

  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {}, segments, MergeStrategy::Heuristic(4000, 12000));
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();
}

TEST_F(ClosureGlyphSegmenterTest, CostRequiresFrequencies) {
  UnicodeFrequencies frequencies;
  auto s = MergeStrategy::CostBased(std::move(frequencies));
  ASSERT_TRUE(absl::IsInvalidArgument(s.status())) << s.status();
}

TEST_F(ClosureGlyphSegmenterTest, SimpleSegmentation_CostStrategy) {
  UnicodeFrequencies frequencies{
      {{' ', ' '}, 100}, {{'a', 'a'}, 95}, {{'b', 'b'}, 95}, {{'c', 'c'}, 95},
      {{'d', 'd'}, 95},  {{'e', 'e'}, 95}, {{'f', 'f'}, 90}, {{'g', 'g'}, 90},
      {{'h', 'h'}, 90},  {{'i', 'i'}, 90}, {{'j', 'j'}, 90}, {{'k', 'k'}, 5},
      {{'l', 'l'}, 5},   {{'m', 'm'}, 5},  {{'n', 'n'}, 5},  {{'o', 'o'}, 5}};

  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {},
      {{'a', 'b', 'c', 'd', 'e'},
       {'f', 'g', 'h', 'i', 'j'},
       {'k', 'l', 'm', 'n', 'o'}},
      *MergeStrategy::CostBased(std::move(frequencies)));
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  // It's expected that s0 and s1 are merged together to reduce network
  // overhead.
  std::vector<SubsetDefinition> expected_segments = {
      {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j'},
      {},
      {'k', 'l', 'm', 'n', 'o'}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid69, gid70, gid71, gid72, gid73, gid74, gid75, gid76, gid77, gid78, gid444, gid446 }
p1: { gid79, gid80, gid81, gid82, gid83 }
p2: { gid445, gid447 }
if (s0) then p0
if (s2) then p1
if (s0 AND s2) then p2
)");
}

TEST_F(ClosureGlyphSegmenterTest,
       SimpleSegmentation_CostStrategy_GroupMinimums) {
  UnicodeFrequencies frequencies1{
      {{' ', ' '}, 100},
      // Everything unspecified defaults to a count of 1.
  };

  // Base line, nothing is merged
  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {},
      {{'a', 'b', 'c'}, {'f', 'g', 'h'}, {'k', 'l', 'm', 'n', 'o'}},
      *MergeStrategy::CostBased(std::move(frequencies1), 75, 3));
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<SubsetDefinition> expected_segments = {
      {'k', 'l', 'm', 'n', 'o'}, {'a', 'b', 'c'}, {'f', 'g', 'h'}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid79, gid80, gid81, gid82, gid83 }
p1: { gid69, gid70, gid71 }
p2: { gid74, gid75, gid76 }
p3: { gid445, gid447 }
if (s0) then p0
if (s1) then p1
if (s2) then p2
if (s0 AND s2) then p3
)");

  // With higher group minimums the two smaller segments are merged together
  UnicodeFrequencies frequencies2{
      {{' ', ' '}, 100},
      // Everything unspecified defaults to a count of 1.
  };
  segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {},
      {{'a', 'b', 'c'}, {'f', 'g', 'h'}, {'k', 'l', 'm', 'n', 'o'}},
      *MergeStrategy::CostBased(std::move(frequencies2), 75, 5));
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  expected_segments = {
      {'k', 'l', 'm', 'n', 'o'}, {'a', 'b', 'c', 'f', 'g', 'h'}, {}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid79, gid80, gid81, gid82, gid83 }
p1: { gid69, gid70, gid71, gid74, gid75, gid76 }
p2: { gid445, gid447 }
if (s0) then p0
if (s1) then p1
if (s0 AND s1) then p2
)");
}

TEST_F(ClosureGlyphSegmenterTest, CustomOverhead_CostStrategy) {
  UnicodeFrequencies frequencies{
      {{' ', ' '}, 100}, {{'a', 'a'}, 95}, {{'b', 'b'}, 95}, {{'c', 'c'}, 95},
      {{'d', 'd'}, 95},  {{'e', 'e'}, 95}, {{'f', 'f'}, 90}, {{'g', 'g'}, 90},
      {{'h', 'h'}, 90},  {{'i', 'i'}, 90}, {{'j', 'j'}, 90}, {{'k', 'k'}, 5},
      {{'l', 'l'}, 5},   {{'m', 'm'}, 5},  {{'n', 'n'}, 5},  {{'o', 'o'}, 5}};

  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {},
      {{'a', 'b', 'c', 'd', 'e'},
       {'f', 'g', 'h', 'i', 'j'},
       {'k', 'l', 'm', 'n', 'o'}},
      *MergeStrategy::CostBased(std::move(frequencies), 7500));
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  // Very high per request overhead incentivizes merging everything
  std::vector<SubsetDefinition> expected_segments = {
      {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
       'o'},
      {},
      {}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid69, gid70, gid71, gid72, gid73, gid74, gid75, gid76, gid77, gid78, gid79, gid80, gid81, gid82, gid83, gid444, gid445, gid446, gid447 }
if (s0) then p0
)");
}

TEST_F(ClosureGlyphSegmenterTest, NonDisjointCodepoints) {
  UnicodeFrequencies frequencies{{{'a', 'a'}, 10}};
  auto s = segmenter.CodepointToGlyphSegments(
      roboto.get(), {}, {{'a', 'd'}, {'d', 'c'}},
      *MergeStrategy::CostBased(std::move(frequencies)));
  ASSERT_FALSE(s.ok());
  EXPECT_EQ(s.status().code(), absl::StatusCode::kInvalidArgument)
      << s.status();
}

TEST_F(ClosureGlyphSegmenterTest, SimpleSegmentation_PatchMerge) {
  UnicodeFrequencies frequencies{
      {{' ', ' '}, 1000},
      {{'a', 'a'}, 1000},
      {{'!', '!'}, 999},
      {{0x203C, 0x203C}, 1}  // !!
  };

  MergeStrategy strategy =
      *MergeStrategy::CostBased(std::move(frequencies), 75, 1);
  strategy.SetUsePatchMerges(true);

  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {}, {{'a'}, {'!'}, {0x203C}}, strategy);
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<SubsetDefinition> expected_segments = {{'a'}, {'!'}, {0x203C}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid989 }
p1: { gid5, gid69 }
if (s2) then p0
if ((s0 OR s1 OR s2)) then p1
)");
}

TEST_F(ClosureGlyphSegmenterTest, SimpleSegmentation_NoPatchMerge) {
  UnicodeFrequencies frequencies{
      {{' ', ' '}, 1000},
      {{'a', 'a'}, 1000},
      {{'!', '!'}, 999},
      {{0x203C, 0x203C}, 1}  // !!
  };

  MergeStrategy strategy =
      *MergeStrategy::CostBased(std::move(frequencies), 75, 1);
  strategy.SetUsePatchMerges(false);

  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {}, {{'a'}, {'!'}, {0x203C}}, strategy);
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  // Because the condition for ! OR !! has high probability it get's merged with
  // a
  std::vector<SubsetDefinition> expected_segments = {
      {'a', '!', 0x203c}, {}, {}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid5, gid69, gid989 }
if (s0) then p0
)");
}

TEST_F(ClosureGlyphSegmenterTest, SimpleSegmentation_NoCostCutoff) {
  UnicodeFrequencies frequencies{
      {{' ', ' '}, 100},
      {{'a', 'a'}, 95},
      {{'b', 'b'}, 1},
      {{'c', 'c'}, 1},
      {{'d', 'd'}, 1},
      // Pairs - setup so that b, c, d are always occuring together.
      {{'b', 'c'}, 1},
      {{'b', 'd'}, 1},
      {{'c', 'd'}, 1},
  };

  auto strategy =
      *MergeStrategy::BigramCostBased(std::move(frequencies), 75, 1);
  strategy.SetOptimizationCutoffFraction(0.0);

  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {}, {{'a'}, {'b'}, {'c'}, {'d'}}, std::move(strategy));
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  // No Optimization Cutoff, b, c, and d are merged
  // It's expected that s0 and s1 are merged together to reduce network
  // overhead.
  std::vector<SubsetDefinition> expected_segments = {
      {'a'},
      {'b', 'c', 'd'},
      {},
      {},
  };
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid69 }
p1: { gid70, gid71, gid72 }
if (s0) then p0
if (s1) then p1
)");
}

TEST_F(ClosureGlyphSegmenterTest, SimpleSegmentation_WithCostCutoff) {
  UnicodeFrequencies frequencies{
      {{' ', ' '}, 100},
      {{'a', 'a'}, 95},
      {{'b', 'b'}, 1},
      {{'c', 'c'}, 1},
      {{'d', 'd'}, 1},
      // Pairs - setup so that b, c, d are always occuring together.
      {{'b', 'c'}, 1},
      {{'b', 'd'}, 1},
      {{'c', 'd'}, 1},
  };

  auto strategy =
      *MergeStrategy::BigramCostBased(std::move(frequencies), 75, 1);
  strategy.SetOptimizationCutoffFraction(0.05);

  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {}, {{'a'}, {'b'}, {'c'}, {'d'}}, std::move(strategy));
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  // With Optimization Cutoff, b, c, and d are not merged since
  // optimization is disabled for them.
  std::vector<SubsetDefinition> expected_segments = {
      {'a'},
      {'b'},
      {'c'},
      {'d'},
  };
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid69 }
p1: { gid70 }
p2: { gid71 }
p3: { gid72 }
if (s0) then p0
if (s1) then p1
if (s2) then p2
if (s3) then p3
)");
}

TEST_F(ClosureGlyphSegmenterTest,
       SimpleSegmentation_WithCostCutoffAndMinGroup) {
  UnicodeFrequencies frequencies{
      {{' ', ' '}, 100},
      {{'a', 'a'}, 95},
      {{'b', 'b'}, 1},
      {{'c', 'c'}, 1},
      {{'d', 'd'}, 1},
      // Pairs - setup so that b, c, d are always occuring together.
      {{'b', 'c'}, 1},
      {{'b', 'd'}, 1},
      {{'c', 'd'}, 1},
  };

  auto strategy =
      *MergeStrategy::BigramCostBased(std::move(frequencies), 75, 2);
  strategy.SetOptimizationCutoffFraction(0.05);

  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {}, {{'a'}, {'b'}, {'c'}, {'d'}}, std::move(strategy));
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  // b, c, and d have optimization cutoff but will still be merged up to the
  // min group size of 2.
  std::vector<SubsetDefinition> expected_segments = {
      {'a', 'b'},
      {},
      {'c', 'd'},
      {},
  };
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid69, gid70 }
p1: { gid71, gid72 }
if (s0) then p0
if (s2) then p1
)");
}

TEST_F(ClosureGlyphSegmenterTest, TotalCost) {
  UnicodeFrequencies frequencies{
      {{' ', ' '}, 100}, {{'a', 'a'}, 95}, {{'b', 'b'}, 1},
      {{'c', 'c'}, 1},   {{'d', 'd'}, 50}, {{'e', 'e'}, 25},
  };
  UnigramProbabilityCalculator calculator(std::move(frequencies));

  // Basic no segment case.
  GlyphSegmentation segmentation1({'a', 'b', 'c'}, {}, {});
  auto sc =
      GlyphSegmentation::GroupsToSegmentation({}, {}, {}, {}, segmentation1);
  ASSERT_TRUE(sc.ok()) << sc;

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY);
  SegmentationCost base_cost =
      *segmenter.TotalCost(roboto.get(), segmentation1, calculator);
  ASSERT_GT(base_cost.total_cost, 1000);
  ASSERT_EQ(base_cost.total_cost, base_cost.cost_for_non_segmented);
  ASSERT_LT(base_cost.ideal_cost, base_cost.total_cost);

  // Add some patches
  GlyphSegmentation segmentation2({'a', 'b', 'c'}, {}, {});
  sc = GlyphSegmentation::GroupsToSegmentation({}, {},
                                               {
                                                   {0, {100, 101, 102}},
                                                   {1, {103, 104, 105}},
                                               },
                                               {}, segmentation2);
  ASSERT_TRUE(sc.ok()) << sc;

  std::vector<SubsetDefinition> segments{
      {'d'},
      {'e'},
  };
  segmentation2.CopySegments(segments);

  SegmentationCost with_patches_cost =
      *segmenter.TotalCost(roboto.get(), segmentation2, calculator);
  ASSERT_GT(with_patches_cost.total_cost, base_cost.total_cost + 400);
  ASSERT_LT(with_patches_cost.ideal_cost, with_patches_cost.total_cost);
}

TEST_F(ClosureGlyphSegmenterTest, NoGlyphSegments_CostMerging) {
  // This test sets up a case where a segment ends up with no glyphs
  // because the glyphs for it are part of a disjunctive condition
  // In this case these segments should be effectively disabled for the
  // analysis and final output.

  UnicodeFrequencies frequencies{
      {{'A', 'A'}, 1000}, {{'C', 'C'}, 1000}, {{0x106, 0x106}, 1}, /* Cacute */
  };

  MergeStrategy strategy =
      *MergeStrategy::CostBased(std::move(frequencies), 75, 1);
  strategy.SetOptimizationCutoffFraction(0.0);
  strategy.SetUsePatchMerges(true);

  auto segmentation =
      segmenter.CodepointToGlyphSegments(roboto.get(), {},
                                         {
                                             {'A'}, {'C'}, {0x106}, /* Cacute */
                                         },
                                         strategy);
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  // The initial conditions are:
  // {A} -> p0
  // {B} -> p1
  // {C, Cacute} -> p2
  std::vector<SubsetDefinition> expected_segments = {
      {'A'},
      {'C'},
      {0x106},
  };
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid117, gid700 }
p1: { gid37, gid39 }
if (s2) then p0
if ((s0 OR s1 OR s2)) then p1
)");
}

TEST_F(ClosureGlyphSegmenterTest, InitNoGlyphSegments_CostMerging) {
  // This test sets up a case where a segment ends up with no glyphs
  // because the glyphs for it are already part of the init font.
  // In this case these segments should be effectively disabled for the
  // analysis and final output.

  UnicodeFrequencies frequencies{
      {{'A', 'A'}, 100},
      {{'B', 'B'}, 100},
      {{'C', 'C'}, 100},
  };

  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {0x106 /* Cacute */},
      {
          {'A'},
          {'B'},
          {'C'},
      },
      *MergeStrategy::CostBased(std::move(frequencies)));
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  // It's expected that s0 and s1 are merged together to reduce network
  // overhead.
  std::vector<SubsetDefinition> expected_segments = {
      {'A', 'B'},
      {},
      {},  // C glyph was already pulled in to the init font, so no merge
  };
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  // And it shouldn't be present in the segmenation conditions,
  // so no s2 should appear in the conditions.
  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid39, gid117, gid700 }
p0: { gid37, gid38 }
if (s0) then p0
)");
}

TEST_F(ClosureGlyphSegmenterTest, InitFontMerging) {
  // In this test we enable merging of segments into the init font
  UnicodeFrequencies frequencies{
      {{'a', 'a'}, 100},
      {{'b', 'b'}, 50},
      {{'c', 'c'}, 50},
      {{'d', 'd'}, 100},

      // b and c co-occur
      {{'b', 'c'}, 50},
  };

  MergeStrategy strategy =
      *MergeStrategy::BigramCostBased(std::move(frequencies));
  strategy.SetInitFontMergeThreshold(-75);

  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {}, {{'a'}, {'d'}, {'b'}, {'c'}}, strategy);
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  //  'a' and 'd' will be moved to the init font, leaving only two segments 'b',
  //  'c'
  std::vector<SubsetDefinition> expected_segments = {
      {},
      {},
      {'b', 'c'},
      {},
  };
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid69, gid72 }
p0: { gid70, gid71 }
if (s2) then p0
)");
}

TEST_F(ClosureGlyphSegmenterTest, InitFontMerging_WithProbabilityThreshold) {
  // In this test we enable merging of segments into the init font
  UnicodeFrequencies frequencies{
      {{'a', 'a'}, 100},
      {{'b', 'b'}, 97},
      {{'c', 'c'}, 97},
      {{'d', 'd'}, 97},

      // b and c co-occur
      {{'b', 'c'}, 97},
  };

  MergeStrategy strategy =
      *MergeStrategy::BigramCostBased(std::move(frequencies));
  strategy.SetInitFontMergeThreshold(-75);
  strategy.SetInitFontMergeProbabilityThreshold(0.98);

  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {}, {{'a'}, {'d'}, {'b'}, {'c'}}, strategy);
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  // 'a' will be moved to the init font, leaving the other segments
  // to be merged together.
  std::vector<SubsetDefinition> expected_segments = {
      {},
      {'b', 'c', 'd'},
      {},
      {},
  };
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid69 }
p0: { gid70, gid71, gid72 }
if (s1) then p0
)");
}

TEST_F(ClosureGlyphSegmenterTest,
       InitFontMerging_DisjunctiveCheckedIndividually) {
  UnicodeFrequencies frequencies{
      {{'A', 'A'}, 100},
      {{'C', 'C'}, 100},
      {{'B', 'B'}, 1},
      {{0x106, 0x106}, 1},  // Cacute
  };

  MergeStrategy strategy =
      *MergeStrategy::BigramCostBased(std::move(frequencies), 75, 1);
  strategy.SetInitFontMergeThreshold(-75);

  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {}, {{'A'}, {'B'}, {'C'}, {0x106}}, strategy);
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  /*
  Before Merge:
  initial font: { gid0 }
  p0: { gid37 }
  p1: { gid38 }
  p2: { gid117, gid700 }
  p3: { gid39 } # this is the 'C' glyph
  if (s0) then p0
  if (s2) then p1
  if (s3) then p2
  if ((s1 OR s3)) then p3

  The init move should check and move C individually despite it not
  having it's own exclusive patch (due to it being in a disjunctive condition).
  Cacute will not be moved since it's low probability.
  */
  std::vector<SubsetDefinition> expected_segments = {{}, {}, {'B'}, {0x106}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid37, gid39 }
p0: { gid38 }
p1: { gid117, gid700 }
if (s2) then p0
if (s3) then p1
)");
}

TEST_F(ClosureGlyphSegmenterTest, InitFontMerging_CommonGlyphs) {
  UnicodeFrequencies frequencies{
      {{'A', 'A'}, 1},
      {{'C', 'C'}, 1},
      {{0x106, 0x106}, 100},  // Cacute (contains C glyph)
  };

  MergeStrategy strategy = *MergeStrategy::CostBased(std::move(frequencies));
  strategy.SetInitFontMergeThreshold(-75);

  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {}, {{0x106}, {'A'}, {'C'}}, strategy);
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<SubsetDefinition> expected_segments = {
      {},
      {'A'},
      {},
  };
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  // C get's covered by the Cacute merge into init, so only A is left in the
  // patch.
  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid39, gid117, gid700 }
p0: { gid37 }
if (s1) then p0
)");
}

TEST_F(ClosureGlyphSegmenterTest, MultipleMergeGroups) {
  UnicodeFrequencies group1_freq{
      {{' ', ' '}, 100}, {{'a', 'a'}, 100}, {{'b', 'b'}, 100}, {{'c', 'c'}, 95},
      {{'d', 'd'}, 95},  {{'e', 'e'}, 5},   {{'f', 'f'}, 5},   {{'o', 'o'}, 5}};

  UnicodeFrequencies group2_freq{
      {{' ', ' '}, 100}, {{'g', 'g'}, 100}, {{'h', 'h'}, 90},
      {{'i', 'i'}, 90},  {{'j', 'j'}, 90},  {{'k', 'k'}, 5},
      {{'l', 'l'}, 5},   {{'m', 'm'}, 5},   {{'n', 'n'}, 5},
  };

  // {a} is shared
  // {b, g} is ungrouped
  btree_map<SegmentSet, MergeStrategy> merge_groups{
      {{0, 2, 3, 4, 5, 14},
       *MergeStrategy::CostBased(std::move(group1_freq), 75, 1)},
      {{0, 7, 8, 9, 10, 11, 12, 13},
       *MergeStrategy::CostBased(std::move(group2_freq), 75, 2)},
  };

  auto segmentation = segmenter.CodepointToGlyphSegments(roboto.get(), {},
                                                         {{'a'},
                                                          {'b'},
                                                          {'c'},
                                                          {'d'},
                                                          {'e'},
                                                          {'f'},
                                                          {'g'},
                                                          {'h'},
                                                          {'i'},
                                                          {'j'},
                                                          {'k'},
                                                          {'l'},
                                                          {'m'},
                                                          {'n'},
                                                          {'o'}},
                                                         merge_groups);
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<SubsetDefinition> expected_segments = {
      // Group 1
      {'c', 'd'},
      {},
      {'e'},
      {'f'},
      {'o'},
      // Group 2
      {'h', 'i', 'j'},
      {},
      {},
      {'k', 'l'},
      {},
      {'m', 'n'},
      {},
      // Shared
      {'a'},
      // Ungrouped
      {'b'},
      {'g'},
  };
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid71, gid72 }
p1: { gid73 }
p2: { gid74 }
p3: { gid83 }
p4: { gid76, gid77, gid78 }
p5: { gid79, gid80 }
p6: { gid81, gid82 }
p7: { gid69 }
p8: { gid70 }
p9: { gid75 }
p10: { gid444, gid446 }
p11: { gid445, gid447 }
if (s0) then p0
if (s2) then p1
if (s3) then p2
if (s4) then p3
if (s5) then p4
if (s8) then p5
if (s10) then p6
if (s12) then p7
if (s13) then p8
if (s14) then p9
if (s3 AND s5) then p10
if (s3 AND s8) then p11
)");
}

TEST_F(ClosureGlyphSegmenterTest, MultipleMergeGroups_InitFontMove) {
  UnicodeFrequencies group1_freq{
      {{' ', ' '}, 100}, {{'a', 'a'}, 100}, {{'b', 'b'}, 100}, {{'c', 'c'}, 95},
      {{'d', 'd'}, 95},  {{'e', 'e'}, 5},   {{'f', 'f'}, 5},   {{'o', 'o'}, 5}};

  UnicodeFrequencies group2_freq{
      {{' ', ' '}, 100}, {{'g', 'g'}, 100}, {{'h', 'h'}, 90},
      {{'i', 'i'}, 90},  {{'j', 'j'}, 90},  {{'k', 'k'}, 5},
      {{'l', 'l'}, 4},   {{'m', 'm'}, 3},   {{'n', 'n'}, 2},
  };

  // {a} is shared
  // {b, g} is ungrouped
  MergeStrategy s1 = *MergeStrategy::CostBased(std::move(group1_freq), 75, 1);
  s1.SetInitFontMergeThreshold(-70);
  s1.SetOptimizationCutoffFraction(0.50);

  MergeStrategy s2 = *MergeStrategy::CostBased(std::move(group2_freq), 75, 2);
  s2.SetInitFontMergeThreshold(std::nullopt);

  btree_map<SegmentSet, MergeStrategy> merge_groups{
      {{0, 1, 2, 3, 4, 5, 6}, s1},
      {{0, 1, 7, 8, 9, 10, 11, 12, 13, 14}, s2},
  };

  auto segmentation = segmenter.CodepointToGlyphSegments(roboto.get(), {},
                                                         {{'a'},
                                                          {'b'},
                                                          {'c'},
                                                          {'d'},
                                                          {'e'},
                                                          {'f'},
                                                          {'g'},
                                                          {'h'},
                                                          {'i'},
                                                          {'j'},
                                                          {'k'},
                                                          {'l'},
                                                          {'m'},
                                                          {'n'},
                                                          {'o'}},
                                                         merge_groups);
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  // Only segments from the groups that set a threshold are eligible to be moved
  // to the init font. so {g, h, i} will not be moved despite otherwise being
  // good candidates. Segments that appear in more than one merge group (ie, s0,
  // s1) are still candidates to be moved to the init font.
  std::vector<SubsetDefinition> expected_segments = {
      // Group 1
      {},  // c
      {},  // d
      {'e'},
      {'f'},
      {'g'},
      // Group 2
      {'h', 'i', 'j'},
      {},  // i
      {},  // j
      {'k', 'l'},
      {},  // l
      {'m', 'n'},
      {},  // n
      {'o'},
      // Shared
      {},  // a
      {},  // b
  };
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid69, gid70, gid71, gid72 }
p0: { gid73 }
p1: { gid74 }
p2: { gid75 }
p3: { gid76, gid77, gid78 }
p4: { gid79, gid80 }
p5: { gid81, gid82 }
p6: { gid83 }
p7: { gid444, gid446 }
p8: { gid445, gid447 }
if (s2) then p0
if (s3) then p1
if (s4) then p2
if (s5) then p3
if (s8) then p4
if (s10) then p5
if (s12) then p6
if (s3 AND s5) then p7
if (s3 AND s8) then p8
)");
}

TEST_F(ClosureGlyphSegmenterTest, MultipleMergeGroups_CompositesRespectGroups) {
  UnicodeFrequencies group1_freq{
      {{' ', ' '}, 100},
      {{'f', 'f'}, 100},
      {{'g', 'g'}, 5},
  };

  UnicodeFrequencies group2_freq{
      {{' ', ' '}, 100},
      {{'i', 'i'}, 100},
      {{'j', 'j'}, 5},
  };

  btree_map<SegmentSet, MergeStrategy> merge_groups{
      {{0, 1}, *MergeStrategy::CostBased(std::move(group1_freq), 75, 1)},
      {{2, 3}, *MergeStrategy::CostBased(std::move(group2_freq), 75, 1)},
  };

  auto segmentation = segmenter.CodepointToGlyphSegments(roboto.get(), {},
                                                         {
                                                             {'f'},
                                                             {'g'},
                                                             {'i'},
                                                             {'j'},
                                                         },
                                                         merge_groups);
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  // f + i would normally be a good merge, but here it's skipped since it
  // spans merge groups.
  std::vector<SubsetDefinition> expected_segments = {
      // Group 1
      {'f'},
      {'g'},
      {'i'},
      {'j'},
  };
  ASSERT_EQ(segmentation->Segments(), expected_segments);
  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid74 }
p1: { gid75 }
p2: { gid77 }
p3: { gid78 }
p4: { gid444, gid446 }
if (s0) then p0
if (s1) then p1
if (s2) then p2
if (s3) then p3
if (s0 AND s2) then p4
)");
}

TEST_F(ClosureGlyphSegmenterTest, MultipleMergeGroups_Heuristic) {
  btree_map<SegmentSet, MergeStrategy> merge_groups{
      {{0, 1}, MergeStrategy::Heuristic(10000)},
      {{2, 3}, MergeStrategy::Heuristic(10000)},
  };

  auto segmentation = segmenter.CodepointToGlyphSegments(roboto.get(), {},
                                                         {
                                                             {'f'},
                                                             {'g'},
                                                             {'i'},
                                                             {'j'},
                                                         },
                                                         merge_groups);
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  // f + i would normally be a good merge, but here it's skipped since it
  // spans merge groups.
  std::vector<SubsetDefinition> expected_segments = {
      // Group 1
      {'f', 'g'},
      {},
      {'i', 'j'},
      {},
  };
  ASSERT_EQ(segmentation->Segments(), expected_segments);
  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid74, gid75 }
p1: { gid77, gid78 }
p2: { gid444, gid446 }
if (s0) then p0
if (s2) then p1
if (s0 AND s2) then p2
)");
}

TEST_F(ClosureGlyphSegmenterTest, CompositeMerge_Cutoff) {
  UnicodeFrequencies freq{
      {{' ', ' '}, 100}, {{'g', 'g'}, 100}, {{'j', 'j'}, 100},
      {{'f', 'f'}, 99},  {{'i', 'i'}, 99},
  };

  MergeStrategy strategy = *MergeStrategy::CostBased(std::move(freq), 75, 1);
  strategy.SetOptimizationCutoffFraction(0.50);
  auto segmentation = segmenter.CodepointToGlyphSegments(roboto.get(), {},
                                                         {
                                                             {'f'},
                                                             {'g'},
                                                             {'i'},
                                                             {'j'},
                                                         },
                                                         std::move(strategy));
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<SubsetDefinition> expected_segments = {
      // Group 1
      {'g', 'j'},
      {},
      {'f'},
      {'i'},
  };
  ASSERT_EQ(segmentation->Segments(), expected_segments);
  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid75, gid78 }
p1: { gid74 }
p2: { gid77 }
p3: { gid444, gid446 }
if (s0) then p0
if (s2) then p1
if (s3) then p2
if (s2 AND s3) then p3
)");
}

TEST_F(ClosureGlyphSegmenterTest, ConjunctiveAdditionalConditions) {
  // This test sets up a scenario where a conjunctive condition is subject to
  // additional conditions which are uncovered by a merge. It ensures that the
  // uncovered additional conditions are appropriately handled.

  SubsetDefinition smcp;
  smcp.feature_tags.insert(HB_TAG('s', 'm', 'c', 'p'));
  smcp.feature_tags.insert(HB_TAG('c', '2', 's', 'c'));

  auto segmentation = segmenter.CodepointToGlyphSegments(roboto.get(), {0xE4},
                                                         {
                                                             {0xD6},  // Ö
                                                             {0xF6},  // ö
                                                             smcp,
                                                         },
                                                         MergeStrategy::None());
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  // Note: gid839 is the small caps O dieresis, it's currently mapped with
  // condition 'if {smcp}', the true condition is 'if {smcp} AND {Ö or ö}'
  // but the closure segmenter does not currently discover composite conditions.
  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid69, gid106, gid670 }
p0: { gid51, gid660 }
p1: { gid83, gid687 }
p2: { gid477, gid563, gid822, gid839 }
if (s0) then p0
if (s1) then p1
if (s2) then p2
)");

  // Rerun segmentation with merging of Ö and ö allowed, should now get
  // the true condition.
  UnicodeFrequencies frequencies{
      {{0xD6, 0xD6}, 100},  // Ö
      {{0xF6, 0xF6}, 100},  // ö
  };
  segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {0xE4},
      {
          {0xD6},  // Ö
          {0xF6},  // ö
          smcp,
      },
      {
          {{0, 1}, *MergeStrategy::CostBased(std::move(frequencies), 75, 3)},
          {{2}, MergeStrategy::None()},
      });
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid69, gid106, gid670 }
p0: { gid51, gid83, gid660, gid687 }
p1: { gid563, gid822 }
p2: { gid477, gid839 }
if (s0) then p0
if (s2) then p1
if (s0 AND s2) then p2
)");
}

TEST_F(ClosureGlyphSegmenterTest, MultipleMergeGroups_PreGrouping) {
  UnicodeFrequencies freq{
      {{' ', ' '}, 100}, {{'d', 'd'}, 100}, {{'a', 'a'}, 60}, {{'e', 'e'}, 30},
      {{'b', 'b'}, 29},  {{'f', 'f'}, 28},  {{'c', 'c'}, 10}, {{'g', 'g'}, 9},
      {{'h', 'h'}, 5},   {{'i', 'i'}, 1},  // 8
  };

  MergeStrategy costs = *MergeStrategy::CostBased(std::move(freq), 0, 1);
  costs.SetPreClosureProbabilityThreshold(0.55);
  costs.SetPreClosureGroupSize(3);

  btree_map<SegmentSet, MergeStrategy> merge_groups{
      {{0, 1, 2, 3, 4, 5, 6, 7, 8}, costs},
      {{7, 8}, MergeStrategy::Heuristic(1)},
  };

  auto segmentation = segmenter.CodepointToGlyphSegments(roboto.get(), {},
                                                         {
                                                             {'a'},
                                                             {'b'},
                                                             {'c'},
                                                             {'d'},
                                                             {'e'},
                                                             {'f'},
                                                             {'g'},
                                                             {'h'},
                                                             {'i'},
                                                         },
                                                         merge_groups);
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  // d, a are above the pregrouping threshold so aren't grouped.
  // e, b, f, c, and g are below so are grouped into sets of 3.
  // h, i are shared between merge groups so don't participate in pregrouping.
  std::vector<SubsetDefinition> expected_segments = {
      // Group 1
      {'d'},
      {'a'},
      {'e', 'b', 'f'},  // pre merge
      {'c', 'g'},       // pre merge
      // Shared
      {'h'},
      {'i'},
  };
  ASSERT_EQ(segmentation->Segments(), expected_segments);
  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid72 }
p1: { gid69 }
p2: { gid70, gid73, gid74 }
p3: { gid71, gid75 }
p4: { gid76 }
p5: { gid77 }
p6: { gid444, gid446 }
if (s0) then p0
if (s1) then p1
if (s2) then p2
if (s3) then p3
if (s4) then p4
if (s5) then p5
if (s2 AND s5) then p6
)");
}

TEST_F(ClosureGlyphSegmenterTest, InitFontMergingAndFindConditions) {
  // In this test we enable merging of segments into the init font
  UnicodeFrequencies frequencies{
      /* s0 */ {{0x20, 0x20}, 1000},
      /* s1 */ {{0x62a, 0x62a}, 1000},
      /* s2 */ {{0x62b, 0x62b}, 1000},
      /* s3 */ {{0x62c, 0x62c}, 1},
      /* s4 */ {{0x62d, 0x62d}, 1},
  };

  MergeStrategy strategy =
      *MergeStrategy::BigramCostBased(std::move(frequencies));
  strategy.SetInitFontMergeThreshold(-75);

  auto segmentation = segmenter_find_conditions.CodepointToGlyphSegments(
      noto_nastaliq_urdu.get(), {},
      {{0x20}, {0x62a}, {0x62b}, {0x62c}, {0x62d}}, strategy);
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  // s0, 1, and 2 will be moved to init font.
  std::vector<SubsetDefinition> expected_segments = {
      {}, {}, {}, {0x62c, 0x62d}, {},
  };
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  // Complex condition if ((s1 OR s2 OR s3 OR s4)) then ... is moved to the init
  // font.
  ASSERT_EQ(
      segmentation->ToString(),
      R"(initial font: { gid0, gid1, gid3, gid4, gid9, gid10, gid12, gid13, gid24, gid30, gid38, gid39, gid57, gid59, gid62, gid68, gid139, gid140, gid153, gid155, gid156, gid172, gid174 }
p0: { gid5, gid6, gid11, gid14, gid33, gid47, gid60, gid64, gid73, gid74, gid75, gid76, gid77, gid83, gid91, gid111, gid112, gid145, gid149, gid152, gid157, gid158, gid190, gid191 }
if (s3) then p0
)");
}

// TODO(garretrieger): test that segments are excluded by init font segment. ie.
// if a segment is present in the init font then it should be cleared out in the
// segmentation.

// TODO(garretrieger): add test where or_set glyphs are moved back to unmapped
// due to found "additional conditions".

}  // namespace ift::encoder