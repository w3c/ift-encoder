#ifndef IFT_ENCODER_WOFF2_PATCH_SIZE_CACHE_H_
#define IFT_ENCODER_WOFF2_PATCH_SIZE_CACHE_H_

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "ift/common/font_data.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/dep_graph/unicode_edges.h"
#include "ift/encoder/patch_size_cache.h"
#include "ift/encoder/subset_definition.h"

namespace ift::encoder {

// Computes the WOFF2 size of standalone font subsets formed by unioning
// base_subset (the initial segment) with the patch's glyphs and their
// associated codepoints, and caches the result.
class Woff2PatchSizeCache : public PatchSizeCache {
 public:
  Woff2PatchSizeCache(hb_face_t* original_face, SubsetDefinition base_subset,
                      uint32_t brotli_quality);

  absl::StatusOr<uint32_t> GetPatchSize(
      const ift::common::GlyphSet& gids) override;

  void LogBrotliCallCount() const override {
    VLOG(0) << "Total number of calls to brotli = " << brotli_call_count_;
  }

  uint64_t BrotliCallCount() const { return brotli_call_count_; }

  absl::StatusOr<ift::common::GlyphSet> BaseGlyphs() const;

 private:
  ift::common::hb_face_unique_ptr preprocessed_face_;
  SubsetDefinition base_subset_;
  uint32_t brotli_quality_;
  dep_graph::UnicodeEdges unicode_edges_;
  absl::flat_hash_map<ift::common::GlyphSet, uint32_t> cache_;
  uint64_t brotli_call_count_ = 0;
};

// Estimates the WOFF2 size of a standalone font subset using the WOFF2 size of
// the base subset (initial_segment) plus the raw outline size of the patch's
// glyphs scaled by the font's overall WOFF2 outline compression ratio.
class EstimatedWoff2PatchSizeCache : public PatchSizeCache {
 public:
  static absl::StatusOr<std::unique_ptr<PatchSizeCache>> New(
      hb_face_t* face, SubsetDefinition base_subset) {
    auto [base_woff2_overhead, compression_ratio] =
        TRY(EstimateParameters(face, std::move(base_subset)));
    return std::unique_ptr<PatchSizeCache>(new EstimatedWoff2PatchSizeCache(
        face, base_woff2_overhead, compression_ratio));
  }

  static std::unique_ptr<PatchSizeCache> New(hb_face_t* face,
                                             uint32_t base_woff2_overhead,
                                             double compression_ratio) {
    return std::unique_ptr<PatchSizeCache>(new EstimatedWoff2PatchSizeCache(
        face, base_woff2_overhead, compression_ratio));
  }

  absl::StatusOr<uint32_t> GetPatchSize(
      const ift::common::GlyphSet& gids) override;

  void LogBrotliCallCount() const override {}

  uint32_t BaseWoff2Overhead() const { return base_woff2_overhead_; }
  double CompressionRatio() const { return compression_ratio_; }

  static absl::StatusOr<std::pair<uint32_t, double>> EstimateParameters(
      hb_face_t* original_face, SubsetDefinition base_subset);

 private:
  EstimatedWoff2PatchSizeCache(hb_face_t* original_face,
                               uint32_t base_woff2_overhead,
                               double compression_ratio)
      : face_(ift::common::make_hb_face(hb_face_reference(original_face))),
        base_woff2_overhead_(base_woff2_overhead),
        compression_ratio_(compression_ratio),
        cache_() {}

  ift::common::hb_face_unique_ptr face_;
  uint32_t base_woff2_overhead_;
  double compression_ratio_;
  absl::flat_hash_map<ift::common::GlyphSet, uint32_t> cache_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_WOFF2_PATCH_SIZE_CACHE_H_
