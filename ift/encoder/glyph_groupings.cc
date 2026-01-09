#include "ift/encoder/glyph_groupings.h"

#include <optional>

#include "absl/container/btree_set.h"
#include "common/int_set.h"
#include "common/try.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/complex_condition_finder.h"
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

void GlyphGroupings::InvalidateGlyphInformation(uint32_t gid) {
  unmapped_glyphs_.erase(gid);

  auto it = glyph_to_condition_.find(gid);
  if (it == glyph_to_condition_.end()) {
    return;
  }

  ActivationCondition condition = it->second;

  auto& glyphs = conditions_and_glyphs_.at(condition);
  glyphs.erase(gid);
  glyph_to_condition_.erase(gid);
  glyph_to_condition_pre_combination_.erase(gid);

  if (glyphs.empty()) {
    RemoveConditionAndGlyphs(condition);
  }

  if (condition.IsExclusive()) {
    segment_index_t s = *condition.TriggeringSegments().begin();
    auto it = exclusive_glyph_groups_.find(s);
    if (it == exclusive_glyph_groups_.end()) {
      return;
    }

    it->second.erase(gid);
    if (it->second.empty()) {
      exclusive_glyph_groups_.erase(it);
    }
    return;
  }

  btree_map<SegmentSet, GlyphSet>* groups = condition.conditions().size() == 1
                                                ? &or_glyph_groups_
                                                : &and_glyph_groups_;

  const SegmentSet& s = condition.TriggeringSegments();
  auto entry = groups->find(s);
  if (entry == groups->end()) {
    return;
  }

  entry->second.erase(gid);
  if (entry->second.empty()) {
    groups->erase(entry);
  }
}

void GlyphGroupings::RemoveAllCombinedConditions() {
  for (const auto& [segments, _] : combined_or_glyph_groups_) {
    RemoveConditionAndGlyphs(ActivationCondition::or_segments(segments, 0),
                             false);
  }
  combined_or_glyph_groups_.clear();
  combined_exclusive_segments_.clear();
}

Status GlyphGroupings::CombinePatches(const GlyphSet& a, const GlyphSet& b) {
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

Status GlyphGroupings::AddGlyphsToExclusiveGroup(
    segment_index_t exclusive_segment, const GlyphSet& glyphs) {
  for (glyph_id_t gid : glyphs) {
    InvalidateGlyphInformation(gid);
  }

  auto& exc_glyphs = exclusive_glyph_groups_[exclusive_segment];
  exc_glyphs.union_set(glyphs);

  ActivationCondition condition =
      ActivationCondition::exclusive_segment(exclusive_segment, 0);
  conditions_and_glyphs_[condition].union_set(glyphs);

  // Update indices to reflect the change.
  triggering_segment_to_conditions_[exclusive_segment].insert(condition);
  for (glyph_id_t gid : glyphs) {
    bool did_insert =
        glyph_to_condition_.insert(std::pair(gid, condition)).second;
    did_insert |=
        glyph_to_condition_pre_combination_.insert(std::pair(gid, condition))
            .second;
    if (!did_insert) {
      return absl::InternalError(
          "Attempting to add conflicting glyph to condition mapping.");
    }
  }

  // When merging this way we have to check if any of the involved glyphs
  // are involved with the combined patches mechanism. If at least one is
  // then it's necessary to recompute all combined patches to reflect any
  // downstream changes.
  for (glyph_id_t gid : glyphs) {
    if (TRY(combined_patches_.GlyphsFor(gid)).size() > 1) {
      RemoveAllCombinedConditions();
      TRYV(RecomputeCombinedConditions());
      break;
    }
  }

  return absl::OkStatus();
}

// Converts this grouping into a finalized GlyphSegmentation.
StatusOr<GlyphSegmentation> GlyphGroupings::ToGlyphSegmentation(
    const RequestedSegmentationInformation& segmentation_info) const {
  GlyphSegmentation segmentation(
      segmentation_info.InitFontSegmentWithoutDefaults(),
      segmentation_info.InitFontGlyphs(), unmapped_glyphs_);
  segmentation.CopySegments(segmentation_info.SegmentSubsetDefinitions());

  // Recreate the glyph groups based on ConditionsAndGlyphs() which reflects
  // the final state (including patch combinations).
  btree_map<SegmentSet, GlyphSet> and_glyph_groups;
  btree_map<SegmentSet, GlyphSet> or_glyph_groups;
  btree_map<segment_index_t, GlyphSet> exclusive_glyph_groups;
  for (const auto& [condition, glyphs] : ConditionsAndGlyphs()) {
    if (condition.IsExclusive()) {
      exclusive_glyph_groups[*condition.TriggeringSegments().begin()] = glyphs;
    } else if (condition.conditions().size() == 1) {
      SegmentSet segments = condition.TriggeringSegments();
      or_glyph_groups[segments] = glyphs;
    } else {
      SegmentSet segments = condition.TriggeringSegments();
      and_glyph_groups[segments] = glyphs;
    }
  }

  auto fallback = or_glyph_groups_.find(fallback_segments_);
  if (fallback != or_glyph_groups_.end()) {
    or_glyph_groups[fallback_segments_] = fallback->second;
  }

  TRYV(GlyphSegmentation::GroupsToSegmentation(
      and_glyph_groups, or_glyph_groups, exclusive_glyph_groups,
      fallback_segments_, segmentation));

  return segmentation;
}

Status GlyphGroupings::GroupGlyphs(
    const RequestedSegmentationInformation& segmentation_info,
    const GlyphConditionSet& glyph_condition_set,
    GlyphClosureCache& closure_cache, GlyphSet glyphs,
    const SegmentSet& modified_segments) {
  const auto& initial_closure = segmentation_info.InitFontGlyphs();

  for (glyph_id_t gid : glyphs) {
    InvalidateGlyphInformation(gid);
  }

  // Find any additional glyphs that are affected by changes in
  // modified_segments
  GlyphSet additional_glyphs = ModifiedGlyphs(modified_segments);
  for (glyph_id_t gid : additional_glyphs) {
    InvalidateGlyphInformation(gid);
  }
  glyphs.union_set(additional_glyphs);

  // TODO XXXX can we skip this is nothing that's beng changed intersects
  // a combined group.
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
    TRYV(AddConditionAndGlyphs(condition, exclusive_glyph_groups_[s]));
  }

  for (const auto& and_group : modified_and_groups) {
    auto condition = ActivationCondition::and_segments(and_group, 0);
    TRYV(AddConditionAndGlyphs(condition, and_glyph_groups_[and_group]));
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

    TRYV(AddConditionAndGlyphs(condition, glyphs));
  }

  if (segmentation_info.GetUnmappedGlyphHandling() == FIND_CONDITIONS) {
    TRYV(FindFallbackGlyphConditions(segmentation_info, glyph_condition_set,
                                     closure_cache));
  }

  // The combined conditions can't be incrementally updated, so we recompute
  // them in full.
  // TODO XXXX we should check if the modified glyph set intersects any
  // combination groups and avoid recomputing if it doesn't.
  TRYV(RecomputeCombinedConditions());

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

GlyphSet GlyphGroupings::ModifiedGlyphs(const SegmentSet& segments) const {
  GlyphSet glyphs;
  for (segment_index_t s : segments) {
    const auto& conditions = TriggeringSegmentToConditions(s);
    for (const auto& c : conditions) {
      glyphs.union_set(conditions_and_glyphs_.at(c));
    }
  }
  return glyphs;
}

Status GlyphGroupings::FindFallbackGlyphConditions(
    const RequestedSegmentationInformation& segmentation_info,
    const GlyphConditionSet& glyph_condition_set,
    GlyphClosureCache& closure_cache) {
  if (unmapped_glyphs_.empty()) {
    return absl::OkStatus();
  }

  btree_map<SegmentSet, GlyphSet> complex_conditions =
      TRY(FindSupersetDisjunctiveConditionsFor(
          segmentation_info, glyph_condition_set, closure_cache,
          unmapped_glyphs_));

  unmapped_glyphs_.clear();
  for (const auto& [s, g] : complex_conditions) {
    if (s.empty()) {
      return absl::InternalError("Complex conditions should never be empty.");
    }

    ActivationCondition c = ActivationCondition::or_segments(s, 0);
    if (s.size() == 1) {
      segment_index_t segment = *s.begin();
      exclusive_glyph_groups_[segment].union_set(g);
      c = ActivationCondition::exclusive_segment(segment, 0);
    } else {
      or_glyph_groups_[s].union_set(g);
    }

    // There may be existing glyphs at this specific condition, so union into
    // it.
    TRYV(UnionConditionAndGlyphs(c, g));
  }
  VLOG(0)
      << "Unmapped glyphs patch removed and replaced with found conditions.";

  return absl::OkStatus();
}

Status GlyphGroupings::RecomputeCombinedConditions() {
  // To minimize the amount of work we need to do we first detect which segments
  // are potentially affected by the patch combination mechanism and then limit
  // processing just to those.
  SegmentSet exclusive_segments;
  btree_set<SegmentSet> or_conditions;
  TRYV(ConditionsAffectedByCombination(exclusive_segments, or_conditions));

  flat_hash_map<glyph_id_t, SegmentSet> merged_conditions;
  flat_hash_map<glyph_id_t, GlyphSet> merged_glyphs;
  TRYV(ComputeConditionExpansionMap(exclusive_segments, or_conditions,
                                    merged_conditions, merged_glyphs));

  for (const auto& [rep, segments] : merged_conditions) {
    const GlyphSet& gids = merged_glyphs.at(rep);
    ActivationCondition condition =
        ActivationCondition::or_segments(segments, 0);
    if (segments.size() == 1 && exclusive_segments.contains(*segments.min())) {
      // This is actually an exclusive condition, and is not expanded.
      condition = ActivationCondition::exclusive_segment(*segments.min(), 0);
    } else {
      SegmentSet segments_copy = segments;
      combined_or_glyph_groups_[segments_copy] = gids;
    }

    TRYV(AddConditionAndGlyphs(condition, gids, false));
  }

  return absl::OkStatus();
}

Status GlyphGroupings::ConditionsAffectedByCombination(
    SegmentSet& exclusive_segments,
    btree_set<SegmentSet>& or_conditions) const {
  for (const GlyphSet& gids : TRY(combined_patches_.NonIdentityGroups())) {
    for (glyph_id_t gid : gids) {
      const auto& cond = glyph_to_condition_pre_combination_.at(gid);
      if (cond.IsExclusive()) {
        exclusive_segments.insert(*cond.TriggeringSegments().begin());
      } else if (cond.conditions().size() == 1) {
        or_conditions.insert(cond.TriggeringSegments());
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
  // Form the complete partition incorporating combined_patches_ across all of
  // the affected groups. This complete partition specifies how groups will be
  // merged together.
  GlyphPartition partition = combined_patches_;
  for (segment_index_t s : exclusive_segments) {
    TRYV(partition.Union(exclusive_glyph_groups_.at(s)));
  }

  for (const auto& segments : or_conditions) {
    TRYV(partition.Union(or_glyph_groups_.at(segments)));
  }

  // Each group can be mapped to a representative, where there is one
  // representative for each combined grouping. We can then collect up all of
  // the combined segments and and glyphs to each representative.
  //
  // During this processing we remove/add conditions as need. Where a existing
  // group will be combined, the uncombined condition is removed. Where a
  // condition is not going to be combined then the condition is added back.
  // Adding back is needed in rare cases where a condition was previously
  // combined, but due to changes it no longer is. If the condition is already
  // present then addition is a noop.
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
      RemoveConditionAndGlyphs(ActivationCondition::exclusive_segment(s, 0),
                               false);
      // Record s as having been removed via combination.
      combined_exclusive_segments_.insert(s);
    } else {
      TRYV(AddConditionAndGlyphs(ActivationCondition::exclusive_segment(s, 0),
                                 gids, false));
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
      RemoveConditionAndGlyphs(ActivationCondition::or_segments(segments, 0),
                               false);
    } else {
      TRYV(AddConditionAndGlyphs(ActivationCondition::or_segments(segments, 0),
                                 gids, false));
    }
  }

  return absl::OkStatus();
}

}  // namespace ift::encoder
