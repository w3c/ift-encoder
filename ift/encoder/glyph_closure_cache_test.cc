#include "ift/encoder/glyph_closure_cache.h"

#include <vector>

#include "gtest/gtest.h"
#include "hb-subset.h"
#include "hb.h"
#include "ift/common/bazel_data_file_resolver.h"
#include "ift/common/font_data.h"
#include "ift/common/font_helper.h"
#include "ift/common/int_set.h"
#include "ift/common/test_font_loader.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/probability_bound.h"

using ift::config::PATCH;

using absl::StatusOr;
using ift::common::BazelDataFileResolver;
using ift::common::CodepointSet;
using ift::common::DataFileResolver;
using ift::common::FontData;
using ift::common::FontHelper;
using ift::common::GlyphSet;
using ift::common::hb_face_unique_ptr;
using ift::common::make_hb_face;
using ift::common::SegmentSet;
using ift::freq::ProbabilityBound;

namespace ift::encoder {

class GlyphClosureCacheTest : public ::testing::Test {
 protected:
  GlyphClosureCacheTest()
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

TEST_F(GlyphClosureCacheTest, GlyphClosure) {
  auto cache = GlyphClosureCache::Create(roboto.get(), *resolver);
  ASSERT_TRUE(cache.ok()) << cache.status();
  auto closure = (*cache)->GlyphClosure({'a'});
  ASSERT_TRUE(closure.ok());
  ASSERT_EQ(*closure, (GlyphSet{0, 69}));
}

TEST_F(GlyphClosureCacheTest, SegmentClosure) {
  auto cache = GlyphClosureCache::Create(roboto.get(), *resolver);
  ASSERT_TRUE(cache.ok()) << cache.status();
  std::vector<Segment> segments{
      {{'f'}},
      {{'i'}},
  };

  SubsetDefinition init;
  init.feature_tags = {HB_TAG('l', 'i', 'g', 'a')};
  auto info =
      RequestedSegmentationInformation::Create(segments, init, **cache, PATCH);
  ASSERT_TRUE(info.ok());

  auto closure = (*cache)->SegmentClosure(info->get(), {0});
  ASSERT_TRUE(closure.ok());
  ASSERT_EQ(*closure, (GlyphSet{0, 74 /* f */}));

  closure = (*cache)->SegmentClosure(info->get(), {1});
  ASSERT_TRUE(closure.ok());
  ASSERT_EQ(*closure, (GlyphSet{0, 77 /* i */}));

  closure = (*cache)->SegmentClosure(info->get(), {0, 1});
  ASSERT_TRUE(closure.ok());
  ASSERT_EQ(*closure, (GlyphSet{0, 74, 77, 444, 446}));
}

TEST_F(GlyphClosureCacheTest, HasAdditionalConditions) {
  auto cache = GlyphClosureCache::Create(roboto.get(), *resolver);
  ASSERT_TRUE(cache.ok()) << cache.status();
  std::vector<Segment> segments{
      {{'A'}},
      {{0xC1 /* Aacute*/}},
  };

  // s0 or s1 -> g37 (A)

  auto info =
      RequestedSegmentationInformation::Create(segments, {}, **cache, PATCH);
  ASSERT_TRUE(info.ok());

  ASSERT_TRUE(
      *(*cache)->HasAdditionalConditions(info->get(), {0}, {37 /* A */}));
  ASSERT_TRUE(
      *(*cache)->HasAdditionalConditions(info->get(), {1}, {37 /* A */}));
  ASSERT_FALSE(
      *(*cache)->HasAdditionalConditions(info->get(), {0, 1}, {37 /* A */}));

  ASSERT_FALSE(
      *(*cache)->HasAdditionalConditions(info->get(), {0}, {668 /* Aacute */}));
  ASSERT_FALSE(
      *(*cache)->HasAdditionalConditions(info->get(), {1}, {668 /* Aacute */}));
}

TEST_F(GlyphClosureCacheTest, HasAdditionalConditions_IncludesInitFont) {
  auto cache = GlyphClosureCache::Create(roboto.get(), *resolver);
  ASSERT_TRUE(cache.ok()) << cache.status();
  std::vector<Segment> segments{
      {{'A'}},
      {{0xC1 /* Aacute*/}},
  };

  SubsetDefinition init;
  init.feature_tags = {HB_TAG('c', '2', 's', 'c')};

  auto info =
      RequestedSegmentationInformation::Create(segments, init, **cache, PATCH);
  ASSERT_TRUE(info.ok());

  // s0 or s1 -> g563 (smcap A)
  EXPECT_EQ(*(*cache)->CodepointsToOrGids(*info->get(), {0}),
            (GlyphSet{37, 563}));
  EXPECT_EQ(*(*cache)->CodepointsToOrGids(*info->get(), {1}),
            (GlyphSet{37, 563}));

  EXPECT_TRUE(*(*cache)->HasAdditionalConditions(info->get(), {0}, {563}));
  EXPECT_TRUE(*(*cache)->HasAdditionalConditions(info->get(), {1}, {563}));
  EXPECT_FALSE(*(*cache)->HasAdditionalConditions(info->get(), {0, 1}, {563}));
}

TEST_F(GlyphClosureCacheTest, AnalyzeSegment) {
  auto cache = GlyphClosureCache::Create(roboto.get(), *resolver);
  ASSERT_TRUE(cache.ok()) << cache.status();
  std::vector<Segment> segments{
      {{'f'}},
      {{'i'}},
  };

  SubsetDefinition init;
  init.feature_tags = {HB_TAG('l', 'i', 'g', 'a')};
  auto info =
      RequestedSegmentationInformation::Create(segments, init, **cache, PATCH);
  ASSERT_TRUE(info.ok());

  GlyphSet and_gids, or_gids, exclusive_gids;
  auto status = (*cache)->AnalyzeSegment(*info->get(), {0}, and_gids, or_gids,
                                         exclusive_gids);
  ASSERT_TRUE(status.ok());

  EXPECT_EQ(exclusive_gids, (GlyphSet{74 /* f */}));
  EXPECT_EQ(or_gids, (GlyphSet{}));
  EXPECT_EQ(and_gids, (GlyphSet{444 /* fi */, 446 /* ffi */}));
}

TEST_F(GlyphClosureCacheTest, ExpandClosure) {
  auto cache = GlyphClosureCache::Create(roboto.get(), *resolver);
  ASSERT_TRUE(cache.ok()) << cache.status();

  SubsetDefinition def;
  def.gids = {74 /* f */, 77 /* i */};
  def.feature_tags = {HB_TAG('l', 'i', 'g', 'a')};

  SubsetDefinition expected{'f', 'i', 0xFB01 /* fi */, 0xFB03 /* ffi */};
  expected.gids = {0, 74, 77, 444, 446};
  expected.feature_tags = {HB_TAG('l', 'i', 'g', 'a')};

  auto expanded = (*cache)->ExpandClosure(def);
  ASSERT_TRUE(expanded.ok());
  ASSERT_EQ(*expanded, expected);
}

TEST_F(GlyphClosureCacheTest, CodepointsToOrGids) {
  auto cache = GlyphClosureCache::Create(roboto.get(), *resolver);
  ASSERT_TRUE(cache.ok()) << cache.status();
  std::vector<Segment> segments{
      {{'A'}},
      {{0xC1 /* Aacute */}},
  };

  SubsetDefinition init_font;
  auto info = RequestedSegmentationInformation::Create(segments, init_font,
                                                       **cache, PATCH);
  ASSERT_TRUE(info.ok());

  SegmentSet segs;
  segs.insert(0);

  auto or_gids = (*cache)->CodepointsToOrGids(*info->get(), {0});
  ASSERT_TRUE(or_gids.ok());
  EXPECT_EQ(*or_gids, (GlyphSet{37}));
}

TEST_F(GlyphClosureCacheTest, UnicodeCompDecomp_ExpandClosure) {
  auto cache = GlyphClosureCache::Create(roboto.get(), *resolver);
  ASSERT_TRUE(cache.ok()) << cache.status();

  SubsetDefinition def;
  def.codepoints = {0xe1};  // á

  auto expanded = (*cache)->ExpandClosure(def);
  ASSERT_TRUE(expanded.ok());

  EXPECT_TRUE(expanded->codepoints.contains(0x61));   // a
  EXPECT_TRUE(expanded->codepoints.contains(0x301));  // combining acute
}

TEST_F(GlyphClosureCacheTest, UnicodeCompDecomp_Composition) {
  auto cache = GlyphClosureCache::Create(roboto.get(), *resolver);
  ASSERT_TRUE(cache.ok()) << cache.status();

  SubsetDefinition def;
  def.codepoints = {0x61, 0x301};  // a, combining acute

  auto expanded = (*cache)->ExpandClosure(def);
  ASSERT_TRUE(expanded.ok());

  EXPECT_TRUE(expanded->codepoints.contains(0xe1));  // á
}

TEST_F(GlyphClosureCacheTest, CodepointsForGlyphs) {
  // Test with Roboto for standard mappings.
  {
    auto cache = GlyphClosureCache::Create(roboto.get(), *resolver);
    ASSERT_TRUE(cache.ok()) << cache.status();

    // 1. Empty set
    EXPECT_TRUE((*cache)->CodepointsForGlyphs({}).empty());

    // 2. Single nominal glyph
    auto a_gid = *FontHelper::GetNominalGlyph(roboto.get(), 'a');
    auto cps = (*cache)->CodepointsForGlyphs({a_gid});
    EXPECT_EQ(cps, (CodepointSet{'a'}));

    // 3. Multiple nominal glyphs
    auto b_gid = *FontHelper::GetNominalGlyph(roboto.get(), 'b');
    cps = (*cache)->CodepointsForGlyphs({a_gid, b_gid});
    EXPECT_EQ(cps, (CodepointSet{'a', 'b'}));
  }

  // Test with NotoSansJP for UVS mappings.
  {
    auto face = from_file("ift/common/testdata/NotoSansJP-Regular.ttf");
    ASSERT_TRUE(face);

    auto cache = GlyphClosureCache::Create(face.get(), *resolver);
    ASSERT_TRUE(cache.ok()) << cache.status();

    auto font = ift::common::make_hb_font(hb_font_create(face.get()));
    hb_codepoint_t gid = 0;
    ASSERT_TRUE(hb_font_get_variation_glyph(font.get(), 0x4FAE, 0xFE00, &gid));

    // 4. UVS glyph
    auto cps = (*cache)->CodepointsForGlyphs({gid});
    EXPECT_TRUE(cps.contains(0xFE00));

    // 5. Mix of nominal and UVS glyphs
    auto x_gid = *ift::common::FontHelper::GetNominalGlyph(face.get(), 'x');
    auto cps_mix = (*cache)->CodepointsForGlyphs({gid, x_gid});
    EXPECT_TRUE(cps_mix.contains(0xFE00));
    EXPECT_TRUE(cps_mix.contains('x'));
  }
}

TEST_F(GlyphClosureCacheTest, GlyphClosure_DecompBeforeComp) {
  auto cache = GlyphClosureCache::Create(roboto.get(), *resolver);
  ASSERT_TRUE(cache.ok()) << cache.status();

  auto gids = (*cache)->GlyphClosure({0xE1 /* aacute */, 'e'});
  ASSERT_TRUE(gids.ok()) << gids.status();

  auto gid_eacute = FontHelper::GetNominalGlyph(roboto.get(), 0xE9);
  ASSERT_TRUE(gid_eacute.ok()) << gid_eacute.status();
  EXPECT_TRUE(gids->contains(*gid_eacute));
}

TEST_F(GlyphClosureCacheTest, GlyphClosure_BidiBeforeDecomposition) {
  hb_face_unique_ptr noto_sans_jp =
      from_file("ift/common/testdata/NotoSansJP-Regular.ttf");

  // Keep 0x2329, 0x3008, 0x3009 while dropping 0x232A (bidi mirror of 0x2329).
  // 0x2329 canonically decomposes to 0x3008, and 0x3008 <-> 0x3009 are bidi
  // mirrors.
  hb_subset_input_t* input = hb_subset_input_create_or_fail();
  ASSERT_NE(input, nullptr);
  hb_subset_input_set_flags(input, HB_SUBSET_FLAGS_NO_BIDI_CLOSURE);
  hb_set_t* unicodes = hb_subset_input_unicode_set(input);
  hb_set_add(unicodes, 0x2329);
  hb_set_add(unicodes, 0x3008);
  hb_set_add(unicodes, 0x3009);
  hb_face_unique_ptr subset =
      ift::common::make_hb_face(hb_subset_or_fail(noto_sans_jp.get(), input));
  hb_subset_input_destroy(input);
  ASSERT_NE(subset.get(), nullptr);
  ift::common::hb_blob_unique_ptr blob =
      ift::common::make_hb_blob(hb_face_reference_blob(subset.get()));
  hb_face_unique_ptr subset_face =
      ift::common::make_hb_face(hb_face_create(blob.get(), 0));

  CodepointSet subset_cps = FontHelper::ToCodepointsSet(subset_face.get());
  ASSERT_TRUE(subset_cps.contains(0x2329));
  ASSERT_TRUE(subset_cps.contains(0x3008));
  ASSERT_TRUE(subset_cps.contains(0x3009));
  ASSERT_FALSE(subset_cps.contains(0x232A));

  auto cache = GlyphClosureCache::Create(subset_face.get(), *resolver);
  ASSERT_TRUE(cache.ok()) << cache.status();

  uint32_t gid_2329 =
      *FontHelper::GetNominalGlyph(subset_face.get(), 0x2329);
  uint32_t gid_3008 =
      *FontHelper::GetNominalGlyph(subset_face.get(), 0x3008);
  uint32_t gid_3009 =
      *FontHelper::GetNominalGlyph(subset_face.get(), 0x3009);

  // Starting from 0x2329: Bidi runs before Decomposition, so 0x3008 (reached
  // only via Decomposition of 0x2329) must NOT trigger Bidi mirroring to
  // 0x3009.
  auto gids = (*cache)->GlyphClosure({0x2329});
  ASSERT_TRUE(gids.ok()) << gids.status();
  EXPECT_TRUE(gids->contains(gid_2329));
  EXPECT_TRUE(gids->contains(gid_3008));
  EXPECT_FALSE(gids->contains(gid_3009));

  // Starting from 0x3009: Bidi mirrors 0x3009 -> 0x3008.
  gids = (*cache)->GlyphClosure({0x3009});
  ASSERT_TRUE(gids.ok()) << gids.status();
  EXPECT_TRUE(gids->contains(gid_3009));
  EXPECT_TRUE(gids->contains(gid_3008));
}

TEST_F(GlyphClosureCacheTest, GlyphClosure_UvsBeforeDecomposition) {
  hb_face_unique_ptr noto_sans_jp =
      from_file("ift/common/testdata/NotoSansJP-Regular.ttf");

  auto cache = GlyphClosureCache::Create(noto_sans_jp.get(), *resolver);
  ASSERT_TRUE(cache.ok()) << cache.status();

  // 0xF901 canonically decomposes to 0x66F4, and (0x66F4, 0xE0101) maps via UVS
  // to glyph 16687 (while nominal glyph for both 0xF901 and 0x66F4 is 6563).
  auto gids = (*cache)->GlyphClosure({0xF901, 0xE0101});
  ASSERT_TRUE(gids.ok()) << gids.status();
  EXPECT_TRUE(gids->contains(6563));
  EXPECT_FALSE(gids->contains(16687));

  // Starting from 0x66F4 + 0xE0101 directly DOES reach UVS glyph 16687.
  gids = (*cache)->GlyphClosure({0x66F4, 0xE0101});
  ASSERT_TRUE(gids.ok()) << gids.status();
  EXPECT_TRUE(gids->contains(16687));
}

}  // namespace ift::encoder