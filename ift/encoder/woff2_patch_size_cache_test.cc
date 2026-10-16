#include "ift/encoder/woff2_patch_size_cache.h"

#include <cstdint>
#include <memory>

#include "gtest/gtest.h"
#include "hb-subset.h"
#include "hb.h"
#include "ift/common/font_data.h"
#include "ift/common/font_helper.h"
#include "ift/common/int_set.h"
#include "ift/common/test_font_loader.h"
#include "ift/common/woff2.h"
#include "ift/encoder/init_subset_defaults.h"
#include "ift/encoder/subset_definition.h"

using ift::common::CodepointSet;
using ift::common::FontData;
using ift::common::FontHelper;
using ift::common::GlyphSet;
using ift::common::hb_face_unique_ptr;
using ift::common::make_hb_face;
using ift::common::Woff2;

namespace ift::encoder {

class Woff2PatchSizeCacheTest : public ::testing::Test {
 protected:
  Woff2PatchSizeCacheTest() : roboto(make_hb_face(nullptr)) {
    auto loader = ift::common::TestFontLoader::Default().value();
    auto blob = loader->LoadFontData("ift/common/testdata/Roboto-Regular.ttf")
                    .value()
                    .blob();
    roboto = make_hb_face(hb_face_create(blob.get(), 0));
  }

  uint32_t ManualWoff2SubsetSize(SubsetDefinition base_subset,
                                 const GlyphSet& gids,
                                 uint32_t brotli_quality) {
    AddInitSubsetDefaults(base_subset);
    base_subset.gids.union_set(gids);
    auto gid_to_unicode = FontHelper::GidToUnicodeMap(roboto.get());
    for (uint32_t gid : gids) {
      auto it = gid_to_unicode.find(gid);
      if (it != gid_to_unicode.end()) {
        base_subset.codepoints.union_set(it->second);
      }
    }

    hb_subset_input_t* input = hb_subset_input_create_or_fail();
    base_subset.ConfigureInput(input, roboto.get());
    hb_face_unique_ptr subset_face =
        make_hb_face(hb_subset_or_fail(roboto.get(), input));
    hb_subset_input_destroy(input);

    FontData subset_data(subset_face.get());
    FontData woff2 =
        Woff2::EncodeWoff2(subset_data.str(), /*glyf_transform=*/true,
                           brotli_quality)
            .value();
    return woff2.size();
  }

  hb_face_unique_ptr roboto;
};

TEST_F(Woff2PatchSizeCacheTest, Woff2PatchSizeMatchesManualSubset) {
  SubsetDefinition base_subset;
  Woff2PatchSizeCache cache(roboto.get(), base_subset, /*brotli_quality=*/1);

  GlyphSet patch_a{44, 47, 49};
  uint32_t size_a = *cache.GetPatchSize(patch_a);
  EXPECT_EQ(size_a, ManualWoff2SubsetSize(base_subset, patch_a, 1));
  EXPECT_EQ(cache.BrotliCallCount(), 1u);

  // Repeat call should hit the cache without invoking Brotli again.
  EXPECT_EQ(*cache.GetPatchSize(patch_a), size_a);
  EXPECT_EQ(cache.BrotliCallCount(), 1u);

  GlyphSet patch_b{45, 48, 50, 51, 52, 53};
  uint32_t size_b = *cache.GetPatchSize(patch_b);
  EXPECT_EQ(size_b, ManualWoff2SubsetSize(base_subset, patch_b, 1));
  EXPECT_EQ(cache.BrotliCallCount(), 2u);
  EXPECT_GT(size_b, size_a);
}

TEST_F(Woff2PatchSizeCacheTest, IncludesBaseSubsetInAllPatches) {
  SubsetDefinition empty_base;
  Woff2PatchSizeCache empty_base_cache(roboto.get(), empty_base,
                                       /*brotli_quality=*/1);

  SubsetDefinition non_empty_base;
  non_empty_base.codepoints = CodepointSet{'A', 'B', 'C', 'D', 'E'};
  Woff2PatchSizeCache non_empty_base_cache(roboto.get(), non_empty_base,
                                           /*brotli_quality=*/1);

  uint32_t empty_base_size = *empty_base_cache.GetPatchSize(GlyphSet{});
  uint32_t non_empty_base_size = *non_empty_base_cache.GetPatchSize(GlyphSet{});
  EXPECT_GT(non_empty_base_size, empty_base_size);
  EXPECT_EQ(non_empty_base_size,
            ManualWoff2SubsetSize(non_empty_base, GlyphSet{}, 1));

  GlyphSet base_gids = *non_empty_base_cache.BaseGlyphs();
  EXPECT_TRUE(base_gids.contains(0));
  EXPECT_GT(base_gids.size(), 1u);
}

TEST_F(Woff2PatchSizeCacheTest, EstimatedWoff2PatchSizeCache) {
  SubsetDefinition base_subset;
  auto [base_overhead, ratio] =
      *EstimatedWoff2PatchSizeCache::EstimateParameters(roboto.get(),
                                                        base_subset);
  EXPECT_EQ(base_overhead, ManualWoff2SubsetSize(base_subset, GlyphSet{}, 11));
  EXPECT_GT(ratio, 0.0);
  EXPECT_LT(ratio, 1.0);

  auto estimated =
      *EstimatedWoff2PatchSizeCache::New(roboto.get(), base_subset);
  EXPECT_EQ(*estimated->GetPatchSize(GlyphSet{}), base_overhead);

  GlyphSet patch_a{44, 47, 49};
  uint32_t raw_a = *FontHelper::TotalGlyphData(roboto.get(), patch_a);
  uint32_t est_a = *estimated->GetPatchSize(patch_a);
  EXPECT_GT(est_a, base_overhead);
  double observed_ratio_a =
      (double)(est_a - base_overhead) / (double)raw_a;
  EXPECT_NEAR(observed_ratio_a, ratio, 0.02);

  GlyphSet patch_b{45, 48, 50, 51, 52, 53};
  uint32_t raw_b = *FontHelper::TotalGlyphData(roboto.get(), patch_b);
  uint32_t est_b = *estimated->GetPatchSize(patch_b);
  double observed_ratio_b =
      (double)(est_b - base_overhead) / (double)raw_b;
  EXPECT_NEAR(observed_ratio_b, ratio, 0.02);
}

}  // namespace ift::encoder
