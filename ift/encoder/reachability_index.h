#ifndef IFT_ENCODER_REACHABILITY_INDEX_H_
#define IFT_ENCODER_REACHABILITY_INDEX_H_

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "hb.h"
#include "ift/common/int_set.h"
#include "ift/encoder/types.h"

namespace ift::encoder {

// Maintains a two-way index of reachability between segments and both glyphs
// and layout features.
class ReachabilityIndex {
 public:
  ReachabilityIndex() = default;

  // Record that glyph is reachable from segment.
  void AddGlyph(segment_index_t segment, glyph_id_t glyph);

  // Record that feature is reachable from segment.
  void AddFeature(segment_index_t segment, hb_tag_t feature);

  // Removes all reachability information for the given segment.
  void ClearSegment(segment_index_t segment);

  // Returns the set of segments that can reach glyph.
  //
  // If no information is present for glyph then an empty set is returned.
  const ift::common::SegmentSet& SegmentsForGlyph(glyph_id_t glyph) const;

  // Returns the set of glyphs that can be reached by segment.
  //
  // If no information is present for segment then an empty set is returned.
  const ift::common::GlyphSet& GlyphsForSegment(segment_index_t segment) const;

  // Returns the set of segments that can reach feature.
  //
  // If no information is present for feature then an empty set is returned.
  const ift::common::SegmentSet& SegmentsForFeature(hb_tag_t feature) const;

  // Returns the set of features that can be reached by segment.
  //
  // If no information is present for segment then an empty set is returned.
  const absl::btree_set<hb_tag_t>& FeaturesForSegment(
      segment_index_t segment) const;

  // Since the accessors above always return something, these two methods
  // can be used to track which segments are present in the index.
  void MarkPresent(segment_index_t segment) { presence_.insert(segment); }
  bool IsPresent(segment_index_t segment) const {
    return presence_.contains(segment);
  }

 private:
  ift::common::SegmentSet presence_;

  absl::flat_hash_map<glyph_id_t, ift::common::SegmentSet> segments_by_glyph_;
  absl::flat_hash_map<segment_index_t, ift::common::GlyphSet>
      glyphs_by_segment_;

  absl::flat_hash_map<hb_tag_t, ift::common::SegmentSet> segments_by_feature_;
  absl::flat_hash_map<segment_index_t, absl::btree_set<hb_tag_t>>
      features_by_segment_;

  ift::common::SegmentSet empty_segments_{};
  ift::common::GlyphSet empty_glyphs_{};
  absl::btree_set<hb_tag_t> empty_features_{};
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_REACHABILITY_INDEX_H_