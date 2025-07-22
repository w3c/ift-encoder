#ifndef IFT_ENCODER_GLYPH_CONDITION_SET_H_
#define IFT_ENCODER_GLYPH_CONDITION_SET_H_

#include "absl/container/flat_hash_map.h"
#include "common/int_set.h"
#include "ift/encoder/types.h"

namespace ift::encoder {

/*
 * A set of conditions which activate a specific single glyph.
 */
class GlyphConditions {
 public:
  GlyphConditions() : and_segments(), or_segments() {}
  common::SegmentSet and_segments;
  common::SegmentSet or_segments;

  bool operator==(const GlyphConditions& other) const {
    return other.and_segments == and_segments &&
           other.or_segments == or_segments;
  }

  void RemoveSegments(const common::SegmentSet& segments) {
    and_segments.subtract(segments);
    or_segments.subtract(segments);
  }
};

/*
 * Collection of per glyph conditions for all glyphs in a font.
 */
class GlyphConditionSet {
 public:
  GlyphConditionSet(uint32_t num_glyphs) { gid_conditions_.resize(num_glyphs); }

  const GlyphConditions& ConditionsFor(glyph_id_t gid) const {
    return gid_conditions_[gid];
  }

  void AddAndCondition(glyph_id_t gid, segment_index_t segment) {
    gid_conditions_[gid].and_segments.insert(segment);
    segment_to_gid_conditions_[segment].insert(gid);
  }

  void AddOrCondition(glyph_id_t gid, segment_index_t segment) {
    gid_conditions_[gid].or_segments.insert(segment);
    segment_to_gid_conditions_[segment].insert(gid);
  }

  // Returns the set of glyphs that have 'segment' in their conditions.
  const common::GlyphSet& GlyphsWithSegment(segment_index_t segment) const {
    const static common::GlyphSet empty;
    auto it = segment_to_gid_conditions_.find(segment);
    if (it == segment_to_gid_conditions_.end()) {
      return empty;
    }
    return it->second;
  }

  // Clears out any stored information for glyphs and segments in this condition
  // set.
  void InvalidateGlyphInformation(const common::GlyphSet& glyphs,
                                  const common::SegmentSet& segments) {
    // Remove all segments we touched here from gid_conditions so they can be
    // recalculated.
    for (uint32_t gid : glyphs) {
      gid_conditions_[gid].RemoveSegments(segments);
    }

    for (uint32_t segment_index : segments) {
      segment_to_gid_conditions_[segment_index].subtract(glyphs);
    }
  }

  bool operator==(const GlyphConditionSet& other) const {
    return other.gid_conditions_ == gid_conditions_ &&
           other.segment_to_gid_conditions_ == other.segment_to_gid_conditions_;
  }

  bool operator!=(const GlyphConditionSet& other) const {
    return !(*this == other);
  }

 private:
  // Index in this vector is the glyph id associated with the condition at that
  // index.
  std::vector<GlyphConditions> gid_conditions_;

  // Index that tracks for each segment id which set of glyphs include that
  // segment in it's conditions.
  absl::flat_hash_map<uint32_t, common::GlyphSet> segment_to_gid_conditions_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_GLYPH_CONDITION_SET_H_