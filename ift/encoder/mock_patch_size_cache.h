#ifndef IFT_ENCODER_MOCK_PATCH_SIZE_CACHE_H_
#define IFT_ENCODER_MOCK_PATCH_SIZE_CACHE_H_

#include "ift/encoder/patch_size_cache.h"

namespace ift::encoder {

// A mock implementation of `PatchSizeCache` for use in tests.
class MockPatchSizeCache : public PatchSizeCache {
 public:
  absl::StatusOr<uint32_t> GetPatchSize(const common::GlyphSet& gids) override {
    auto it = patch_sizes_.find(gids);
    if (it != patch_sizes_.end()) {
      return it->second;
    }
    // Return a default value if no specific size is set for the given glyphs.
    return 100;
  }

  void SetPatchSize(const common::GlyphSet& gids, uint32_t size) {
    patch_sizes_[gids] = size;
  }

 private:
  absl::flat_hash_map<common::GlyphSet, uint32_t> patch_sizes_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_MOCK_PATCH_SIZE_CACHE_H_