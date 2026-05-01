#ifndef IFT_ENCODER_GLYPH_GROUPINGS_H_
#define IFT_ENCODER_GLYPH_GROUPINGS_H_

#include <cstdint>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "ift/common/int_set.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/condition_to_glyphs_index.h"
#include "ift/encoder/dependency_closure.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/glyph_condition_set.h"
#include "ift/encoder/glyph_partition.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/types.h"

namespace ift::encoder {

/*
 * Grouping of the glyphs in a font by the associated activation conditions.
 */
class GlyphGroupings {
 public:
  GlyphGroupings(uint32_t glyph_count) : combined_patches_(glyph_count) {}

  bool operator==(const GlyphGroupings& other) {
    return or_glyph_groups_ == other.or_glyph_groups_ &&
           exclusive_glyph_groups_ == other.exclusive_glyph_groups_ &&
           combined_or_glyph_groups_ == other.combined_or_glyph_groups_ &&
           conditions_and_glyphs_ == other.conditions_and_glyphs_ &&
           conditions_and_glyphs_pre_combination_ ==
               other.conditions_and_glyphs_pre_combination_ &&
           unmapped_glyphs_ == other.unmapped_glyphs_;
  }

  bool operator!=(const GlyphGroupings& other) { return !(*this == other); }

  const absl::flat_hash_map<ActivationCondition, ift::common::GlyphSet>&
  ConditionsAndGlyphs() const {
    return conditions_and_glyphs_.ConditionsAndGlyphs();
  }

  const absl::btree_set<ActivationCondition>& OrderedConditions() const {
    return conditions_and_glyphs_.OrderedConditions();
  }

  // Returns the set all of segments that are part of a disjunctive condition.
  // This includes segments that are part of exclusive conditions.
  ift::common::SegmentSet AllDisjunctiveSegments() const {
    ift::common::SegmentSet result;
    for (const auto& [c, _] : ConditionsAndGlyphs()) {
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
  const ift::common::GlyphSet& ExclusiveGlyphs(segment_index_t s) const {
    static const ift::common::GlyphSet empty{};
    if (combined_exclusive_segments_.contains(s)) {
      return empty;
    }

    auto it = exclusive_glyph_groups_.find(s);
    if (it != exclusive_glyph_groups_.end()) {
      return it->second;
    }
    return empty;
  }

  // Returns the set of glyphs that are considered unmapped,
  // which will be placed in the fallback (always loaded) patch.
  ift::common::GlyphSet UnmappedGlyphs() const { return unmapped_glyphs_; }

  // Returns the set of glyphs that were unmapped but had conditions
  // found for them.
  const ift::common::GlyphSet& FoundConditionGlyphs() const {
    return found_condition_glyphs_;
  }

  // Returns a list of conditions which include segment.
  const absl::flat_hash_set<ActivationCondition>& TriggeringSegmentToConditions(
      segment_index_t segment) const {
    static absl::flat_hash_set<ActivationCondition> empty;
    auto it =
        conditions_and_glyphs_.TriggeringSegmentToConditions().find(segment);
    if (it != conditions_and_glyphs_.TriggeringSegmentToConditions().end()) {
      return it->second;
    }
    return empty;
  }

  // Add a set of glyphs to an existing exclusive group (and_group of one
  // segment).
  absl::Status AddGlyphsToExclusiveGroup(segment_index_t exclusive_segment,
                                         const ift::common::GlyphSet& glyphs);

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
  absl::Status CombinePatches(const ift::common::GlyphSet& a,
                              const ift::common::GlyphSet& b);

  // Updates this glyph grouping for all glyphs in the 'glyphs' set to match
  // the associated conditions in 'glyph_condition_set'.
  //
  // Also applies any requested patch cominbations from CombinePatches().
  absl::Status GroupGlyphs(
      const RequestedSegmentationInformation& segmentation_info,
      const GlyphConditionSet& glyph_condition_set,
      GlyphClosureCache& closure_cache,
      std::optional<DependencyClosure*> dependency_closure,
      ift::common::GlyphSet glyphs,
      const ift::common::SegmentSet& modified_segments,
      bool additional_conditions_check = true);

  // Converts this grouping into a finalized GlyphSegmentation.
  absl::StatusOr<GlyphSegmentation> ToGlyphSegmentation(
      const RequestedSegmentationInformation& segmentation_info) const;

  std::optional<ActivationCondition> GlyphToCondition(glyph_id_t gid) const {
    auto it = conditions_and_glyphs_.GlyphToCondition().find(gid);
    if (it == conditions_and_glyphs_.GlyphToCondition().end()) {
      return std::nullopt;
    }

    return it->second;
  }

 private:
  void CollectSegments(glyph_id_t gid, ift::common::SegmentSet& segments);

  ift::common::GlyphSet ModifiedGlyphs(
      const ift::common::SegmentSet& segments) const;

  // Perform a more detailed analysis to try and find more granular conditions
  // for fallback glyphs. Will replace the fallback glyphs with any found
  // conditions.
  absl::Status FindFallbackGlyphConditions(
      const RequestedSegmentationInformation& segmentation_info,
      const GlyphConditionSet& glyph_condition_set,
      const ift::common::SegmentSet& inscope_segments,
      GlyphClosureCache& closure_cache,
      std::optional<DependencyClosure*> dependency_closure);

  // Removes all stored grouping information related to glyph with the specified
  // condition.
  void InvalidateGlyphInformation(uint32_t gid);

  absl::Status RecomputeCombinedConditionsIfNeeded(
      const ift::common::GlyphSet& modified_glyphs) {
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
      ift::common::SegmentSet& exclusive_segments,
      absl::btree_set<ift::common::SegmentSet>& or_conditions) const;

  // Computes a mapping from a representative glyph of each combined patch to
  // the set of segments and glyphs after combination.
  absl::Status ComputeConditionExpansionMap(
      const ift::common::SegmentSet& exclusive_segments,
      const absl::btree_set<ift::common::SegmentSet>& or_conditions,
      absl::flat_hash_map<glyph_id_t, ift::common::SegmentSet>&
          merged_conditions,
      absl::flat_hash_map<glyph_id_t, ift::common::GlyphSet>& merged_glyphs);

  absl::Status AddConditionAndGlyphs(ActivationCondition condition,
                                     ift::common::GlyphSet glyphs,
                                     bool pre_combination = true) {
    TRYV(conditions_and_glyphs_.Add(condition, glyphs));
    if (pre_combination) {
      TRYV(conditions_and_glyphs_pre_combination_.Add(condition, glyphs));
    }

    return absl::OkStatus();
  }

  absl::Status UnionConditionAndGlyphs(ActivationCondition condition,
                                       ift::common::GlyphSet glyphs) {
    TRYV(conditions_and_glyphs_.Union(condition, glyphs));
    return conditions_and_glyphs_pre_combination_.Union(condition, glyphs);
  }

  void RemoveConditionAndGlyphs(ActivationCondition condition,
                                bool pre_combination = true) {
    conditions_and_glyphs_.Remove(condition);
    if (pre_combination) {
      conditions_and_glyphs_pre_combination_.Remove(condition);
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

  absl::flat_hash_map<ift::common::SegmentSet, ift::common::GlyphSet>
      or_glyph_groups_;
  absl::flat_hash_map<segment_index_t, ift::common::GlyphSet>
      exclusive_glyph_groups_;

  // This is a set of disjunctive conditions which have been combined by the
  // CombinePatches() mechanism. Does not store groupings which have not been
  // modified the the mechanism.
  absl::flat_hash_map<ift::common::SegmentSet, ift::common::GlyphSet>
      combined_or_glyph_groups_;

  // This is a set of segments which are normally exclusive but have been
  // combined via the patch combination mechanism and are no longer present.
  ift::common::SegmentSet combined_exclusive_segments_;

  // An alternate representation of and/or_glyph_groups_, derived from them.
  ConditionToGlyphsIndex<true> conditions_and_glyphs_;
  ConditionToGlyphsIndex<false> conditions_and_glyphs_pre_combination_;

  // These glyphs aren't mapped by any conditions and as a result should be
  // included in the fallback patch.
  ift::common::GlyphSet unmapped_glyphs_;

  // These glyphs were previously considered unmapped, but have had conditions
  // found for them.
  ift::common::GlyphSet found_condition_glyphs_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_GLYPH_GROUPINGS_H_
