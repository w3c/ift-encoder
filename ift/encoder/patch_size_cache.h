#ifndef IFT_ENCODER_PATCH_SIZE_CACHE_H_
#define IFT_ENCODER_PATCH_SIZE_CACHE_H_

#include <cstdint>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "common/font_data.h"
#include "common/int_set.h"
#include "common/try.h"
#include "ift/glyph_keyed_diff.h"

namespace ift::encoder {

// Interface for a cache that stores the estimated size of patches.
class PatchSizeCache {
 public:
  virtual ~PatchSizeCache() = default;
  virtual absl::StatusOr<uint32_t> GetPatchSize(
      const common::GlyphSet& gids) = 0;
};

// Computes estimated sizes of patches (based on the contained glyphs),
// caches the result.
class PatchSizeCacheImpl : public PatchSizeCache {
 public:
  explicit PatchSizeCacheImpl(hb_face_t* original_face)
      : original_face_(common::make_hb_face(hb_face_reference(original_face))) {
  }

  absl::StatusOr<uint32_t> GetPatchSize(const common::GlyphSet& gids) override {
    auto it = cache_.find(gids);
    if (it != cache_.end()) {
      return it->second;
    }

    common::FontData font_data(original_face_.get());
    common::CompatId id;
    GlyphKeyedDiff diff(
        font_data, id,
        {common::FontHelper::kGlyf, common::FontHelper::kGvar,
         common::FontHelper::kCFF, common::FontHelper::kCFF2},
        8);  // TODO XXXX use brotli quality from merge strategy.

    auto patch_data = TRY(diff.CreatePatch(gids));
    uint32_t size = patch_data.size();
    cache_[gids] = size;
    return size;
  }

 private:
  common::hb_face_unique_ptr original_face_;
  absl::flat_hash_map<common::GlyphSet, uint32_t> cache_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_PATCH_SIZE_CACHE_H_
