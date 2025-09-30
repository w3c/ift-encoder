#ifndef IFT_ENCODER_GLYPH_GROUPINGS_H_
#define IFT_ENCODER_GLYPH_GROUPINGS_H_

#include <cstdint>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "common/int_set.h"
#include "common/try.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/glyph_condition_set.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/glyph_union.h"
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
      : glyph_union_(glyph_count) {
    uint32_t index = 0;
    for (const auto& s : segments) {
      if (!s.Definition().Empty()) {
        fallback_segments_.insert(index);
      }
      index++;
    }
  }

  bool operator==(const GlyphGroupings& other) {
    // TODO XXXX include the expanded stuff.
    return and_glyph_groups_ == other.and_glyph_groups_ &&
           or_glyph_groups_ == other.or_glyph_groups_ &&
           exclusive_glyph_groups_ == other.exclusive_glyph_groups_;
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

  const common::GlyphSet& ExclusiveGlyphs(segment_index_t s) const {
    // TODO XXXX use expanded groups (also check other public methods)
    static const common::GlyphSet empty{};
    auto it = exclusive_glyph_groups_.find(s);
    if (it != exclusive_glyph_groups_.end()) {
      return it->second;
    }
    return empty;
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

  // Removes all stored grouping information related to glyph with the specified
  // condition.
  void InvalidateGlyphInformation(const GlyphConditions& condition, uint32_t gid);

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
  void AddGlyphsToExclusiveGroup(segment_index_t exclusive_segment,
                                 const common::GlyphSet& glyphs) {
    auto& exc_glyphs = exclusive_glyph_groups_[exclusive_segment];
    exc_glyphs.union_set(glyphs);

    ActivationCondition condition =
        ActivationCondition::exclusive_segment(exclusive_segment, 0);
    conditions_and_glyphs_[condition].union_set(glyphs);
    // triggering segment to conditions is not affected by this change, so
    // doesn't need an update.
  }

  // Specify that any patches containing glyphs from either a or b should be
  // merged into one patch. Only affects exclusive and disjunctive patches.
  //
  // Automatically updates all of the groupings/conditions to reflect the changes.
  absl::Status UnionPatches(const common::GlyphSet& a,
                            const common::GlyphSet& b);

  // Updates this glyph grouping for all glyphs in the 'glyphs' set to match
  // the associated conditions in 'glyph_condition_set'.
  absl::Status GroupGlyphs(
      const RequestedSegmentationInformation& segmentation_info,
      const GlyphConditionSet& glyph_condition_set,
      GlyphClosureCache& closure_cache, const common::GlyphSet& glyphs);

  // Converts this grouping into a finalized GlyphSegmentation.
  absl::StatusOr<GlyphSegmentation> ToGlyphSegmentation(
      const RequestedSegmentationInformation& segmentation_info) const {
    GlyphSegmentation segmentation(
        segmentation_info.InitFontSegmentWithoutDefaults(),
        segmentation_info.InitFontGlyphs(), unmapped_glyphs_);
    segmentation.CopySegments(segmentation_info.SegmentSubsetDefinitions());

    TRYV(GlyphSegmentation::GroupsToSegmentation(
        and_glyph_groups_, or_glyph_groups_, exclusive_glyph_groups_,
        fallback_segments_, segmentation));

    return segmentation;
  }

 private:
  // XXXXX naming here isn't very helpful eg. how is ReplaceConditions different from UpdateExpandedConditions and
  // UpdateReplacementConditions? may want to also indicate where applicable when something applies only to disjunctive
  // stuff.
  // XXXX method comments and so on.
  absl::Status RecomputeExpandedConditions(const GlyphConditionSet& glyph_condition_set);
  absl::Status ConditionsAffectedByUnion(
    const GlyphConditionSet& glyph_condition_set,
    common::SegmentSet& exclusive_segments,
    absl::btree_set<common::SegmentSet>& or_conditions
  ) const;

  absl::Status ComputeConditionExpansionMap(
    const common::SegmentSet& exclusive_segments,
    const absl::btree_set<common::SegmentSet>& or_conditions,
    absl::flat_hash_map<glyph_id_t, common::SegmentSet>& merged_conditions,
    absl::flat_hash_map<glyph_id_t, common::GlyphSet>& merged_glyphs);


  void AddConditionAndGlyphs(ActivationCondition condition,
                             common::GlyphSet glyphs) {
    const auto& [new_value_it, did_insert] =
        conditions_and_glyphs_.insert(std::pair(condition, glyphs));
    for (segment_index_t s : condition.TriggeringSegments()) {
      triggering_segment_to_conditions_[s].insert(new_value_it->first);
    }
  }

  void RemoveConditionAndGlyphs(ActivationCondition condition) {
    conditions_and_glyphs_.erase(condition);
    for (segment_index_t s : condition.TriggeringSegments()) {
      triggering_segment_to_conditions_[s].erase(condition);
    }
  }

  void RemoveAllExpandedConditions();

  // Tracks patches that are should be merged directly together. Any disjunctive
  // or exclusive patches which belong to the same union group will be merged
  // together. The merge is done by combining all of the linked glyphs into a
  // single patch and merging all of the condition segments into a single
  // condition.
  //
  // Conjunctive conditions/patches are unaffected by this mechanism since they
  // can't be joined together in the same fashion.
  //
  // To illustrate this here is a simple example. Let's say we have the
  // following conditions:
  //
  // - s1 -> {g1}
  // - s2 -> {g2}
  // - s2 or s3 -> {g3, g4}
  //
  // And the union has {g1, g4}
  //
  // Then this would result in the final conditions:
  // s2 -> {g2}
  // s1 or s2 or s3 -> {g1, g3, g4}
  //
  // Notice how the (s1), (s2 or s3) conditions/patches have been merged while
  // the s2 -> {g2} condition/patch is left untouched.
  GlyphUnion glyph_union_; // TODO XXXX don't need this as separate from disjunctive_partition_

  absl::btree_map<common::SegmentSet, common::GlyphSet> and_glyph_groups_;
  absl::btree_map<common::SegmentSet, common::GlyphSet> or_glyph_groups_;
  absl::btree_map<segment_index_t, common::GlyphSet> exclusive_glyph_groups_;

  // TODO XXXX rename partition_to_activating_segments?
  // This is a set of disjunctive conditions which have been expanded by the glyph union mechanism
  // Does not store groupings which have not been modified the the mechanism.
  absl::btree_map<common::SegmentSet, common::GlyphSet> expanded_or_glyph_groups_;
  // TODO XXX renamed "expanded" to something more descriptive, maybe "merged"?

  // An alternate representation of and/or_glyph_groups_, derived from them.
  absl::btree_map<ActivationCondition, common::GlyphSet> conditions_and_glyphs_;

  // Index that maps segments to all conditions in conditions_and_glyphs_ which
  // reference that segment.
  absl::flat_hash_map<uint32_t, absl::btree_set<ActivationCondition>>
      triggering_segment_to_conditions_;

  // Set of segments in the fallback condition.
  common::SegmentSet fallback_segments_;

  // These glyphs aren't mapped by any conditions and as a result should be
  // included in the fallback patch.
  common::GlyphSet unmapped_glyphs_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_GLYPH_GROUPINGS_H_