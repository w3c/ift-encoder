#include "ift/encoder/glyph_groupings.h"

#include <optional>

#include "absl/container/btree_set.h"
#include "common/int_set.h"
#include "common/try.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/glyph_condition_set.h"
#include "ift/encoder/glyph_partition.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/types.h"

using absl::btree_map;
using absl::btree_set;
using absl::flat_hash_map;
using absl::Status;
using absl::StatusOr;
using common::GlyphSet;
using common::SegmentSet;

namespace ift::encoder {

void GlyphGroupings::InvalidateGlyphInformation(
    const GlyphConditions& condition, uint32_t gid) {
  if (condition.and_segments.size() == 1) {
    segment_index_t s = *condition.and_segments.begin();
    exclusive_glyph_groups_[s].erase(gid);
    ActivationCondition activation_condition = ActivationCondition::exclusive_segment(s, 0);
    conditions_and_glyphs_[activation_condition].erase(gid);

    if (exclusive_glyph_groups_[s].empty()) {
      exclusive_glyph_groups_.erase(s);
      RemoveConditionAndGlyphs(activation_condition);
    }
  }

  auto it = and_glyph_groups_.find(condition.and_segments);
  if (it != and_glyph_groups_.end()) {
    it->second.erase(gid);
    ActivationCondition activation_condition =
        ActivationCondition::and_segments(condition.and_segments, 0);
    conditions_and_glyphs_[activation_condition].erase(gid);

    if (it->second.empty()) {
      and_glyph_groups_.erase(it);
      RemoveConditionAndGlyphs(activation_condition);
    }
  }

  it = or_glyph_groups_.find(condition.or_segments);
  if (it != or_glyph_groups_.end()) {
    it->second.erase(gid);
    ActivationCondition activation_condition = ActivationCondition::or_segments(condition.or_segments, 0);
    conditions_and_glyphs_[activation_condition].erase(gid);

    if (it->second.empty()) {
      or_glyph_groups_.erase(it);
      RemoveConditionAndGlyphs(activation_condition);
    }
  }

  unmapped_glyphs_.erase(gid);

  // Any changes may affect in complex ways the combined conditions
  // so remove them all. They will be fully recalculated during grouping.
  RemoveAllCombinedConditions();
}

void GlyphGroupings::RemoveAllCombinedConditions() {
  for (const auto& [segments, _] : combined_or_glyph_groups_) {
    RemoveConditionAndGlyphs(ActivationCondition::or_segments(segments, 0));
  }
  combined_or_glyph_groups_.clear();
  combined_exclusive_segments_.clear();
}

Status GlyphGroupings::CombinePatches(const GlyphSet& a,
                                    const GlyphSet& b) {
  TRYV(combined_patches_.Union(a));
  TRYV(combined_patches_.Union(b));
  auto a_min = a.min();
  auto b_min = b.min();
  if (a_min.has_value() && b_min.has_value()) {
    TRYV(combined_patches_.Union(*a_min, *b_min));
  }

  RemoveAllCombinedConditions();

  return absl::OkStatus();
}

Status GlyphGroupings::GroupGlyphs(
    const RequestedSegmentationInformation& segmentation_info,
    const GlyphConditionSet& glyph_condition_set,
    GlyphClosureCache& closure_cache, const GlyphSet& glyphs) {
  const auto& initial_closure = segmentation_info.InitFontGlyphs();
  RemoveAllCombinedConditions();

  SegmentSet modified_exclusive_segments;
  btree_set<SegmentSet> modified_and_groups;
  btree_set<SegmentSet> modified_or_groups;
  for (glyph_id_t gid : glyphs) {
    const auto& condition = glyph_condition_set.ConditionsFor(gid);

    if (!condition.and_segments.empty()) {
      if (condition.and_segments.size() == 1) {
        segment_index_t s = *condition.and_segments.begin();
        exclusive_glyph_groups_[s].insert(gid);
        modified_exclusive_segments.insert(s);
      } else {
        and_glyph_groups_[condition.and_segments].insert(gid);
        modified_and_groups.insert(condition.and_segments);
      }
    }

    if (!condition.or_segments.empty()) {
      or_glyph_groups_[condition.or_segments].insert(gid);
      modified_or_groups.insert(condition.or_segments);
    }

    if (condition.and_segments.empty() && condition.or_segments.empty() &&
        !initial_closure.contains(gid) &&
        segmentation_info.FullClosure().contains(gid)) {
      unmapped_glyphs_.insert(gid);
    }
  }

  for (segment_index_t s : modified_exclusive_segments) {
    auto condition = ActivationCondition::exclusive_segment(s, 0);
    AddConditionAndGlyphs(condition, exclusive_glyph_groups_[s]);
  }

  for (const auto& and_group : modified_and_groups) {
    auto condition = ActivationCondition::and_segments(and_group, 0);
    AddConditionAndGlyphs(condition, and_glyph_groups_[and_group]);
  }

  // Any of the or_set conditions we've generated may have some additional
  // conditions that were not detected. Therefore we need to rule out the
  // presence of these additional conditions if an or group is able to be
  // used.
  for (const auto& or_group : modified_or_groups) {
    auto& glyphs = or_glyph_groups_[or_group];

    SegmentSet all_other_segment_ids;
    if (!segmentation_info.Segments().empty()) {
      all_other_segment_ids.insert_range(
          0, segmentation_info.Segments().size() - 1);
      all_other_segment_ids.subtract(or_group);
    }

    GlyphSet or_gids = TRY(closure_cache.CodepointsToOrGids(
        segmentation_info, all_other_segment_ids));

    // Any "OR" glyphs associated with all other codepoints have some
    // additional conditions to activate so we can't safely include them into
    // this or condition. They are instead moved to the set of unmapped
    // glyphs.
    for (uint32_t gid : or_gids) {
      if (glyphs.erase(gid) > 0) {
        unmapped_glyphs_.insert(gid);
      }
    }

    ActivationCondition condition =
        ActivationCondition::or_segments(or_group, 0);
    if (glyphs.empty()) {
      // Group has been emptied out, so it's no longer needed.
      or_glyph_groups_.erase(or_group);
      RemoveConditionAndGlyphs(condition);
      continue;
    }

    AddConditionAndGlyphs(condition, glyphs);
  }

  // The combined conditions can't be incrementally updated, so we recompute
  // them in full.
  TRYV(RecomputeCombinedConditions(glyph_condition_set));

  for (uint32_t gid : unmapped_glyphs_) {
    // this glyph is not activated anywhere but is needed in the full closure
    // so add it to an activation condition of any segment.
    or_glyph_groups_[fallback_segments_].insert(gid);
  }

  // Note: we don't need to include the fallback segment/condition in
  //       conditions_and_glyphs since all downstream processing which
  //       utilizes that map ignores the fallback segment.

  return absl::OkStatus();
}

Status GlyphGroupings::RecomputeCombinedConditions(const GlyphConditionSet& glyph_condition_set) {
  // To minimize the amount of work we need to do we first detect which segments are potentially
  // affected by the patch combination mechanism and then limit processing just to those.
  SegmentSet exclusive_segments;
  btree_set<SegmentSet> or_conditions;
  TRYV(ConditionsAffectedByCombination(glyph_condition_set, exclusive_segments, or_conditions));

  flat_hash_map<glyph_id_t, SegmentSet> merged_conditions;
  flat_hash_map<glyph_id_t, GlyphSet> merged_glyphs;
  TRYV(ComputeConditionExpansionMap(exclusive_segments, or_conditions, merged_conditions, merged_glyphs));

  for (const auto& [rep, segments] : merged_conditions) {
    const GlyphSet& gids = merged_glyphs.at(rep);
    ActivationCondition condition = ActivationCondition::or_segments(segments, 0);
    if (segments.size() == 1 && exclusive_segments.contains(*segments.min())) {
      // This is actually an exclusive condition, and is not expanded.
      condition = ActivationCondition::exclusive_segment(*segments.min(), 0);
    } else {
      SegmentSet segments_copy = segments;
      combined_or_glyph_groups_[segments_copy] = gids;
    }

    AddConditionAndGlyphs(condition, gids);
  }

  return absl::OkStatus();
}

Status GlyphGroupings::ConditionsAffectedByCombination(
  const GlyphConditionSet& glyph_condition_set,
  SegmentSet& exclusive_segments,
  btree_set<SegmentSet>& or_conditions
) const {
  for (const GlyphSet& gids : TRY(combined_patches_.NonIdentityGroups())) {
    for (glyph_id_t gid : gids) {
      const auto& cond = glyph_condition_set.ConditionsFor(gid);
      if (cond.and_segments.size() == 1) {
        exclusive_segments.insert(*cond.and_segments.begin());
      }
      if (!cond.or_segments.empty()) {
        or_conditions.insert(cond.or_segments);
      }
    }
  }
  return absl::OkStatus();
}

Status GlyphGroupings::ComputeConditionExpansionMap(
    const SegmentSet& exclusive_segments,
    const btree_set<SegmentSet>& or_conditions,
    flat_hash_map<glyph_id_t, SegmentSet>& merged_conditions,
    flat_hash_map<glyph_id_t, GlyphSet>& merged_glyphs) {


  // Form the complete partition incorporating combined_patches_ across all of the affected
  // groups. This complete partition specifies how groups will be merged together.
  GlyphPartition partition = combined_patches_;
  for (segment_index_t s : exclusive_segments) {
    TRYV(partition.Union(exclusive_glyph_groups_.at(s)));
  }

  for (const auto& segments : or_conditions) {
    TRYV(partition.Union(or_glyph_groups_.at(segments)));
  }

  // Each group can be mapped to a representative, where there is one representative for
  // each combined grouping. We can then collect up all of the combined segments and and
  // glyphs to each representative.
  //
  // During this processing we remove/add conditions as need. Where a existing group will
  // be combined, the uncombined condition is removed. Where a condition is not going to be
  // combined then the condition is added back. Adding back is needed in rare cases where
  // a condition was previously combined, but due to changes it no longer is. If the
  // condition is already present then addition is a noop.
  for (segment_index_t s : exclusive_segments) {
    const GlyphSet& gids = exclusive_glyph_groups_.at(s);
    std::optional<glyph_id_t> first = gids.min();
    if (!first.has_value()) {
      continue;
    }

    glyph_id_t rep = TRY(partition.Find(*first));
    if (gids != TRY(partition.GlyphsFor(rep))) {
      // Only record cases where merges happen, if the glyph set is unmodifed
      // then there will be no merge.
      merged_conditions[rep].insert(s);
      merged_glyphs[rep].union_set(gids);
      RemoveConditionAndGlyphs(ActivationCondition::exclusive_segment(s, 0));
      // Record s as having been removed via combination.
      combined_exclusive_segments_.insert(s);
    } else {
      AddConditionAndGlyphs(ActivationCondition::exclusive_segment(s, 0), gids);
    }
  }

  for (const auto& segments : or_conditions) {
    const GlyphSet& gids = or_glyph_groups_.at(segments);
    std::optional<glyph_id_t> first = gids.min();
    if (!first.has_value()) {
      continue;
    }

    glyph_id_t rep = TRY(partition.Find(*first));
    if (gids != TRY(partition.GlyphsFor(rep))) {
      merged_conditions[rep].union_set(segments);
      merged_glyphs[rep].union_set(gids);
      RemoveConditionAndGlyphs(ActivationCondition::or_segments(segments, 0));
    } else {
      AddConditionAndGlyphs(ActivationCondition::or_segments(segments, 0), gids);
    }
  }

  return absl::OkStatus();
}

}  // namespace ift::encoder
