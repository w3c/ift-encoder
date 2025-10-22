#include "ift/encoder/estimated_patch_size_cache.h"

#include "common/font_helper.h"
#include "common/int_set.h"
#include "gtest/gtest.h"

#include "common/font_data.h"

using common::hb_face_unique_ptr;
using common::make_hb_face;
using common::hb_blob_unique_ptr;
using common::make_hb_blob;
using common::FontHelper;
using common::GlyphSet;

namespace ift::encoder {

class EstimatedPatchSizeCacheTest : public ::testing::Test {
 protected:
  EstimatedPatchSizeCacheTest() : roboto(make_hb_face(nullptr)) {
    hb_blob_unique_ptr blob = make_hb_blob(
        hb_blob_create_from_file("common/testdata/Roboto-Regular.ttf"));
    roboto = make_hb_face(hb_face_create(blob.get(), 0));
  }


  double CompressionRatio(GlyphSet gids, double expected_compression_ratio) {
    uint32_t raw_outline_size =
      *FontHelper::TotalGlyphData(roboto.get(), gids);
    double fixed_size = 1 + 7 * 4; // header
    fixed_size += (double) (5 + gids.size() * 2 + 4 + (gids.size() + 1)*4) * expected_compression_ratio; // glyph patches header
    auto estimated = *EstimatedPatchSizeCache::New(roboto.get());
    uint32_t compressed_size = *estimated->GetPatchSize(gids);
    return (double) (compressed_size - fixed_size) / (double) raw_outline_size;
  }

  hb_face_unique_ptr roboto;
};

TEST_F(EstimatedPatchSizeCacheTest, PatchSize) {
  // There should be a consistent compression ratio between patches.
  ASSERT_NEAR(this->CompressionRatio(GlyphSet {44, 47, 49}, 0.457), 0.46, 0.01);
  ASSERT_NEAR(CompressionRatio(GlyphSet {45, 48, 50, 51, 52, 53}, 0.457), 0.46, 0.01);
}

}  // namespace ift::encoder