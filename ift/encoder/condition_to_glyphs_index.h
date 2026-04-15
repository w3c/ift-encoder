#ifndef IFT_ENCODER_CONDITION_TO_GLYPHS_INDEX_H_
#define IFT_ENCODER_CONDITION_TO_GLYPHS_INDEX_H_

#include <optional>

#include "absl/container/btree_map.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/types.h"

namespace ift::encoder {

// Stores a mapping from ActivationCondition to a set of glyphs.
//
// Also maintains the reverse mapping indices.
class ConditionToGlyphsIndex {
 public:
  std::optional<ActivationCondition> Invalidate(glyph_id_t gid) {
    auto it = glyph_to_condition_.find(gid);
    if (it == glyph_to_condition_.end()) {
      return std::nullopt;
    }

    ActivationCondition condition = it->second;

    auto& glyphs = conditions_and_glyphs_.at(condition);
    glyphs.erase(gid);
    glyph_to_condition_.erase(gid);

    if (glyphs.empty()) {
      Remove(condition);
    }
    return condition;
  }

  void Remove(ActivationCondition condition) {
    auto it = conditions_and_glyphs_.find(condition);
    if (it == conditions_and_glyphs_.end()) {
      return;
    }

    for (glyph_id_t gid : it->second) {
      glyph_to_condition_.erase(gid);
    }

    conditions_and_glyphs_.erase(it);

    for (segment_index_t s : condition.TriggeringSegments()) {
      auto it = triggering_segment_to_conditions_.find(s);
      if (it != triggering_segment_to_conditions_.end()) {
        it->second.erase(condition);
        if (it->second.empty()) {
          triggering_segment_to_conditions_.erase(it);
        }
      }
    }
  }

  absl::Status Union(ActivationCondition condition, common::GlyphSet glyphs) {
    conditions_and_glyphs_[condition].union_set(glyphs);

    for (segment_index_t s : condition.TriggeringSegments()) {
      triggering_segment_to_conditions_[s].insert(condition);
    }

    for (glyph_id_t gid : glyphs) {
      auto [it, did_insert] =
          glyph_to_condition_.insert(std::pair(gid, condition));
      if (!did_insert && it->second != condition) {
        return absl::InternalError(
            "glyph_to_condition mapping does not match existing one.");
      }
    }

    return absl::OkStatus();
  }

  absl::Status Add(ActivationCondition condition, common::GlyphSet glyphs) {
    const auto& [new_value_it, did_insert] =
        conditions_and_glyphs_.insert(std::pair(condition, glyphs));

    if (!did_insert) {
      // If there's an existing value it must match what we're trying to add
      if (!new_value_it->second.is_subset_of(glyphs)) {
        return absl::InternalError(absl::StrCat(
            "Trying to add a condition and glyph mapping (",
            condition.ToString(), " => ", glyphs.ToString(),
            ") which "
            "would override an existing mapping (",
            new_value_it->first.ToString(), " => ",
            new_value_it->second.ToString(), ") to a different value."));
      }

      // We allow overrides that only increase the glyph set.
      glyphs.subtract(new_value_it->second);
      new_value_it->second.union_set(glyphs);
    } else {
      for (segment_index_t s : condition.TriggeringSegments()) {
        triggering_segment_to_conditions_[s].insert(new_value_it->first);
      }
    }

    for (glyph_id_t gid : glyphs) {
      bool did_insert =
          glyph_to_condition_.insert(std::pair(gid, new_value_it->first))
              .second;
      if (!did_insert) {
        return absl::InternalError(
            "Unexpected existing glyph to condition mapping.");
      }
    }

    return absl::OkStatus();
  }

  const absl::btree_map<ActivationCondition, common::GlyphSet>&
  ConditionsAndGlyphs() const {
    return conditions_and_glyphs_;
  }

  const absl::flat_hash_map<glyph_id_t, ActivationCondition>& GlyphToCondition()
      const {
    return glyph_to_condition_;
  }

  const absl::flat_hash_map<segment_index_t,
                            absl::btree_set<ActivationCondition>>&
  TriggeringSegmentToConditions() const {
    return triggering_segment_to_conditions_;
  }

  bool operator==(const ConditionToGlyphsIndex& other) const {
    return conditions_and_glyphs_ == other.conditions_and_glyphs_ &&
           glyph_to_condition_ == other.glyph_to_condition_ &&
           triggering_segment_to_conditions_ ==
               other.triggering_segment_to_conditions_;
  }

 private:
  absl::btree_map<ActivationCondition, common::GlyphSet> conditions_and_glyphs_;
  absl::flat_hash_map<glyph_id_t, ActivationCondition> glyph_to_condition_;
  absl::flat_hash_map<segment_index_t, absl::btree_set<ActivationCondition>>
      triggering_segment_to_conditions_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_CONDITION_TO_GLYPHS_INDEX_H_