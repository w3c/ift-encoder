#include "ift/encoder/glyph_closure_cache.h"

#include <vector>

#include "common/font_data.h"
#include "gtest/gtest.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/probability_bound.h"

using ift::proto::PATCH;


using absl::StatusOr;
using common::CodepointSet;
using common::FontData;
using common::GlyphSet;
using common::hb_face_unique_ptr;
using common::make_hb_face;
using common::SegmentSet;
using ift::freq::ProbabilityBound;

namespace ift::encoder {

class GlyphClosureCacheTest : public ::testing::Test {
 protected:
  GlyphClosureCacheTest() : roboto(make_hb_face(nullptr)) {
    roboto = from_file("common/testdata/Roboto-Regular.ttf");
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

TEST_F(GlyphClosureCacheTest, GlyphClosure) {
  GlyphClosureCache cache(roboto.get());
  auto closure = cache.GlyphClosure({'a'});
  ASSERT_TRUE(closure.ok());
  ASSERT_EQ(*closure, (GlyphSet{0, 69}));
}

TEST_F(GlyphClosureCacheTest, SegmentClosure) {
  GlyphClosureCache cache(roboto.get());
  std::vector<Segment> segments{
      {{'f'}, ProbabilityBound::Zero()},
      {{'i'}, ProbabilityBound::Zero()},
  };

  SubsetDefinition init;
  init.feature_tags = {HB_TAG('l', 'i', 'g', 'a')};
  auto info =
      RequestedSegmentationInformation::Create(segments, init, cache, PATCH);
  ASSERT_TRUE(info.ok());

  auto closure = cache.SegmentClosure(info->get(), {0});
  ASSERT_TRUE(closure.ok());
  ASSERT_EQ(*closure, (GlyphSet{0, 74 /* f */}));

  closure = cache.SegmentClosure(info->get(), {1});
  ASSERT_TRUE(closure.ok());
  ASSERT_EQ(*closure, (GlyphSet{0, 77 /* i */}));

  closure = cache.SegmentClosure(info->get(), {0, 1});
  ASSERT_TRUE(closure.ok());
  ASSERT_EQ(*closure, (GlyphSet{0, 74, 77, 444, 446}));
}

TEST_F(GlyphClosureCacheTest, HasAdditionalConditions) {
  GlyphClosureCache cache(roboto.get());
  std::vector<Segment> segments{
      {{'A'}, ProbabilityBound::Zero()},
      {{0xC1 /* Aacute*/}, ProbabilityBound::Zero()},
  };

  // s0 or s1 -> g37 (A)

  auto info =
      RequestedSegmentationInformation::Create(segments, {}, cache, PATCH);
  ASSERT_TRUE(info.ok());

  ASSERT_TRUE(*cache.HasAdditionalConditions(info->get(), {0}, {37 /* A */}));
  ASSERT_TRUE(*cache.HasAdditionalConditions(info->get(), {1}, {37 /* A */}));
  ASSERT_FALSE(
      *cache.HasAdditionalConditions(info->get(), {0, 1}, {37 /* A */}));

  ASSERT_FALSE(
      *cache.HasAdditionalConditions(info->get(), {0}, {668 /* Aacute */}));
  ASSERT_FALSE(
      *cache.HasAdditionalConditions(info->get(), {1}, {668 /* Aacute */}));
}

TEST_F(GlyphClosureCacheTest, HasAdditionalConditions_IncludesInitFont) {
  GlyphClosureCache cache(roboto.get());
  std::vector<Segment> segments{
      {{'A'}, ProbabilityBound::Zero()},
      {{0xC1 /* Aacute*/}, ProbabilityBound::Zero()},
  };

  SubsetDefinition init;
  init.feature_tags = {HB_TAG('c', '2', 's', 'c')};

  auto info =
      RequestedSegmentationInformation::Create(segments, init, cache, PATCH);
  ASSERT_TRUE(info.ok());

  // s0 or s1 -> g563 (smcap A)
  EXPECT_EQ(*cache.CodepointsToOrGids(*info->get(), {0}), (GlyphSet{37, 563}));
  EXPECT_EQ(*cache.CodepointsToOrGids(*info->get(), {1}), (GlyphSet{37, 563}));

  EXPECT_TRUE(*cache.HasAdditionalConditions(info->get(), {0}, {563}));
  EXPECT_TRUE(*cache.HasAdditionalConditions(info->get(), {1}, {563}));
  EXPECT_FALSE(*cache.HasAdditionalConditions(info->get(), {0, 1}, {563}));
}

TEST_F(GlyphClosureCacheTest, AnalyzeSegment) {
  GlyphClosureCache cache(roboto.get());
  std::vector<Segment> segments{
      {{'f'}, ProbabilityBound::Zero()},
      {{'i'}, ProbabilityBound::Zero()},
  };

  SubsetDefinition init;
  init.feature_tags = {HB_TAG('l', 'i', 'g', 'a')};
  auto info =
      RequestedSegmentationInformation::Create(segments, init, cache, PATCH);
  ASSERT_TRUE(info.ok());

  GlyphSet and_gids, or_gids, exclusive_gids;
  auto status = cache.AnalyzeSegment(*info->get(), {0}, and_gids, or_gids,
                                     exclusive_gids);
  ASSERT_TRUE(status.ok());

  EXPECT_EQ(exclusive_gids, (GlyphSet{74 /* f */}));
  EXPECT_EQ(or_gids, (GlyphSet{}));
  EXPECT_EQ(and_gids, (GlyphSet{444 /* fi */, 446 /* ffi */}));
}

TEST_F(GlyphClosureCacheTest, ExpandClosure) {
  GlyphClosureCache cache(roboto.get());
  SubsetDefinition def;
  def.gids = {74 /* f */, 77 /* i */};
  def.feature_tags = {HB_TAG('l', 'i', 'g', 'a')};

  SubsetDefinition expected{'f', 'i', 0xFB01 /* fi */, 0xFB03 /* ffi */};
  expected.gids = {0, 74, 77, 444, 446};
  expected.feature_tags = {HB_TAG('l', 'i', 'g', 'a')};

  auto expanded = cache.ExpandClosure(def);
  ASSERT_TRUE(expanded.ok());
  ASSERT_EQ(*expanded, expected);
}

TEST_F(GlyphClosureCacheTest, CodepointsToOrGids) {
  GlyphClosureCache cache(roboto.get());
  std::vector<Segment> segments{
      {{'A'}, ProbabilityBound::Zero()},
      {{0xC1 /* Aacute */}, ProbabilityBound::Zero()},
  };

  SubsetDefinition init_font;
  auto info = RequestedSegmentationInformation::Create(segments, init_font,
                                                       cache, PATCH);
  ASSERT_TRUE(info.ok());

  SegmentSet segs;
  segs.insert(0);

  auto or_gids = cache.CodepointsToOrGids(*info->get(), {0});
  ASSERT_TRUE(or_gids.ok());
  EXPECT_EQ(*or_gids, (GlyphSet{37}));
}

}  // namespace ift::encoder