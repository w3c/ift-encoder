#include "ift/encoder/estimated_patch_size_cache.h"

#include "ift/common/font_helper.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"

using absl::flat_hash_set;
using absl::StatusOr;
using ift::common::FontHelper;
using ift::common::GlyphSet;

namespace ift::encoder {

StatusOr<uint32_t> EstimatedPatchSizeCache::GetPatchSize(const GlyphSet& gids) {
  auto it = cache_.find(gids);
  if (it != cache_.end()) {
    return it->second;
  }

  flat_hash_set<hb_tag_t> tags = FontHelper::GetTags(face_.get());
  uint32_t table_count = (tags.contains(FontHelper::kCFF) ? 1 : 0) +
                         (tags.contains(FontHelper::kCFF2) ? 1 : 0) +
                         (tags.contains(FontHelper::kGlyf) ? 1 : 0) +
                         (tags.contains(FontHelper::kGvar) ? 1 : 0);

  uint32_t gid_width = (gids.size() > 255) ? 3 : 2;

  uint32_t header_size = 1 + 7 * 4;
  uint32_t uncompressed_stream_size =
      5 + gids.size() * gid_width +         // glyph ids
      4 * table_count +                     // table tags
      4 * (gids.size() * table_count + 1);  // data offsets

  uncompressed_stream_size +=
      TRY(FontHelper::TotalGlyphData(face_.get(), gids));

  uint32_t size = header_size + (uint32_t)((double)uncompressed_stream_size *
                                           compression_ratio_);
  cache_[gids] = size;
  return size;
}

StatusOr<double> EstimatedPatchSizeCache::EstimateCompressionRatio(
    hb_face_t* original_face) {
  PatchSizeCacheImpl patch_sizes(original_face, 11);

  uint32_t glyph_count = hb_face_get_glyph_count(original_face);
  if (glyph_count == 0) {
    return 0.0;
  }

  GlyphSet gids;
  gids.insert_range(0, glyph_count - 1);

  double uncompressed_size =
      TRY(FontHelper::TotalGlyphData(original_face, gids));
  double compressed_size = TRY(patch_sizes.GetPatchSize(gids));

  return compressed_size / uncompressed_size;
}

}  // namespace ift::encoder