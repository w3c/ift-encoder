#include "ift/encoder/closure_glyph_segmenter.h"

#include "common/font_data.h"
#include "common/font_helper.h"
#include "common/int_set.h"
#include "gtest/gtest.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/merge_strategy.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/probability_bound.h"
#include "ift/freq/unicode_frequencies.h"
#include "ift/freq/unigram_probability_calculator.h"

using common::CodepointSet;
using common::FontData;
using common::hb_face_unique_ptr;
using common::IntSet;
using common::make_hb_face;
using ift::freq::UnicodeFrequencies;
using ift::freq::UnigramProbabilityCalculator;

namespace ift::encoder {

class ClosureGlyphSegmenterTest : public ::testing::Test {
 protected:
  ClosureGlyphSegmenterTest()
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
  ClosureGlyphSegmenter segmenter;
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

TEST_F(ClosureGlyphSegmenterTest, SimpleSegmentation_DropsUneededSegment) {
  auto segmentation = segmenter.CodepointToGlyphSegments(roboto.get(), {'a'},
                                                         {{'b'}, {'c'}, {'a'}});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  std::vector<SubsetDefinition> expected_segments = {{'b'}, {'c'}, {'a'}};
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

TEST_F(ClosureGlyphSegmenterTest, SimpleSegmentation_AvoidsEmptyBases) {
  UnicodeFrequencies frequencies{
      {{' ', ' '}, 1000},
      {{'a', 'a'}, 1000},
      {{'!', '!'}, 999},
      {{0x203C, 0x203C}, 1}  // !!
                             // Everything unspecified defaults to a count of 1.
  };

  // Base line, nothing is merged
  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {}, {{'a'}, {'!'}, {0x203C}},
      *MergeStrategy::CostBased(std::move(frequencies), 75, 1));
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  // With the current implementation we don't expect these segments to be merged
  // together.
  //
  // {'!'} has no exclusive patch associated with it so it won't participate
  // in the base merging checks. 0x203C won't be merged due to it's low
  // frequency
  //
  // TODO(garretrieger): for this case to be correctly handled we need to check
  //                     all composite conditions not just those that interact
  //                     with the base segment. This will see the ('!' or
  //                     0x203c) as a cost reducing merge candidate.
  std::vector<SubsetDefinition> expected_segments = {{'a'}, {'!'}, {0x203C}};
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0 }
p0: { gid69 }
p1: { gid989 }
p2: { gid5 }
if (s0) then p0
if (s2) then p1
if ((s1 OR s2)) then p2
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
  auto sc = GlyphSegmentation::GroupsToSegmentation({}, {}, {}, segmentation1);
  ASSERT_TRUE(sc.ok()) << sc;

  ClosureGlyphSegmenter segmenter;
  SegmentationCost base_cost =
      *segmenter.TotalCost(roboto.get(), segmentation1, calculator);
  ASSERT_GT(base_cost.total_cost, 1000);
  ASSERT_EQ(base_cost.total_cost, base_cost.cost_for_non_segmented);
  ASSERT_LT(base_cost.ideal_cost, base_cost.total_cost);

  // Add some patches
  GlyphSegmentation segmentation2({'a', 'b', 'c'}, {}, {});
  sc = GlyphSegmentation::GroupsToSegmentation(
      {
          {{0}, {100, 101, 102}},
          {{1}, {103, 104, 105}},
      },
      {}, {}, segmentation2);
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
      {'C'},  // C glyph was already pulled in to the init font, so no merge
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
      {'C'},
  };
  ASSERT_EQ(segmentation->Segments(), expected_segments);

  // C get's covered by the Cacute merge into int, so only A is left in the
  // patch.
  ASSERT_EQ(segmentation->ToString(),
            R"(initial font: { gid0, gid39, gid117, gid700 }
p0: { gid37 }
if (s1) then p0
)");
}

// TODO(garretrieger): add test where or_set glyphs are moved back to unmapped
// due to found "additional conditions".

}  // namespace ift::encoder