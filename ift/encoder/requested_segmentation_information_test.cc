#include "ift/encoder/requested_segmentation_information.h"

#include <vector>

#include "gtest/gtest.h"
#include "ift/common/font_data.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/probability_bound.h"

using ift::config::PATCH;
using ift::common::CodepointSet;
using ift::common::SegmentSet;
using ift::common::FontData;
using ift::common::hb_face_unique_ptr;
using ift::common::make_hb_face;
using ift::freq::ProbabilityBound;

namespace ift::encoder {

class RequestedSegmentationInformationTest : public ::testing::Test {
 protected:
  RequestedSegmentationInformationTest() : roboto(make_hb_face(nullptr)) {
    roboto = from_file("ift/common/testdata/Roboto-Regular.ttf");
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
};

TEST_F(RequestedSegmentationInformationTest, SegmentsForCodepoints) {
  std::unique_ptr<GlyphClosureCache> cache = *GlyphClosureCache::Create(roboto.get());
  std::vector<Segment> segments{
      {{'a', 'b'}, ProbabilityBound::Zero()},
      {{'b', 'c'}, ProbabilityBound::Zero()},
      {{'d'}, ProbabilityBound::Zero()},
  };

  auto info_or =
      RequestedSegmentationInformation::Create(segments, {}, *cache, PATCH);
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
  std::unique_ptr<GlyphClosureCache> cache = *GlyphClosureCache::Create(roboto.get());
  std::vector<Segment> segments{
      {{'a'}, ProbabilityBound::Zero()},
      {{'b'}, ProbabilityBound::Zero()},
      {{'c'}, ProbabilityBound::Zero()},
  };

  auto info_or =
      RequestedSegmentationInformation::Create(segments, {}, *cache, PATCH);
  ASSERT_TRUE(info_or.ok());
  auto& info = *info_or;

  EXPECT_EQ(info->SegmentsForCodepoints({'a'}), (SegmentSet{0}));
  EXPECT_EQ(info->SegmentsForCodepoints({'b'}), (SegmentSet{1}));
  EXPECT_EQ(info->SegmentsForCodepoints({'c'}), (SegmentSet{2}));

  // Merge segment 1 into segment 0, new definition is {'a', 'b'}
  Segment merged{{'a', 'b'}, ProbabilityBound::Zero()};
  info->AssignMergedSegment(0, {1}, merged);

  EXPECT_EQ(info->SegmentsForCodepoints({'a'}), (SegmentSet{0}));
  EXPECT_EQ(info->SegmentsForCodepoints({'b'}), (SegmentSet{0}));
  EXPECT_EQ(info->SegmentsForCodepoints({'c'}), (SegmentSet{2}));
}

TEST_F(RequestedSegmentationInformationTest, IndexUpdatesOnReassignInit) {
  std::unique_ptr<GlyphClosureCache> cache = *GlyphClosureCache::Create(roboto.get());
  std::vector<Segment> segments{
      {{'a', 'b'}, ProbabilityBound::Zero()},
      {{'b', 'c'}, ProbabilityBound::Zero()},
  };

  auto info_or =
      RequestedSegmentationInformation::Create(segments, {}, *cache, PATCH);
  ASSERT_TRUE(info_or.ok());
  auto& info = *info_or;

  EXPECT_EQ(info->SegmentsForCodepoints({'a'}), (SegmentSet{0}));
  EXPECT_EQ(info->SegmentsForCodepoints({'b'}), (SegmentSet{0, 1}));
  EXPECT_EQ(info->SegmentsForCodepoints({'c'}), (SegmentSet{1}));

  // Reassign init subset to include 'b'. This should remove 'b' from all segments.
  SubsetDefinition new_init;
  new_init.codepoints = {'b'};
  ASSERT_TRUE(info->ReassignInitSubset(*cache, new_init).ok());

  EXPECT_EQ(info->SegmentsForCodepoints({'a'}), (SegmentSet{0}));
  EXPECT_EQ(info->SegmentsForCodepoints({'b'}), (SegmentSet{}));
  EXPECT_EQ(info->SegmentsForCodepoints({'c'}), (SegmentSet{1}));
}

}  // namespace ift::encoder
