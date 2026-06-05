#ifndef IFT_ENCODER_GLYPH_CONDITION_SET_H_
#define IFT_ENCODER_GLYPH_CONDITION_SET_H_

#include <ostream>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
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

  // Returns the list of segments which were actually removed, may
  // contain more than the input due to simplification.
  common::SegmentSet RemoveSegments(const common::SegmentSet& segments) {
    common::SegmentSet removed_segments = condition_.TriggeringSegments();
    bool was_disjunctive = condition_.IsPurelyDisjunctive();

    std::vector<common::SegmentSet> new_conditions;
    for (const auto& sub_group : condition_.conditions()) {
      common::SegmentSet modified = sub_group;
      modified.subtract(segments);
      if (!modified.empty()) {
        new_conditions.push_back(modified);
      }
    }

    bool new_is_unitary =
        new_conditions.size() == 1 && new_conditions.begin()->size() == 1;

    if (new_conditions.empty()) {
      condition_ = ActivationCondition::True(0);
    } else if (!was_disjunctive && new_is_unitary) {
      condition_ = ActivationCondition::exclusive_segment(
          *new_conditions.begin()->min(), 0);
    } else if (was_disjunctive) {
      condition_ = ActivationCondition::or_segments(*new_conditions.begin(), 0);
    } else {
      condition_ = ActivationCondition::composite_condition(new_conditions, 0);
    }

    removed_segments.subtract(condition_.TriggeringSegments());
    return removed_segments;
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

  // Used in testing to ensure the forward and reverse condition mappings are
  // in sync.
  absl::Status Validate() const {
    for (const auto& [s, glyphs] : segment_to_gid_conditions_) {
      for (glyph_id_t g : glyphs) {
        bool correct =
            gid_conditions_.at(g).condition_.TriggeringSegments().contains(s);
        if (!correct) {
          LOG(ERROR) << "glyph condition set state is inconsisent: g" << g
                     << " condition does not have s" << s;
          LOG(ERROR) << "  g" << g << " condition: "
                     << gid_conditions_.at(g).condition_.ToString();
          LOG(ERROR) << "  s" << s << " glyphs: " << glyphs.ToString();
          return absl::InternalError(
              "glyph condition set state is inconsisent.");
        }
      }
    }
    return absl::OkStatus();
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
    for (segment_index_t s :
         gid_conditions_[gid].condition_.TriggeringSegments()) {
      segment_to_gid_conditions_[s].erase(gid);
    }
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
      RemoveSegmentsFromGlyph(gid, segments);
    }
  }

  void InvalidateGlyphInformation(const ift::common::GlyphSet& glyphs,
                                  const ift::common::SegmentSet& segments) {
    for (uint32_t gid : glyphs) {
      RemoveSegmentsFromGlyph(gid, segments);
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
  void RemoveSegmentsFromGlyph(glyph_id_t gid,
                               const ift::common::SegmentSet& segments) {
    // Remove segments from a gid condition may remove additional segments
    // not in the input due to simplifcation. So check the old and new segment
    // set to see exactly what got removed.
    common::SegmentSet removed = gid_conditions_[gid].RemoveSegments(segments);
    for (segment_index_t s : removed) {
      segment_to_gid_conditions_[s].erase(gid);
    }
  }
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