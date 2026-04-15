#ifndef IFT_ENCODER_GLYPH_CONDITION_SET_H_
#define IFT_ENCODER_GLYPH_CONDITION_SET_H_

#include <ostream>

#include "absl/container/flat_hash_map.h"
#include "ift/common/int_set.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/types.h"

namespace ift::encoder {

class GlyphConditionSet;

/*
 * A set of conditions which activate a specific single glyph.
 */
class GlyphConditions {
  friend GlyphConditionSet;

 public:
  GlyphConditions() : condition_(ActivationCondition::True(0)) {}

  const ActivationCondition& activation() const { return condition_; }

  bool operator==(const GlyphConditions& other) const {
    return condition_ == other.condition_;
  }

  void RemoveSegments(const common::SegmentSet& segments) {
    bool is_exclusive = condition_.IsExclusive();
    std::vector<common::SegmentSet> new_conditions;
    for (const auto& sub_group : condition_.conditions()) {
      common::SegmentSet modified = sub_group;
      modified.subtract(segments);
      if (!modified.empty()) {
        new_conditions.push_back(modified);
      }
    }

    if (new_conditions.empty()) {
      condition_ = ActivationCondition::True(0);
    } else if (is_exclusive && new_conditions.size() == 1 &&
               new_conditions.begin()->size() == 1) {
      condition_ = ActivationCondition::exclusive_segment(
          *new_conditions.begin()->min(), 0);
    } else {
      condition_ = ActivationCondition::composite_condition(new_conditions, 0);
    }
  }

 private:
  ActivationCondition condition_;
};

/*
 * Collection of per glyph conditions for all glyphs in a font.
 */
class GlyphConditionSet {
 public:
  GlyphConditionSet(uint32_t num_glyphs) { gid_conditions_.resize(num_glyphs); }

  friend void PrintTo(const GlyphConditionSet& conditions, std::ostream* os);
  static void PrintDiff(const GlyphConditionSet& a, const GlyphConditionSet& b);

  const GlyphConditions& ConditionsFor(glyph_id_t gid) const {
    return gid_conditions_[gid];
  }

  void AddAndCondition(glyph_id_t gid, segment_index_t segment) {
    auto& condition = gid_conditions_[gid].condition_;
    if (condition.IsAlwaysTrue()) {
      condition = ActivationCondition::exclusive_segment(segment, 0);
    } else {
      condition = ActivationCondition::And(
          condition, ActivationCondition::exclusive_segment(segment, 0));
    }
    segment_to_gid_conditions_[segment].insert(gid);
  }

  void AddOrCondition(glyph_id_t gid, segment_index_t segment) {
    auto& condition = gid_conditions_[gid].condition_;
    if (condition.IsAlwaysTrue()) {
      condition = ActivationCondition::or_segments({segment}, 0);
    } else {
      condition = ActivationCondition::Or(
          condition, ActivationCondition::or_segments({segment}, 0));
    }
    segment_to_gid_conditions_[segment].insert(gid);
  }

  void SetCondition(glyph_id_t gid, ActivationCondition condition) {
    common::SegmentSet segments = condition.TriggeringSegments();
    gid_conditions_[gid].condition_ = std::move(condition);
    for (segment_index_t s : segments) {
      segment_to_gid_conditions_[s].insert(gid);
    }
  }

  // Returns the set of glyphs that have 'segment' in their conditions.
  const ift::common::GlyphSet& GlyphsWithSegment(
      segment_index_t segment) const {
    const static ift::common::GlyphSet empty;
    auto it = segment_to_gid_conditions_.find(segment);
    if (it == segment_to_gid_conditions_.end()) {
      return empty;
    }
    return it->second;
  }

  // Clears out any stored information for glyphs and segments in this condition
  // set.
  void InvalidateGlyphInformation(const ift::common::GlyphSet& glyphs) {
    ift::common::SegmentSet touched;
    for (uint32_t gid : glyphs) {
      auto& condition = gid_conditions_[gid].condition_;
      touched.union_set(condition.TriggeringSegments());
      condition = ActivationCondition::True(0);
    }

    for (uint32_t segment_index : touched) {
      segment_to_gid_conditions_[segment_index].subtract(glyphs);
    }
  }

  void InvalidateGlyphInformation(const ift::common::SegmentSet& segments) {
    ift::common::GlyphSet touched;
    for (uint32_t segment_index : segments) {
      auto& entry = segment_to_gid_conditions_[segment_index];
      touched.union_set(entry);
      entry.clear();
    }

    for (uint32_t gid : touched) {
      gid_conditions_[gid].RemoveSegments(segments);
    }
  }

  void InvalidateGlyphInformation(const ift::common::GlyphSet& glyphs,
                                  const ift::common::SegmentSet& segments) {
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
  absl::flat_hash_map<uint32_t, ift::common::GlyphSet>
      segment_to_gid_conditions_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_GLYPH_CONDITION_SET_H_