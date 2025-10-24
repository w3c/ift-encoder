#ifndef IFT_ENCODER_ESTIMATED_PATCH_SIZE_CACHE_H_
#define IFT_ENCODER_ESTIMATED_PATCH_SIZE_CACHE_H_

#include <memory>

#include "absl/status/statusor.h"
#include "common/font_data.h"
#include "common/int_set.h"
#include "ift/encoder/patch_size_cache.h"

namespace ift::encoder {

// Estimates the size of a glyph keyed patch using a fixed compression ratio.
// Does not actually run the brotli compression.
//
// The fixed compression ratio is determined by looking at the compression ratio
// of glyph data in the provided original_face.
class EstimatedPatchSizeCache : public PatchSizeCache {
 public:
  static absl::StatusOr<std::unique_ptr<PatchSizeCache>> New(hb_face_t* face) {
    double compression_ratio = TRY(EstimateCompressionRatio(face));
    return std::unique_ptr<PatchSizeCache>(
        new EstimatedPatchSizeCache(face, compression_ratio));
  }

  absl::StatusOr<uint32_t> GetPatchSize(const common::GlyphSet& gids) override;

  double CompressionRatio() const { return compression_ratio_; }

 private:
  explicit EstimatedPatchSizeCache(hb_face_t* original_face,
                                   double compression_ratio)
      : face_(common::make_hb_face(hb_face_reference(original_face))),
        compression_ratio_(compression_ratio),
        cache_() {}

  static absl::StatusOr<double> EstimateCompressionRatio(
      hb_face_t* original_face);

  common::hb_face_unique_ptr face_;
  double compression_ratio_;
  absl::flat_hash_map<common::GlyphSet, uint32_t> cache_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_ESTIMATED_PATCH_SIZE_CACHE_H_
