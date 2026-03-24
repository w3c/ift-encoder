#include "ift/encoder/reachability_index.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "hb.h"

namespace ift::encoder {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

TEST(ReachabilityIndexTest, GlyphReachability) {
  ReachabilityIndex index;

  EXPECT_THAT(index.SegmentsForGlyph(1), IsEmpty());
  EXPECT_THAT(index.GlyphsForSegment(10), IsEmpty());

  index.AddGlyph(10, 1);
  index.AddGlyph(10, 2);
  index.AddGlyph(20, 1);

  EXPECT_THAT(index.SegmentsForGlyph(1), ElementsAre(10, 20));
  EXPECT_THAT(index.SegmentsForGlyph(2), ElementsAre(10));
  EXPECT_THAT(index.SegmentsForGlyph(3), IsEmpty());

  EXPECT_THAT(index.GlyphsForSegment(10), ElementsAre(1, 2));
  EXPECT_THAT(index.GlyphsForSegment(20), ElementsAre(1));
  EXPECT_THAT(index.GlyphsForSegment(30), IsEmpty());
}

TEST(ReachabilityIndexTest, FeatureReachability) {
  ReachabilityIndex index;

  hb_tag_t f1 = HB_TAG('l', 'i', 'g', 'a');
  hb_tag_t f2 = HB_TAG('c', 'c', 'm', 'p');

  EXPECT_THAT(index.SegmentsForFeature(f1), IsEmpty());
  EXPECT_THAT(index.FeaturesForSegment(10), IsEmpty());

  index.AddFeature(10, f1);
  index.AddFeature(10, f2);
  index.AddFeature(20, f1);

  EXPECT_THAT(index.SegmentsForFeature(f1), ElementsAre(10, 20));
  EXPECT_THAT(index.SegmentsForFeature(f2), ElementsAre(10));

  EXPECT_THAT(index.FeaturesForSegment(10), ElementsAre(f2, f1));
  EXPECT_THAT(index.FeaturesForSegment(20), ElementsAre(f1));
}

TEST(ReachabilityIndexTest, ClearSegment) {
  ReachabilityIndex index;
  hb_tag_t f1 = HB_TAG('l', 'i', 'g', 'a');

  index.AddGlyph(10, 1);
  index.AddGlyph(20, 1);
  index.AddFeature(10, f1);

  index.ClearSegment(10);

  EXPECT_THAT(index.GlyphsForSegment(10), IsEmpty());
  EXPECT_THAT(index.FeaturesForSegment(10), IsEmpty());

  EXPECT_THAT(index.SegmentsForGlyph(1), ElementsAre(20));
  EXPECT_THAT(index.SegmentsForFeature(f1), IsEmpty());
}

}  // namespace ift::encoder
