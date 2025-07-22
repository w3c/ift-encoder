#include "ift/encoder/glyph_groupings.h"

#include "absl/container/btree_set.h"
#include "common/int_set.h"
#include "common/try.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/glyph_condition_set.h"
#include "ift/encoder/requested_segmentation_information.h"

using absl::btree_set;
using absl::Status;
using common::GlyphSet;
using common::SegmentSet;

namespace ift::encoder {

void GlyphGroupings::InvalidateGlyphInformation(
    const GlyphConditions& condition, uint32_t gid) {
  auto it = and_glyph_groups_.find(condition.and_segments);
  if (it != and_glyph_groups_.end()) {
    it->second.erase(gid);
    ActivationCondition activation_condition =
        ActivationCondition::and_segments(condition.and_segments, 0);
    if (condition.and_segments.size() == 1) {
      activation_condition = ActivationCondition::exclusive_segment(
          *condition.and_segments.begin(), 0);
    }
    conditions_and_glyphs_[activation_condition].erase(gid);

    if (it->second.empty()) {
      and_glyph_groups_.erase(it);
      RemoveConditionAndGlyphs(activation_condition);
    }
  }

  it = or_glyph_groups_.find(condition.or_segments);
  if (it != or_glyph_groups_.end()) {
    it->second.erase(gid);
    ActivationCondition activation_condition =
        ActivationCondition::or_segments(condition.or_segments, 0);
    conditions_and_glyphs_[activation_condition].erase(gid);

    if (it->second.empty()) {
      or_glyph_groups_.erase(it);
      RemoveConditionAndGlyphs(activation_condition);
    }
  }

  unmapped_glyphs_.erase(gid);
}

Status GlyphGroupings::GroupGlyphs(
    const RequestedSegmentationInformation& segmentation_info,
    const GlyphConditionSet& glyph_condition_set,
    GlyphClosureCache& closure_cache, const GlyphSet& glyphs) {
  const auto& initial_closure = segmentation_info.InitFontGlyphs();

  btree_set<SegmentSet> modified_and_groups;
  btree_set<SegmentSet> modified_or_groups;
  for (glyph_id_t gid : glyphs) {
    const auto& condition = glyph_condition_set.ConditionsFor(gid);

    if (!condition.and_segments.empty()) {
      and_glyph_groups_[condition.and_segments].insert(gid);
      modified_and_groups.insert(condition.and_segments);
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

  for (const auto& and_group : modified_and_groups) {
    if (and_group.size() == 1) {
      auto condition =
          ActivationCondition::exclusive_segment(*and_group.begin(), 0);
      AddConditionAndGlyphs(condition, and_glyph_groups_[and_group]);
      continue;
    }

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

}  // namespace ift::encoder