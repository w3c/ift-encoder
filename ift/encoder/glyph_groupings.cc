#include "ift/encoder/glyph_groupings.h"

#include <optional>

#include "absl/container/btree_set.h"
#include "absl/log/log.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/complex_condition_finder.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/glyph_condition_set.h"
#include "ift/encoder/glyph_partition.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/types.h"

using ift::config::FIND_CONDITIONS;

using absl::btree_map;
using absl::btree_set;
using absl::flat_hash_map;
using absl::Status;
using absl::StatusOr;
using ift::common::GlyphSet;
using ift::common::SegmentSet;

namespace ift::encoder {

void GlyphGroupings::InvalidateGlyphInformation(uint32_t gid) {
  unmapped_glyphs_.erase(gid);
  found_condition_glyphs_.erase(gid);

  conditions_and_glyphs_.Invalidate(gid);
  std::optional<ActivationCondition> pre_combination_condition_or =
      conditions_and_glyphs_pre_combination_.Invalidate(gid);

  if (!pre_combination_condition_or.has_value()) {
    return;
  }

  ActivationCondition condition = *pre_combination_condition_or;

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

  if (!condition.IsPurelyDisjunctive()) {
    return;
  }

  const SegmentSet& s = condition.TriggeringSegments();
  auto entry = or_glyph_groups_.find(s);
  if (entry == or_glyph_groups_.end()) {
    return;
  }

  entry->second.erase(gid);
  if (entry->second.empty()) {
    or_glyph_groups_.erase(entry);
  }
}

void GlyphGroupings::RemoveAllCombinedConditions() {
  combined_patches_dirty_ = true;
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

  TRYV(conditions_and_glyphs_.Union(condition, glyphs));
  TRYV(conditions_and_glyphs_pre_combination_.Union(condition, glyphs));

  // When merging this way we have to check if any of the involved glyphs
  // are involved with the combined patches mechanism. If at least one is
  // then it's necessary to recompute all combined patches to reflect any
  // downstream changes.
  return RecomputeCombinedConditionsIfNeeded(glyphs);
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

  // The fallback patch isn't stored in ConditionAndGlyphs() so add it in
  // manually.
  SegmentSet fallback_segments;
  btree_map<ActivationCondition, GlyphSet> conditions_with_fallback;
  conditions_with_fallback.insert(ConditionsAndGlyphs().begin(), ConditionsAndGlyphs().end());
  if (!unmapped_glyphs_.empty()) {
    fallback_segments = segmentation_info.NonEmptySegments();

    ActivationCondition non_fallback_cond =
        ActivationCondition::or_segments(fallback_segments, 0, false);
    ActivationCondition fallback_cond =
        ActivationCondition::or_segments(fallback_segments, 0, true);

    if (conditions_with_fallback.contains(non_fallback_cond)) {
      // If an equivalent non-fallback condition exists merge it with the
      // fallback one.
      conditions_with_fallback[fallback_cond].union_set(
          conditions_with_fallback[non_fallback_cond]);
      conditions_with_fallback.erase(non_fallback_cond);
    }

    conditions_with_fallback[fallback_cond].union_set(unmapped_glyphs_);
  }

  TRYV(GlyphSegmentation::ConditionsToSegmentation(
      conditions_with_fallback, fallback_segments, segmentation));

  return segmentation;
}

Status GlyphGroupings::GroupGlyphs(
    const RequestedSegmentationInformation& segmentation_info,
    const GlyphConditionSet& glyph_condition_set,
    GlyphClosureCache& closure_cache,
    std::optional<DependencyClosure*> dependency_closure, GlyphSet glyphs,
    const SegmentSet& modified_segments, bool additional_conditions_check) {
  const auto& initial_closure = segmentation_info.InitFontGlyphs();
  SegmentSet inscope_fallback_segments;

  for (glyph_id_t gid : glyphs) {
    CollectSegments(gid, inscope_fallback_segments);
    InvalidateGlyphInformation(gid);
  }

  if (!inscope_fallback_segments.empty()) {
    inscope_fallback_segments.union_set(modified_segments);
  } else {
    // If no existing conditions exist, all segments are inscope.
    inscope_fallback_segments.invert();
  }

  // Find any additional glyphs that are affected by changes in
  // modified_segments
  GlyphSet additional_glyphs = ModifiedGlyphs(modified_segments);
  for (glyph_id_t gid : additional_glyphs) {
    CollectSegments(gid, inscope_fallback_segments);
    InvalidateGlyphInformation(gid);
  }
  glyphs.union_set(additional_glyphs);

  btree_set<SegmentSet> modified_or_groups;
  btree_set<ActivationCondition> all_other_modified_conditions;
  flat_hash_map<ActivationCondition, GlyphSet> new_groupings;

  for (glyph_id_t gid : glyphs) {
    const auto& condition = glyph_condition_set.ConditionsFor(gid).activation();

    if (condition.IsAlwaysTrue()) {
      if (!initial_closure.contains(gid) &&
          segmentation_info.FullClosure().contains(gid)) {
        // no condition so this is an unmapped glyph
        unmapped_glyphs_.insert(gid);
      }
      continue;
    }

    if (!new_groupings.contains(condition)) {
      // Since we may be doing a partial update, pull in any existing glyphs for
      // this particular condition
      auto it =
          conditions_and_glyphs_pre_combination_.ConditionsAndGlyphs().find(
              condition);
      if (it !=
          conditions_and_glyphs_pre_combination_.ConditionsAndGlyphs().end()) {
        new_groupings[condition] = it->second;
      }
    }
    new_groupings[condition].insert(gid);

    if (condition.IsExclusive()) {
      segment_index_t s = *condition.TriggeringSegments().begin();
      exclusive_glyph_groups_[s].insert(gid);
      all_other_modified_conditions.insert(condition);
    } else if (condition.IsPurelyDisjunctive()) {
      SegmentSet or_segments = condition.TriggeringSegments();
      or_glyph_groups_[or_segments].insert(gid);
      modified_or_groups.insert(or_segments);
    } else {
      // conjunctive or mixed condition
      all_other_modified_conditions.insert(condition);
    }
  }

  // Add conditions for everything except for purely disjunctive conditions whic
  // need additional processing.
  for (const auto& c : all_other_modified_conditions) {
    TRYV(AddConditionAndGlyphs(c, new_groupings.at(c)));
  }

  // Any of the or_set conditions we've generated may have some additional
  // conditions that were not detected. Therefore we need to rule out the
  // presence of these additional conditions if an or group is able to be
  // used.
  for (const auto& or_group : modified_or_groups) {
    auto& glyphs = or_glyph_groups_[or_group];
    ActivationCondition condition =
        ActivationCondition::or_segments(or_group, 0);

    if (!additional_conditions_check) {
      TRYV(AddConditionAndGlyphs(condition, glyphs));
      continue;
    }

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
                                     inscope_fallback_segments, closure_cache,
                                     dependency_closure));
  }

  // The combined conditions can't be incrementally updated, so we recompute
  // them in full if needed.
  TRYV(RecomputeCombinedConditionsIfNeeded(glyphs));

  // Note: we don't need to include the fallback segment/condition in
  //       conditions_and_glyphs since all downstream processing which
  //       utilizes that map ignores the fallback segment. It will be
  //       manually added to the final segmentation in ToGlyphSegmentation()

  return absl::OkStatus();
}

void GlyphGroupings::CollectSegments(glyph_id_t gid, SegmentSet& segments) {
  auto it = conditions_and_glyphs_.GlyphToCondition().find(gid);
  if (it == conditions_and_glyphs_.GlyphToCondition().end()) {
    return;
  }
  segments.union_set(it->second.TriggeringSegments());
}

GlyphSet GlyphGroupings::ModifiedGlyphs(const SegmentSet& segments) const {
  GlyphSet glyphs;
  for (segment_index_t s : segments) {
    const auto& conditions = TriggeringSegmentToConditions(s);
    for (const auto& c : conditions) {
      glyphs.union_set(ConditionsAndGlyphs().at(c));
    }
  }
  return glyphs;
}

Status GlyphGroupings::FindFallbackGlyphConditions(
    const RequestedSegmentationInformation& segmentation_info,
    const GlyphConditionSet& glyph_condition_set,
    const SegmentSet& inscope_segments, GlyphClosureCache& closure_cache,
    std::optional<DependencyClosure*> dependency_closure) {
  if (unmapped_glyphs_.empty()) {
    return absl::OkStatus();
  }

  SegmentSet inscope = SegmentSet::all();
  if (dependency_closure.has_value()) {
    inscope = TRY(
        dependency_closure.value()->SegmentsThatInteractWith(unmapped_glyphs_));
    VLOG(0) << "used dep graph to scope complex condition finding to "
            << inscope.size() << " segments.";
  }

  // Note: inscope_segments is not currently used, the approach needs more
  // work. In testing in some cases it caused complex conditions to be larger
  // than necessary when a segment which could shorten the condition isn't in
  // scope.
  //
  // For example, with true condition (a or b) AND (b or c), if only {a, c} is
  // inscope then we don't have the possibility of finding the superset of {b}.
  btree_map<SegmentSet, GlyphSet> complex_conditions =
      TRY(FindSupersetDisjunctiveConditionsFor(
          segmentation_info, glyph_condition_set, closure_cache,
          unmapped_glyphs_, inscope));

  found_condition_glyphs_.union_set(unmapped_glyphs_);
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
  // TODO XXXXX now that we can arbitrarily combine activation conditions,
  // we should be able to support patch merging on arbitrary conditions
  // including conjunctive and/or mixed ones. Rework this to not be limited just
  // to exclusive and purely disjunctive cases.
  //
  // Will also need to update the merger/candidate_merge code that selects patch
  // merges to consider all cases.

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

  combined_patches_dirty_ = false;
  return absl::OkStatus();
}

Status GlyphGroupings::ConditionsAffectedByCombination(
    SegmentSet& exclusive_segments,
    btree_set<SegmentSet>& or_conditions) const {
  for (const GlyphSet& gids : TRY(combined_patches_.NonIdentityGroups())) {
    for (glyph_id_t gid : gids) {
      const auto& cond =
          conditions_and_glyphs_pre_combination_.GlyphToCondition().at(gid);
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
