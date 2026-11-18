#include "ift/encoder/requested_segmentation_information.h"

#include <vector>

#include "gtest/gtest.h"
#include "ift/common/bazel_data_file_resolver.h"
#include "ift/common/font_data.h"
#include "ift/common/test_font_loader.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/probability_bound.h"

using ift::common::BazelDataFileResolver;
using ift::common::CodepointSet;
using ift::common::DataFileResolver;
using ift::common::FontData;
using ift::common::hb_face_unique_ptr;
using ift::common::make_hb_face;
using ift::common::SegmentSet;
using ift::freq::ProbabilityBound;

namespace ift::encoder {

class RequestedSegmentationInformationTest : public ::testing::Test {
 protected:
  RequestedSegmentationInformationTest()
      : roboto(make_hb_face(nullptr)),
        resolver(*BazelDataFileResolver::CreateForTest()) {
    roboto = from_file("ift/common/testdata/Roboto-Regular.ttf");
  }

  hb_face_unique_ptr from_file(const char* filename) {
    auto loader = ift::common::TestFontLoader::Default().value();
    return loader->LoadFace(filename).value();
  }

  hb_face_unique_ptr roboto;
  std::shared_ptr<DataFileResolver> resolver;
};

TEST_F(RequestedSegmentationInformationTest, SegmentsForCodepoints) {
  std::unique_ptr<GlyphClosureCache> cache =
      *GlyphClosureCache::Create(roboto.get(), *resolver);
  std::vector<Segment> segments{
      {{'a', 'b'}},
      {{'b', 'c'}},
      {{'d'}},
  };

  auto info_or = RequestedSegmentationInformation::Create(segments, {}, *cache);
  ASSERT_TRUE(info_or.ok());
  auto& info = *info_or;

  EXPECT_EQ(info->SegmentsForCodepoints({'a'}), (SegmentSet{0}));
  EXPECT_EQ(info->SegmentsForCodepoints({'b'}), (SegmentSet{0, 1}));
  EXPECT_EQ(info->SegmentsForCodepoints({'c'}), (SegmentSet{1}));
  EXPECT_EQ(info->SegmentsForCodepoints({'d'}), (SegmentSet{2}));
  EXPECT_EQ(info->SegmentsForCodepoints({'e'}), (SegmentSet{}));
  EXPECT_EQ(info->SegmentsForCodepoints({'a', 'c'}), (SegmentSet{0, 1}));
  EXPECT_EQ(info->SegmentsForCodepoints({'a', 'd'}), (SegmentSet{0, 2}));
}

TEST_F(RequestedSegmentationInformationTest, IndexUpdatesOnMerge) {
  std::unique_ptr<GlyphClosureCache> cache =
      *GlyphClosureCache::Create(roboto.get(), *resolver);
  std::vector<Segment> segments{
      {{'a'}},
      {{'b'}},
      {{'c'}},
  };

  auto info_or = RequestedSegmentationInformation::Create(segments, {}, *cache);
  ASSERT_TRUE(info_or.ok());
  auto& info = *info_or;

  EXPECT_EQ(info->SegmentsForCodepoints({'a'}), (SegmentSet{0}));
  EXPECT_EQ(info->SegmentsForCodepoints({'b'}), (SegmentSet{1}));
  EXPECT_EQ(info->SegmentsForCodepoints({'c'}), (SegmentSet{2}));

  // Merge segment 1 into segment 0, new definition is {'a', 'b'}
  Segment merged{{'a', 'b'}};
  info->AssignMergedSegment(0, {1}, merged);

  EXPECT_EQ(info->SegmentsForCodepoints({'a'}), (SegmentSet{0}));
  EXPECT_EQ(info->SegmentsForCodepoints({'b'}), (SegmentSet{0}));
  EXPECT_EQ(info->SegmentsForCodepoints({'c'}), (SegmentSet{2}));
}

TEST_F(RequestedSegmentationInformationTest, IndexUpdatesOnReassignInit) {
  std::unique_ptr<GlyphClosureCache> cache =
      *GlyphClosureCache::Create(roboto.get(), *resolver);
  std::vector<Segment> segments{
      {{'a', 'b'}},
      {{'b', 'c'}},
  };

  auto info_or = RequestedSegmentationInformation::Create(segments, {}, *cache);
  ASSERT_TRUE(info_or.ok());
  auto& info = *info_or;

  EXPECT_EQ(info->SegmentsForCodepoints({'a'}), (SegmentSet{0}));
  EXPECT_EQ(info->SegmentsForCodepoints({'b'}), (SegmentSet{0, 1}));
  EXPECT_EQ(info->SegmentsForCodepoints({'c'}), (SegmentSet{1}));

  // Reassign init subset to include 'b'. This should remove 'b' from all
  // segments.
  SubsetDefinition new_init;
  new_init.codepoints = {'b'};
  ASSERT_TRUE(info->ReassignInitSubset(*cache, new_init).ok());

  EXPECT_EQ(info->SegmentsForCodepoints({'a'}), (SegmentSet{0}));
  EXPECT_EQ(info->SegmentsForCodepoints({'b'}), (SegmentSet{}));
  EXPECT_EQ(info->SegmentsForCodepoints({'c'}), (SegmentSet{1}));
}

TEST_F(RequestedSegmentationInformationTest, NonInitFontNonEmptyGlyphs_Glyf) {
  std::unique_ptr<GlyphClosureCache> cache =
      *GlyphClosureCache::Create(roboto.get(), *resolver);
  // ' ' -> gid 4 (empty in glyf), 'a' -> gid 69, 'b' -> gid 70.
  std::vector<Segment> segments{
      {{' '}},
      {{'a'}},
      {{'b'}},
  };

  auto info_or = RequestedSegmentationInformation::Create(segments, {}, *cache);
  ASSERT_TRUE(info_or.ok()) << info_or.status();
  auto& info = *info_or;

  EXPECT_EQ(info->NonInitFontGlyphs(), (ift::common::GlyphSet{4, 69, 70}));
  EXPECT_EQ(info->NonInitFontNonEmptyGlyphs(), (ift::common::GlyphSet{69, 70}));

  // Moving 'a' into the init font removes gid 69 from both sets.
  SubsetDefinition new_init;
  new_init.codepoints = {'a'};
  ASSERT_TRUE(info->ReassignInitSubset(*cache, new_init).ok());
  EXPECT_EQ(info->NonInitFontGlyphs(), (ift::common::GlyphSet{4, 70}));
  EXPECT_EQ(info->NonInitFontNonEmptyGlyphs(), (ift::common::GlyphSet{70}));
}

TEST_F(RequestedSegmentationInformationTest,
       NonInitFontNonEmptyGlyphs_GlyfAndGvar) {
  hb_face_unique_ptr roboto_vf =
      from_file("ift/common/testdata/Roboto[wdth,wght].ttf");
  std::unique_ptr<GlyphClosureCache> cache =
      *GlyphClosureCache::Create(roboto_vf.get(), *resolver);
  // 0x0002 -> gid 2 (empty in both glyf and gvar)
  // ' ' -> gid 4 (empty in glyf, non-empty in gvar)
  // 'a' -> gid 69 (non-empty in both)
  std::vector<Segment> segments{
      {{0x0002}},
      {{' '}},
      {{'a'}},
  };

  auto info_or = RequestedSegmentationInformation::Create(segments, {}, *cache);
  ASSERT_TRUE(info_or.ok()) << info_or.status();
  auto& info = *info_or;

  EXPECT_EQ(info->NonInitFontGlyphs(), (ift::common::GlyphSet{2, 4, 69}));
  EXPECT_EQ(info->NonInitFontNonEmptyGlyphs(), (ift::common::GlyphSet{4, 69}));
}

TEST_F(RequestedSegmentationInformationTest, NonInitFontNonEmptyGlyphs_Cff2) {
  hb_face_unique_ptr noto_vf =
      from_file("ift/common/testdata/NotoSansJP-VF.subset.otf");
  std::unique_ptr<GlyphClosureCache> cache =
      *GlyphClosureCache::Create(noto_vf.get(), *resolver);
  SubsetDefinition empty_gid_seg;
  empty_gid_seg.gids = {1};  // gid 1 is 0-byte charstring in CFF2
  std::vector<Segment> segments{
      Segment{empty_gid_seg},
      Segment{{'A'}},  // gid 34
  };

  auto info_or = RequestedSegmentationInformation::Create(segments, {}, *cache);
  ASSERT_TRUE(info_or.ok()) << info_or.status();
  auto& info = *info_or;

  EXPECT_EQ(info->NonInitFontGlyphs(), (ift::common::GlyphSet{1, 34}));
  EXPECT_EQ(info->NonInitFontNonEmptyGlyphs(), (ift::common::GlyphSet{34}));
}

}  // namespace ift::encoder
