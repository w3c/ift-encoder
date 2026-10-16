#include "ift/encoder/woff2_patch_size_cache.h"

#include <cstdint>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "hb-subset.h"
#include "hb.h"
#include "ift/common/font_data.h"
#include "ift/common/font_helper.h"
#include "ift/common/hb_set_unique_ptr.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/common/woff2.h"
#include "ift/dep_graph/unicode_edges.h"
#include "ift/encoder/init_subset_defaults.h"
#include "ift/encoder/subset_definition.h"

using absl::StatusOr;
using ift::common::FontData;
using ift::common::FontHelper;
using ift::common::GlyphSet;
using ift::common::hb_face_unique_ptr;
using ift::common::hb_set_unique_ptr;
using ift::common::make_hb_face;
using ift::common::make_hb_set;
using ift::common::Woff2;
using ift::dep_graph::UnicodeEdges;

namespace ift::encoder {

Woff2PatchSizeCache::Woff2PatchSizeCache(hb_face_t* original_face,
                                         SubsetDefinition base_subset,
                                         uint32_t brotli_quality)
    : preprocessed_face_(make_hb_face(hb_subset_preprocess(original_face))),
      base_subset_(std::move(base_subset)),
      brotli_quality_(brotli_quality),
      unicode_edges_(
          UnicodeEdges::ComputeCmapAndUVSEdges(preprocessed_face_.get())),
      cache_() {
  AddInitSubsetDefaults(base_subset_);
}

StatusOr<GlyphSet> Woff2PatchSizeCache::BaseGlyphs() const {
  hb_subset_input_t* input = hb_subset_input_create_or_fail();
  if (!input) {
    return absl::InternalError("Failed to create subset input.");
  }
  base_subset_.ConfigureInput(input, preprocessed_face_.get());
  hb_subset_plan_t* plan =
      hb_subset_plan_create_or_fail(preprocessed_face_.get(), input);
  hb_subset_input_destroy(input);
  if (!plan) {
    return absl::InternalError("Failed to create base subset plan.");
  }
  hb_map_t* new_to_old = hb_subset_plan_new_to_old_glyph_mapping(plan);
  hb_set_unique_ptr gids = make_hb_set();
  hb_map_values(new_to_old, gids.get());
  hb_subset_plan_destroy(plan);
  return GlyphSet(gids);
}

StatusOr<uint32_t> Woff2PatchSizeCache::GetPatchSize(const GlyphSet& gids) {
  auto it = cache_.find(gids);
  if (it != cache_.end()) {
    return it->second;
  }

  brotli_call_count_++;

  SubsetDefinition subset_def = base_subset_;
  subset_def.gids.union_set(gids);
  subset_def.codepoints.union_set(unicode_edges_.CodepointsForGlyphs(gids));

  hb_subset_input_t* input = hb_subset_input_create_or_fail();
  if (!input) {
    return absl::InternalError("Failed to create subset input.");
  }
  subset_def.ConfigureInput(input, preprocessed_face_.get());

  hb_face_unique_ptr subset_face =
      make_hb_face(hb_subset_or_fail(preprocessed_face_.get(), input));
  hb_subset_input_destroy(input);
  if (!subset_face.get()) {
    return absl::InternalError("Failed to create font subset.");
  }

  FontData subset_data(subset_face.get());
  FontData woff2 = TRY(Woff2::EncodeWoff2(
      subset_data.str(), /*glyf_transform=*/true, brotli_quality_));
  uint32_t size = woff2.size();
  cache_[gids] = size;
  return size;
}

StatusOr<uint32_t> EstimatedWoff2PatchSizeCache::GetPatchSize(
    const GlyphSet& gids) {
  auto it = cache_.find(gids);
  if (it != cache_.end()) {
    return it->second;
  }

  uint32_t uncompressed_outline_size =
      TRY(FontHelper::TotalGlyphData(face_.get(), gids));
  uint32_t size =
      base_woff2_overhead_ +
      (uint32_t)((double)uncompressed_outline_size * compression_ratio_);
  cache_[gids] = size;
  return size;
}

StatusOr<std::pair<uint32_t, double>>
EstimatedWoff2PatchSizeCache::EstimateParameters(hb_face_t* original_face,
                                                 SubsetDefinition base_subset) {
  Woff2PatchSizeCache woff2_sizer(original_face, std::move(base_subset), 11);
  uint32_t base_woff2_overhead = TRY(woff2_sizer.GetPatchSize(GlyphSet{}));

  uint32_t glyph_count = hb_face_get_glyph_count(original_face);
  if (glyph_count == 0) {
    return std::make_pair(base_woff2_overhead, 0.0);
  }

  GlyphSet all_gids;
  all_gids.insert_range(0, glyph_count - 1);
  GlyphSet base_gids = TRY(woff2_sizer.BaseGlyphs());

  GlyphSet non_base_gids = all_gids;
  non_base_gids.subtract(base_gids);
  if (non_base_gids.empty()) {
    return std::make_pair(base_woff2_overhead, 0.0);
  }

  uint32_t uncompressed_size =
      TRY(FontHelper::TotalGlyphData(original_face, non_base_gids));
  uint32_t full_woff2_size = TRY(woff2_sizer.GetPatchSize(non_base_gids));
  if (uncompressed_size == 0 || full_woff2_size <= base_woff2_overhead) {
    return std::make_pair(base_woff2_overhead, 0.0);
  }

  double compression_ratio = (double)(full_woff2_size - base_woff2_overhead) /
                             (double)uncompressed_size;
  return std::make_pair(base_woff2_overhead, compression_ratio);
}

}  // namespace ift::encoder
