#ifndef IFT_ENCODER_GLYPH_CLOSURE_CACHE_H_
#define IFT_ENCODER_GLYPH_CLOSURE_CACHE_H_

#include "absl/status/status.h"
#include "hb-subset.h"
#include "ift/common/font_data.h"
#include "ift/common/int_set.h"
#include "ift/encoder/subset_definition.h"

namespace ift::encoder {

class RequestedSegmentationInformation;

/*
 * A cache of the results of glyph closure on a specific font face.
 */
class GlyphClosureCache {
 public:
  GlyphClosureCache(hb_face_t* face)
      : preprocessed_face_(
            ift::common::make_hb_face(hb_subset_preprocess(face))),
        original_face_(ift::common::make_hb_face(hb_face_reference(face))) {}

  absl::StatusOr<ift::common::GlyphSet> GlyphClosure(
      const SubsetDefinition& segment);

  absl::StatusOr<ift::common::GlyphSet> SegmentClosure(
      const RequestedSegmentationInformation* segmentation_info,
      const ift::common::SegmentSet& segments);

  // Checks if a disjunction accross segments satisifies the closure require for
  // glyphs, returns true if there are potential additional conditions beyond
  // segments that may activate glyphs.
  absl::StatusOr<bool> HasAdditionalConditions(
      const RequestedSegmentationInformation* segmentation_info,
      const ift::common::SegmentSet& segments,
      const ift::common::GlyphSet& glyphs);

  // Analyzes the provided segments with AnalyzeSegment() and returns just the
  // or_gids
  absl::StatusOr<ift::common::GlyphSet> CodepointsToOrGids(
      const RequestedSegmentationInformation& segmentation_info,
      const ift::common::SegmentSet& segment_ids);

  absl::Status AnalyzeSegment(
      const RequestedSegmentationInformation& segmentation_info,
      const ift::common::SegmentSet& segment_ids,
      ift::common::GlyphSet& and_gids, ift::common::GlyphSet& or_gids,
      ift::common::GlyphSet& exclusive_gids);

  absl::StatusOr<SubsetDefinition> ExpandClosure(
      const SubsetDefinition& definition);

  uint64_t CacheHits() const { return glyph_closure_cache_hit_; }
  uint64_t CacheMisses() const { return glyph_closure_cache_miss_; }

  hb_face_t* Face() { return preprocessed_face_.get(); }

 private:
  ift::common::hb_face_unique_ptr preprocessed_face_;
  ift::common::hb_face_unique_ptr original_face_;
  absl::flat_hash_map<SubsetDefinition, ift::common::GlyphSet>
      glyph_closure_cache_;
  uint64_t glyph_closure_cache_hit_ = 0;
  uint64_t glyph_closure_cache_miss_ = 0;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_GLYPH_CLOSURE_CACHE_H_