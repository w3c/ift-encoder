#include "ift/encoder/glyph_groupings.h"

#include <optional>

#include "absl/container/btree_set.h"
#include "absl/log/log.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/glyph_condition_set.h"
#include "ift/encoder/glyph_partition.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/types.h"

using absl::btree_map;
using absl::btree_set;
using absl::flat_hash_map;
using absl::Status;
using absl::StatusOr;
using ift::common::GlyphSet;
using ift::common::SegmentSet;

namespace ift::encoder {

void GlyphGroupings::InvalidateGlyphInformation(uint32_t gid) {
  std::optional<ActivationCondition> post_combination_condition_or =
      conditions_and_glyphs_.Invalidate(gid);
  std::optional<ActivationCondition> pre_combination_condition_or =
      conditions_and_glyphs_pre_combination_.Invalidate(gid);

  if (post_combination_condition_or.has_value() &&
      combined_conditions_.contains(*post_combination_condition_or)) {
    RemoveAllCombinedConditions();
  }

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
  for (const auto& condition : combined_conditions_) {
    RemoveConditionAndGlyphs(condition, false);
  }
  combined_conditions_.clear();
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

// Converts this grouping into a finalized GlyphSegmentation.
StatusOr<GlyphSegmentation> GlyphGroupings::ToGlyphSegmentation(
    const RequestedSegmentationInformation& segmentation_info,
    bool exclusive_only) const {
  GlyphSegmentation segmentation(
      segmentation_info.InitFontSegmentWithoutDefaults(),
      segmentation_info.InitFontGlyphs());
  segmentation.CopySegments(segmentation_info.SegmentSubsetDefinitions());

  btree_map<ActivationCondition, GlyphSet> filtered_conditions;
  for (const auto& [condition, glyphs] : ConditionsAndGlyphs()) {
    if (exclusive_only && !condition.IsExclusive()) {
      continue;
    }
    filtered_conditions.insert({condition, glyphs});
  }

  TRYV(GlyphSegmentation::ConditionsToSegmentation(filtered_conditions,
                                                   segmentation));

  return segmentation;
}

Status GlyphGroupings::GroupGlyphs(
    const RequestedSegmentationInformation& segmentation_info,
    const GlyphConditionSet& glyph_condition_set, GlyphSet glyphs,
    const SegmentSet& modified_segments) {
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

  btree_set<SegmentSet> modified_or_groups;
  btree_set<ActivationCondition> all_other_modified_conditions;
  flat_hash_map<ActivationCondition, GlyphSet> new_groupings;

  for (glyph_id_t gid : glyphs) {
    const auto& condition = glyph_condition_set.ConditionsFor(gid).activation();

    if (condition.IsAlwaysTrue()) {
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

  // Add conditions for everything except for purely disjunctive conditions.
  for (const auto& c : all_other_modified_conditions) {
    TRYV(AddConditionAndGlyphs(c, new_groupings.at(c)));
  }

  for (const auto& or_group : modified_or_groups) {
    auto& glyphs = or_glyph_groups_[or_group];
    ActivationCondition condition =
        ActivationCondition::or_segments(or_group, 0);
    TRYV(AddConditionAndGlyphs(condition, glyphs));
  }

  // The combined conditions can't be incrementally updated, so we recompute
  // them in full if needed.
  TRYV(RecomputeCombinedConditionsIfNeeded(glyphs));

  return absl::OkStatus();
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

Status GlyphGroupings::RecomputeCombinedConditions() {
  // Find all conditions that are affected by combined_patches_.
  btree_set<ActivationCondition> affected_conditions;
  for (const GlyphSet& gids : TRY(combined_patches_.NonIdentityGroups())) {
    for (glyph_id_t gid : gids) {
      auto it =
          conditions_and_glyphs_pre_combination_.GlyphToCondition().find(gid);
      if (it !=
          conditions_and_glyphs_pre_combination_.GlyphToCondition().end()) {
        affected_conditions.insert(it->second);
      }
    }
  }

  if (affected_conditions.empty()) {
    combined_patches_dirty_ = false;
    return absl::OkStatus();
  }

  // Form the complete partition incorporating combined_patches_ across all of
  // the affected groups. This complete partition specifies how groups will be
  // merged together.
  GlyphPartition partition = combined_patches_;
  for (const auto& cond : affected_conditions) {
    const GlyphSet& gids =
        conditions_and_glyphs_pre_combination_.ConditionsAndGlyphs().at(cond);
    TRYV(partition.Union(gids));
  }

  // Map representative to combined condition and glyphs.
  flat_hash_map<glyph_id_t, ActivationCondition> merged_conditions;
  flat_hash_map<glyph_id_t, GlyphSet> merged_glyphs;

  for (const auto& cond : affected_conditions) {
    const GlyphSet& gids =
        conditions_and_glyphs_pre_combination_.ConditionsAndGlyphs().at(cond);
    std::optional<glyph_id_t> first = gids.min();
    if (!first.has_value()) {
      continue;
    }

    glyph_id_t rep = TRY(partition.Find(*first));

    if (gids != TRY(partition.GlyphsFor(rep))) {
      // Only record cases where merges happen.
      RemoveConditionAndGlyphs(cond, false);

      if (cond.IsExclusive()) {
        combined_exclusive_segments_.insert(*cond.TriggeringSegments().begin());
      }

      auto [it, inserted] = merged_conditions.insert({rep, cond});
      if (!inserted) {
        it->second = ActivationCondition::Or(it->second, cond);
      }
      merged_glyphs[rep].union_set(gids);
    } else {
      TRYV(AddConditionAndGlyphs(cond, gids, false));
    }
  }

  if (simplify_combined_) {
    for (auto& cond : merged_conditions) {
      cond.second = cond.second.NonCompositeSuperset();
    }
  }

  // Add the new combined conditions.
  for (const auto& [rep, condition] : merged_conditions) {
    const GlyphSet& gids = merged_glyphs.at(rep);
    // Union is used here because the merged condition may collide
    // with an existing patch that has the same condition.
    TRYV(UnionConditionAndGlyphs(condition, gids, false));
    combined_conditions_.insert(condition);

    // There may be precombination conditions that happen to have the same
    // condition as this merged conditions. We need to also pull those glyphs
    // into the final combined condition mapping as well if they are not,
    // yet included via the combination analysis.
    auto it = conditions_and_glyphs_pre_combination_.ConditionsAndGlyphs().find(
        condition);
    if (it != conditions_and_glyphs_pre_combination_.ConditionsAndGlyphs()
                  .end() &&
        !affected_conditions.contains(condition)) {
      TRYV(UnionConditionAndGlyphs(condition, it->second, false));
    }
  }

  combined_patches_dirty_ = false;
  return absl::OkStatus();
}

}  // namespace ift::encoder
