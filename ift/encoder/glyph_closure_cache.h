#ifndef IFT_ENCODER_GLYPH_CLOSURE_CACHE_H_
#define IFT_ENCODER_GLYPH_CLOSURE_CACHE_H_

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "common/font_data.h"
#include "common/int_set.h"
#include "hb-subset.h"
#include "ift/encoder/subset_definition.h"

namespace ift::encoder {

class RequestedSegmentationInformation;

/*
 * A cache of the results of glyph closure on a specific font face.
 */
class GlyphClosureCache {
 public:
  GlyphClosureCache(hb_face_t* face)
      : preprocessed_face_(common::make_hb_face(hb_subset_preprocess(face))),
        original_face_(common::make_hb_face(hb_face_reference(face))) {}

  absl::StatusOr<common::GlyphSet> GlyphClosure(
      const SubsetDefinition& segment);

  absl::StatusOr<common::GlyphSet> CodepointsToOrGids(
      const RequestedSegmentationInformation& segmentation_info,
      const common::SegmentSet& segment_ids);

  absl::Status AnalyzeSegment(
      const RequestedSegmentationInformation& segmentation_info,
      const common::SegmentSet& segment_ids, common::GlyphSet& and_gids,
      common::GlyphSet& or_gids, common::GlyphSet& exclusive_gids);

  void LogCacheStats() const {
    double closure_hit_rate =
        100.0 * ((double)glyph_closure_cache_hit_) /
        ((double)(glyph_closure_cache_hit_ + glyph_closure_cache_miss_));
    VLOG(1) << "Glyph closure cache hit rate: " << closure_hit_rate << "% ("
            << glyph_closure_cache_hit_ << " hits, "
            << glyph_closure_cache_miss_ << " misses)";
  }

  void LogClosureCount(absl::string_view operation) {
    VLOG(1) << operation << ": cumulative number of glyph closures "
            << closure_count_cumulative_ << " (+" << closure_count_delta_
            << ")";
    closure_count_delta_ = 0;
  }

  hb_face_t* Face() { return preprocessed_face_.get(); }

 private:
  common::hb_face_unique_ptr preprocessed_face_;
  common::hb_face_unique_ptr original_face_;
  absl::flat_hash_map<SubsetDefinition, common::GlyphSet> glyph_closure_cache_;
  uint32_t glyph_closure_cache_hit_ = 0;
  uint32_t glyph_closure_cache_miss_ = 0;
  uint32_t closure_count_cumulative_ = 0;
  uint32_t closure_count_delta_ = 0;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_GLYPH_CLOSURE_CACHE_H_