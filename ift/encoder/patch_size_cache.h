#ifndef IFT_ENCODER_PATCH_SIZE_CACHE_H_
#define IFT_ENCODER_PATCH_SIZE_CACHE_H_

#include <cstdint>
#include <memory>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
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
  virtual void LogBrotliCallCount() const = 0;
};

// Computes estimated sizes of patches (based on the contained glyphs),
// caches the result.
class PatchSizeCacheImpl : public PatchSizeCache {
 public:
  explicit PatchSizeCacheImpl(hb_face_t* original_face, uint32_t brotli_quality)
      : font_data_(original_face),
        id_(),
        differ_(font_data_, id_,
                {common::FontHelper::kGlyf, common::FontHelper::kGvar,
                 common::FontHelper::kCFF, common::FontHelper::kCFF2},
                brotli_quality),
        cache_() {}

  absl::StatusOr<uint32_t> GetPatchSize(const common::GlyphSet& gids) override {
    auto it = cache_.find(gids);
    if (it != cache_.end()) {
      return it->second;
    }

    brotli_call_count_++;
    auto patch_data = TRY(differ_.CreatePatch(gids));
    uint32_t size = patch_data.size();
    cache_[gids] = size;
    return size;
  }

  void LogBrotliCallCount() const override {
    VLOG(0) << "Total number of calls to brotli = " << brotli_call_count_;
  }

 private:
  common::FontData font_data_;
  common::CompatId id_;
  GlyphKeyedDiff differ_;
  absl::flat_hash_map<common::GlyphSet, uint32_t> cache_;
  uint64_t brotli_call_count_ = 0;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_PATCH_SIZE_CACHE_H_
