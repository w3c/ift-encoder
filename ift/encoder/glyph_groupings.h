#ifndef IFT_ENCODER_GLYPH_GROUPINGS_H_
#define IFT_ENCODER_GLYPH_GROUPINGS_H_

#include <cstdint>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "common/int_set.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/glyph_condition_set.h"
#include "ift/encoder/glyph_partition.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"

namespace ift::encoder {

/*
 * Grouping of the glyphs in a font by the associated activation conditions.
 */
class GlyphGroupings {
 public:
  GlyphGroupings(const std::vector<Segment>& segments, uint32_t glyph_count)
      : combined_patches_(glyph_count) {
    uint32_t index = 0;
    for (const auto& s : segments) {
      if (!s.Definition().Empty()) {
        fallback_segments_.insert(index);
      }
      index++;
    }
  }

  bool operator==(const GlyphGroupings& other) {
    return and_glyph_groups_ == other.and_glyph_groups_ &&
           or_glyph_groups_ == other.or_glyph_groups_ &&
           exclusive_glyph_groups_ == other.exclusive_glyph_groups_ &&
           combined_or_glyph_groups_ == other.combined_or_glyph_groups_ &&
           conditions_and_glyphs_ == other.conditions_and_glyphs_;
  }

  bool operator!=(const GlyphGroupings& other) { return !(*this == other); }

  const absl::btree_map<ActivationCondition, common::GlyphSet>&
  ConditionsAndGlyphs() const {
    return conditions_and_glyphs_;
  }

  // Returns the set all of segments that are part of a disjunctive condition.
  // This includes segments that are part of exclusive conditions.
  common::SegmentSet AllDisjunctiveSegments() const {
    common::SegmentSet result;
    for (const auto& [c, _] : conditions_and_glyphs_) {
      if (c.conditions().size() != 1) {
        // Any condition with more than one segment group is conjunctive.
        continue;
      }
      for (segment_index_t s : *c.conditions().begin()) {
        result.insert(s);
      }
    }
    return result;
  }

  const GlyphPartition& CombinedPatches() const { return combined_patches_; }

  // Returns the set of glyphs that are exclusive to segment s.
  //
  // Exclusive means that set of glyphs that are needed if and only if
  // segment s is present.
  const common::GlyphSet& ExclusiveGlyphs(segment_index_t s) const {
    static const common::GlyphSet empty{};
    if (combined_exclusive_segments_.contains(s)) {
      return empty;
    }

    auto it = exclusive_glyph_groups_.find(s);
    if (it != exclusive_glyph_groups_.end()) {
      return it->second;
    }
    return empty;
  }

  // Returns the set of glyphs in the fallback (always loaded) patch.
  common::GlyphSet FallbackGlyphs() const {
    if (fallback_segments_.empty()) {
      return common::GlyphSet{};
    }

    auto it = or_glyph_groups_.find(fallback_segments_);
    if (it == or_glyph_groups_.end()) {
      return common::GlyphSet{};
    }

    return it->second;
  }

  const common::SegmentSet& FallbackSegments() const {
    return fallback_segments_;
  }

  // Returns a list of conditions which include segment.
  const absl::btree_set<ActivationCondition>& TriggeringSegmentToConditions(
      segment_index_t segment) const {
    static absl::btree_set<ActivationCondition> empty;
    auto it = triggering_segment_to_conditions_.find(segment);
    if (it != triggering_segment_to_conditions_.end()) {
      return it->second;
    }
    return empty;
  }

  // Remove a set of segments from the fallback segments set.
  // Invalidates any existing fallback segments or glyph group.
  void RemoveFallbackSegments(const common::SegmentSet& removed_segments) {
    // Invalidate the existing fallback segment 'or group', it will be fully
    // recomputed by GroupGlyphs
    or_glyph_groups_.erase(fallback_segments_);
    for (segment_index_t segment_index : removed_segments) {
      fallback_segments_.erase(segment_index);
    }
  }

  // Add a set of glyphs to an existing exclusive group (and_group of one
  // segment).
  absl::Status AddGlyphsToExclusiveGroup(segment_index_t exclusive_segment,
                                         const common::GlyphSet& glyphs);

  // Specify that any patches containing glyphs from either a or b should be
  // merged into one patch. Only affects exclusive and disjunctive patches.
  //
  // Combination will be performed by merging the glyphs of the combined patches
  // and merging the conditions. For example, if we have the conditions:
  //
  // if (s0) -> {a, b, c}
  // if (s1 OR s2) -> {d, e}
  // if (s0 OR s2) -> {f, g}
  //
  // And call CombinePatches({a}, {d}), then the updated conditions would be:
  //
  // if (s0 OR s1 OR s2) -> {a, b, c, d, e}
  // if (s0 OR s2) -> {f, g}
  //
  // Invalidates the current grouping, GroupGlyphs() will need to be called
  // afterwards to realize the changes.
  absl::Status CombinePatches(const common::GlyphSet& a,
                              const common::GlyphSet& b);

  // Updates this glyph grouping for all glyphs in the 'glyphs' set to match
  // the associated conditions in 'glyph_condition_set'.
  //
  // Also applies any requested patch cominbations from CombinePatches().
  absl::Status GroupGlyphs(
      const RequestedSegmentationInformation& segmentation_info,
      const GlyphConditionSet& glyph_condition_set,
      GlyphClosureCache& closure_cache, common::GlyphSet glyphs,
      const common::SegmentSet& modified_segments);

  // Converts this grouping into a finalized GlyphSegmentation.
  absl::StatusOr<GlyphSegmentation> ToGlyphSegmentation(
      const RequestedSegmentationInformation& segmentation_info) const;

  std::optional<ActivationCondition> GlyphToCondition(glyph_id_t gid) const {
    auto it = glyph_to_condition_.find(gid);
    if (it == glyph_to_condition_.end()) {
      return std::nullopt;
    }

    return it->second;
  }

 private:
  void CollectSegments(glyph_id_t gid, common::SegmentSet& segments);

  common::GlyphSet ModifiedGlyphs(const common::SegmentSet& segments) const;

  // Perform a more detailed analysis to try and find more granular conditions
  // for fallback glyphs. Will replace the fallback glyphs with any found
  // conditions.
  absl::Status FindFallbackGlyphConditions(
      const RequestedSegmentationInformation& segmentation_info,
      const GlyphConditionSet& glyph_condition_set,
      const common::SegmentSet& inscope_segments,
      GlyphClosureCache& closure_cache);

  // Removes all stored grouping information related to glyph with the specified
  // condition.
  void InvalidateGlyphInformation(uint32_t gid);

  absl::Status RecomputeCombinedConditionsIfNeeded(
      const common::GlyphSet& modified_glyphs) {
    if (!combined_patches_dirty_) {
      for (glyph_id_t gid : modified_glyphs) {
        if (TRY(combined_patches_.GlyphsFor(gid)).size() > 1) {
          RemoveAllCombinedConditions();
          break;
        }
      }
    }

    if (combined_patches_dirty_) {
      return RecomputeCombinedConditions();
    }

    return absl::OkStatus();
  }

  // Looks at the requested combinations from combined_patches_ and
  // computes any resulting combinations, then updates the condition_and_glyphs_
  // with the combined conditions.
  //
  // The combined groupings are tracked separately in combined_or_glyph_groups_,
  // or_glyph_groups is not changed.
  absl::Status RecomputeCombinedConditions();

  // Finds all conditions (exclusive and disjunctive) which may interact with
  // the specified patch combinations in combined_patches_.
  absl::Status ConditionsAffectedByCombination(
      common::SegmentSet& exclusive_segments,
      absl::btree_set<common::SegmentSet>& or_conditions) const;

  // Computes a mapping from a representative glyph of each combined patch to
  // the set of segments and glyphs after combination.
  absl::Status ComputeConditionExpansionMap(
      const common::SegmentSet& exclusive_segments,
      const absl::btree_set<common::SegmentSet>& or_conditions,
      absl::flat_hash_map<glyph_id_t, common::SegmentSet>& merged_conditions,
      absl::flat_hash_map<glyph_id_t, common::GlyphSet>& merged_glyphs);

  absl::Status AddConditionAndGlyphs(ActivationCondition condition,
                                     common::GlyphSet glyphs,
                                     bool pre_combination = true) {
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
      if (pre_combination) {
        did_insert |= glyph_to_condition_pre_combination_
                          .insert(std::pair(gid, new_value_it->first))
                          .second;
      }
      if (!did_insert) {
        return absl::InternalError(
            "Unexpected existing glyph to condition mapping.");
      }
    }

    return absl::OkStatus();
  }

  absl::Status UnionConditionAndGlyphs(ActivationCondition condition,
                                       common::GlyphSet glyphs) {
    conditions_and_glyphs_[condition].union_set(glyphs);

    for (segment_index_t s : condition.TriggeringSegments()) {
      triggering_segment_to_conditions_[s].insert(condition);
    }

    for (glyph_id_t gid : glyphs) {
      auto [it_1, did_insert_1] =
          glyph_to_condition_.insert(std::pair(gid, condition));
      if (!did_insert_1 && it_1->second != condition) {
        return absl::InternalError(
            "glyph_to_condition mapping does not match existing one.");
      }
      auto [it_2, did_insert_2] =
          glyph_to_condition_pre_combination_.insert(std::pair(gid, condition));
      if (!did_insert_2 && it_2->second != condition) {
        return absl::InternalError(
            "glyph_to_condition mapping does not match existing one.");
      }
    }

    return absl::OkStatus();
  }

  void RemoveConditionAndGlyphs(ActivationCondition condition,
                                bool pre_combination = true) {
    auto it = conditions_and_glyphs_.find(condition);
    if (it == conditions_and_glyphs_.end()) {
      return;
    }

    for (glyph_id_t gid : it->second) {
      glyph_to_condition_.erase(gid);
      if (pre_combination) {
        glyph_to_condition_pre_combination_.erase(gid);
      }
    }

    conditions_and_glyphs_.erase(it);
    for (segment_index_t s : condition.TriggeringSegments()) {
      triggering_segment_to_conditions_[s].erase(condition);
    }
  }

  // Clears out all conditions in conditions_and_glyphs_ which were produced
  // by combinations specified in combined_patches_.
  void RemoveAllCombinedConditions();

  // Tracks patches that are should be merged directly together. Any disjunctive
  // or exclusive patches which belong to the same partition will be merged
  // together. The merge is done by combining all of the linked glyphs into a
  // single patch and merging all of the condition segments into a single
  // condition.
  //
  // Conjunctive conditions/patches are unaffected by this mechanism since they
  // can't be joined together in the same fashion.
  GlyphPartition combined_patches_;
  bool combined_patches_dirty_ = false;

  absl::btree_map<common::SegmentSet, common::GlyphSet> and_glyph_groups_;
  absl::btree_map<common::SegmentSet, common::GlyphSet> or_glyph_groups_;
  absl::btree_map<segment_index_t, common::GlyphSet> exclusive_glyph_groups_;

  // This is a set of disjunctive conditions which have been combined by the
  // CombinePatches() mechanism. Does not store groupings which have not been
  // modified the the mechanism.
  absl::btree_map<common::SegmentSet, common::GlyphSet>
      combined_or_glyph_groups_;

  // This is a set of segments which are normally exclusive but have been
  // combined via the patch combination mechanism and are no longer present.
  common::SegmentSet combined_exclusive_segments_;

  // An alternate representation of and/or_glyph_groups_, derived from them.
  absl::btree_map<ActivationCondition, common::GlyphSet> conditions_and_glyphs_;

  // Index that maps segments to all conditions in conditions_and_glyphs_ which
  // reference that segment.
  absl::flat_hash_map<segment_index_t, absl::btree_set<ActivationCondition>>
      triggering_segment_to_conditions_;

  absl::flat_hash_map<glyph_id_t, ActivationCondition>
      glyph_to_condition_pre_combination_;
  absl::flat_hash_map<glyph_id_t, ActivationCondition> glyph_to_condition_;

  // Set of segments in the fallback condition.
  common::SegmentSet fallback_segments_;

  // These glyphs aren't mapped by any conditions and as a result should be
  // included in the fallback patch.
  common::GlyphSet unmapped_glyphs_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_GLYPH_GROUPINGS_H_
