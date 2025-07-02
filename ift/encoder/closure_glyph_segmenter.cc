#include "ift/encoder/closure_glyph_segmenter.h"

#include <cstdint>
#include <optional>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/compat_id.h"
#include "common/font_data.h"
#include "common/font_helper.h"
#include "common/hb_set_unique_ptr.h"
#include "common/int_set.h"
#include "common/try.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/subset_definition.h"
#include "ift/glyph_keyed_diff.h"

using absl::btree_map;
using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::node_hash_map;
using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using common::CodepointSet;
using common::CompatId;
using common::FontData;
using common::FontHelper;
using common::GlyphSet;
using common::hb_face_unique_ptr;
using common::hb_set_unique_ptr;
using common::IntSet;
using common::make_hb_face;
using common::make_hb_set;
using common::SegmentSet;
using ift::GlyphKeyedDiff;

namespace ift::encoder {

// An indepth description of how this segmentation implementation works can
// be found in ../../docs/closure_glyph_segmentation.md.

// TODO(garretrieger): extensions/improvements that could be made:
// - Can we reduce # of closures for the additional conditions checks?
//   - is the full analysis needed to get the or set?
// - Use merging and/or duplication to ensure minimum patch size.
//   - composite patches (NOT STARTED)
// - Multi segment combination testing with GSUB dep analysis to guide.

class RequestedSegmentationInformation;
class GlyphClosureCache;

Status AnalyzeSegment(const RequestedSegmentationInformation& segmentation_info,
                      GlyphClosureCache& closure_cache,
                      const SegmentSet& segment_ids, GlyphSet& and_gids,
                      GlyphSet& or_gids, GlyphSet& exclusive_gids);

/*
 * A cache of the results of glyph closure on a specific font face.
 */
class GlyphClosureCache {
 public:
  GlyphClosureCache(hb_face_t* face)
      : preprocessed_face_(make_hb_face(hb_subset_preprocess(face))),
        original_face_(make_hb_face(hb_face_reference(face))) {}

  StatusOr<GlyphSet> GlyphClosure(const SubsetDefinition& segment) {
    auto it = glyph_closure_cache_.find(segment);
    if (it != glyph_closure_cache_.end()) {
      glyph_closure_cache_hit_++;
      return it->second;
    }

    glyph_closure_cache_miss_++;
    closure_count_cumulative_++;
    closure_count_delta_++;

    hb_subset_input_t* input = hb_subset_input_create_or_fail();
    if (!input) {
      return absl::InternalError("Closure subset configuration failed.");
    }

    // TODO(garretrieger): automatically include IFT default features in input
    // config.
    segment.ConfigureInput(input, preprocessed_face_.get());

    hb_subset_plan_t* plan =
        hb_subset_plan_create_or_fail(preprocessed_face_.get(), input);
    hb_subset_input_destroy(input);
    if (!plan) {
      return absl::InternalError("Closure calculation failed.");
    }

    hb_map_t* new_to_old = hb_subset_plan_new_to_old_glyph_mapping(plan);
    hb_set_unique_ptr gids = make_hb_set();
    hb_map_values(new_to_old, gids.get());
    hb_subset_plan_destroy(plan);

    glyph_closure_cache_.insert(std::pair(segment, GlyphSet(gids)));

    return GlyphSet(gids);
  }

  StatusOr<GlyphSet> CodepointsToOrGids(
      const RequestedSegmentationInformation& segmentation_info,
      const SegmentSet& segment_ids) {
    GlyphSet and_gids;
    GlyphSet or_gids;
    GlyphSet exclusive_gids;
    TRYV(AnalyzeSegment(segmentation_info, *this, segment_ids, and_gids,
                        or_gids, exclusive_gids));

    return or_gids;
  }

  void LogCacheStats() const {
    double closure_hit_rate =
        100.0 * ((double)glyph_closure_cache_hit_) /
        ((double)(glyph_closure_cache_hit_ + glyph_closure_cache_miss_));
    VLOG(0) << "Glyph closure cache hit rate: " << closure_hit_rate << "% ("
            << glyph_closure_cache_hit_ << " hits, "
            << glyph_closure_cache_miss_ << " misses)";
  }

  void LogClosureCount(absl::string_view operation) {
    VLOG(0) << operation << ": cumulative number of glyph closures "
            << closure_count_cumulative_ << " (+" << closure_count_delta_
            << ")";
    closure_count_delta_ = 0;
  }

 private:
  hb_face_unique_ptr preprocessed_face_;
  hb_face_unique_ptr original_face_;
  flat_hash_map<SubsetDefinition, GlyphSet> glyph_closure_cache_;
  uint32_t glyph_closure_cache_hit_ = 0;
  uint32_t glyph_closure_cache_miss_ = 0;
  uint32_t closure_count_cumulative_ = 0;
  uint32_t closure_count_delta_ = 0;
};

/*
 * Stores basic information about the configuration of a requested segmentation
 */
class RequestedSegmentationInformation {
 public:
  RequestedSegmentationInformation(std::vector<SubsetDefinition> segments,
                                   SubsetDefinition init_font_segment,
                                   GlyphClosureCache& closure_cache)
      : segments_(std::move(segments)),
        init_font_segment_(std::move(init_font_segment)) {
    SubsetDefinition all;
    all.Union(init_font_segment_);
    for (const auto& s : segments_) {
      all.Union(s);
    }

    {
      auto closure = closure_cache.GlyphClosure(init_font_segment_);
      if (closure.ok()) {
        init_font_glyphs_ = std::move(*closure);
      }
    }

    auto closure = closure_cache.GlyphClosure(all);
    if (closure.ok()) {
      full_closure_ = std::move(*closure);
    }
  }

  uint32_t MergeSegments(segment_index_t base, const SegmentSet& to_merge,
                         const SubsetDefinition& merged_segment) {
    segments_[base].Union(merged_segment);
    for (segment_index_t s : to_merge) {
      // To avoid changing the indices of other segments set the ones we're
      // removing to empty sets. That effectively disables them.
      segments_[s].Clear();
    }
    return segments_[base].codepoints.size();
  }

  const SubsetDefinition& InitFontSegment() const { return init_font_segment_; }

  const GlyphSet& InitFontGlyphs() const { return init_font_glyphs_; }

  const GlyphSet& FullClosure() const { return full_closure_; }

  const std::vector<SubsetDefinition>& Segments() const { return segments_; }

 private:
  std::vector<SubsetDefinition> segments_;
  SubsetDefinition init_font_segment_;
  GlyphSet init_font_glyphs_;
  GlyphSet full_closure_;
};

/*
 * A set of conditions which activate a specific single glyph.
 */
class GlyphConditions {
 public:
  GlyphConditions() : and_segments(), or_segments() {}
  SegmentSet and_segments;
  SegmentSet or_segments;

  bool operator==(const GlyphConditions& other) const {
    return other.and_segments == and_segments &&
           other.or_segments == or_segments;
  }

  void RemoveSegments(const SegmentSet& segments) {
    and_segments.subtract(segments);
    or_segments.subtract(segments);
  }
};

/*
 * Collection of per glyph conditions for all glyphs in a font.
 */
class GlyphConditionSet {
 public:
  GlyphConditionSet(uint32_t num_glyphs) { gid_conditions_.resize(num_glyphs); }

  const GlyphConditions& ConditionsFor(glyph_id_t gid) const {
    return gid_conditions_[gid];
  }

  void AddAndCondition(glyph_id_t gid, segment_index_t segment) {
    gid_conditions_[gid].and_segments.insert(segment);
    segment_to_gid_conditions_[segment].insert(gid);
  }

  void AddOrCondition(glyph_id_t gid, segment_index_t segment) {
    gid_conditions_[gid].or_segments.insert(segment);
    segment_to_gid_conditions_[segment].insert(gid);
  }

  // Returns the set of glyphs that have 'segment' in their conditions.
  const GlyphSet& GlyphsWithSegment(segment_index_t segment) const {
    const static GlyphSet empty;
    auto it = segment_to_gid_conditions_.find(segment);
    if (it == segment_to_gid_conditions_.end()) {
      return empty;
    }
    return it->second;
  }

  // Clears out any stored information for glyphs and segments in this condition
  // set.
  void InvalidateGlyphInformation(const GlyphSet& glyphs,
                                  const SegmentSet& segments) {
    // Remove all segments we touched here from gid_conditions so they can be
    // recalculated.
    for (uint32_t gid : glyphs) {
      gid_conditions_[gid].RemoveSegments(segments);
    }

    for (uint32_t segment_index : segments) {
      segment_to_gid_conditions_[segment_index].subtract(glyphs);
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
  // Index in this vector is the glyph id associated with the condition at that
  // index.
  std::vector<GlyphConditions> gid_conditions_;

  // Index that tracks for each segment id which set of glyphs include that
  // segment in it's conditions.
  flat_hash_map<uint32_t, GlyphSet> segment_to_gid_conditions_;
};

/*
 * Grouping of the glyphs in a font by the associated activation conditions.
 */
class GlyphGroupings {
 public:
  GlyphGroupings(const std::vector<SubsetDefinition>& segments) {
    uint32_t index = 0;
    for (const auto& s : segments) {
      if (!s.Empty()) {
        fallback_segments_.insert(index);
      }
      index++;
    }
  }

  const btree_map<ActivationCondition, GlyphSet>& ConditionsAndGlyphs() const {
    return conditions_and_glyphs_;
  }

  const btree_map<SegmentSet, GlyphSet>& AndGlyphGroups() const {
    return and_glyph_groups_;
  }

  const btree_map<SegmentSet, GlyphSet>& OrGlyphGroups() const {
    return or_glyph_groups_;
  }

  // Returns a list of conditions which include segment.
  const btree_set<ActivationCondition>& TriggeringSegmentToConditions(
      segment_index_t segment) const {
    static btree_set<ActivationCondition> empty;
    auto it = triggering_segment_to_conditions_.find(segment);
    if (it != triggering_segment_to_conditions_.end()) {
      return it->second;
    }
    return empty;
  }

  // Removes all stored grouping information related to glyph with the specified
  // condition.
  void InvalidateGlyphInformation(const GlyphConditions& condition,
                                  uint32_t gid) {
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

  // Remove a set of segments from the fallback segments set.
  // Invalidates any existing fallback segments or glyph group.
  void RemoveFallbackSegments(const SegmentSet& removed_segments) {
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
                                 const GlyphSet& glyphs) {
    auto& and_glyphs = and_glyph_groups_[SegmentSet{exclusive_segment}];
    and_glyphs.union_set(glyphs);

    ActivationCondition condition =
        ActivationCondition::exclusive_segment(exclusive_segment, 0);
    conditions_and_glyphs_[condition].union_set(glyphs);
    // triggering segment to conditions is not affected by this change, so
    // doesn't need an update.
  }

  // Updates this glyph grouping for all glyphs in the 'glyphs' set to match
  // the associated conditions in 'glyph_condition_set'.
  Status GroupGlyphs(const RequestedSegmentationInformation& segmentation_info,
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

  // Converts this grouping into a finalized GlyphSegmentation.
  StatusOr<GlyphSegmentation> ToGlyphSegmentation(
      const RequestedSegmentationInformation& segmentation_info) const {
    GlyphSegmentation segmentation(segmentation_info.InitFontSegment(),
                                   segmentation_info.InitFontGlyphs(),
                                   unmapped_glyphs_);
    segmentation.CopySegments(segmentation_info.Segments());
    TRYV(GlyphSegmentation::GroupsToSegmentation(
        and_glyph_groups_, or_glyph_groups_, fallback_segments_, segmentation));
    return segmentation;
  }

 private:
  void AddConditionAndGlyphs(ActivationCondition condition, GlyphSet glyphs) {
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

  btree_map<SegmentSet, GlyphSet> and_glyph_groups_;
  btree_map<SegmentSet, GlyphSet> or_glyph_groups_;

  // An alternate representation of and/or_glyph_groups_, derived from them.
  btree_map<ActivationCondition, GlyphSet> conditions_and_glyphs_;

  // Index that maps segments to all conditions in conditions_and_glyphs_ which
  // reference that segment.
  flat_hash_map<uint32_t, btree_set<ActivationCondition>>
      triggering_segment_to_conditions_;

  // Set of segments in the fallback condition.
  SegmentSet fallback_segments_;

  // These glyphs aren't mapped by any conditions and as a result should be
  // included in the fallback patch.
  common::GlyphSet unmapped_glyphs_;
};

// Stores all of the information used during generating of a glyph segmentation.
//
// The following high level information is stored:
// 1. requested segmentation: the input segmentation in terms of codepoints.
// 2. glyph closure cache: helper for computing glyph closures that caches the
// results.
// 3. glyph condition set: per glyph what conditions activate that glyph.
// 4. glyph groupings: glyphs grouped by activation conditions.
//
// Information flows through these items:
// 1. Generated from the input.
// 3. Generated based on #1.
// 4. Generated based on #1 and #3.
//
// These pieces all support incremental update. For example if 1. is updated we
// can incrementally update the down stream items 3. and 4. Only needing to
// recompute the parts that change as a result of the changes in 1.
class SegmentationContext {
 public:
  SegmentationContext(hb_face_t* face, const SubsetDefinition& initial_segment,
                      const std::vector<SubsetDefinition>& segments)
      : glyph_closure_cache(face),
        original_face(make_hb_face(hb_face_reference(face))),
        segmentation_info(segments, initial_segment, glyph_closure_cache),
        glyph_condition_set(hb_face_get_glyph_count(face)),
        glyph_groupings(segments) {}

  /*
   * Ensures that the produce segmentation is:
   * - Disjoint (no duplicated glyphs) and doesn't overlap what's in the initial
   * font.
   * - Fully covers the full closure.
   */
  Status ValidateSegmentation(const GlyphSegmentation& segmentation) const {
    IntSet visited;
    const auto& initial_closure = segmentation.InitialFontGlyphs();
    for (const auto& [id, gids] : segmentation.GidSegments()) {
      for (glyph_id_t gid : gids) {
        if (initial_closure.contains(gid)) {
          return absl::FailedPreconditionError(
              "Initial font glyph is present in a patch.");
        }
        if (visited.contains(gid)) {
          return absl::FailedPreconditionError(
              "Glyph segments are not disjoint.");
        }
        visited.insert(gid);
      }
    }

    IntSet full_minus_initial = segmentation_info.FullClosure();
    full_minus_initial.subtract(initial_closure);

    if (full_minus_initial != visited) {
      return absl::FailedPreconditionError(
          "Not all glyphs in the full closure have been placed.");
    }

    return absl::OkStatus();
  }

  // Convert the information in this context into a finalized GlyphSegmentation
  // representation.
  StatusOr<GlyphSegmentation> ToGlyphSegmentation() const {
    GlyphSegmentation segmentation =
        TRY(glyph_groupings.ToGlyphSegmentation(segmentation_info));
    glyph_closure_cache.LogCacheStats();
    TRYV(ValidateSegmentation(segmentation));
    return segmentation;
  }

  /*
   * Removes all condition and grouping information related to all gids in
   * glyphs.
   */
  void InvalidateGlyphInformation(const GlyphSet& glyphs,
                                  const SegmentSet& segments) {
    // unmapped, and and/or glyph groups are down stream of glyph conditions
    // so must be invalidated. Do this before modifying the conditions.
    for (uint32_t gid : glyphs) {
      const auto& condition = glyph_condition_set.ConditionsFor(gid);
      glyph_groupings.InvalidateGlyphInformation(condition, gid);
    }

    glyph_condition_set.InvalidateGlyphInformation(glyphs, segments);
  }

  // Performs a closure analysis on codepoints and returns the associated
  // and, or, and exclusive glyph sets.
  Status AnalyzeSegment(const SegmentSet& segment_ids, GlyphSet& and_gids,
                        GlyphSet& or_gids, GlyphSet& exclusive_gids) {
    return ::ift::encoder::AnalyzeSegment(segmentation_info,
                                          glyph_closure_cache, segment_ids,
                                          and_gids, or_gids, exclusive_gids);
  }

  // Generates updated glyph conditions and glyph groupings for segment_index
  // which has the provided set of codepoints.
  StatusOr<GlyphSet> ReprocessSegment(segment_index_t segment_index) {
    GlyphSet and_gids;
    GlyphSet or_gids;
    GlyphSet exclusive_gids;
    TRYV(ift::encoder::AnalyzeSegment(segmentation_info, glyph_closure_cache,
                                      {segment_index}, and_gids, or_gids,
                                      exclusive_gids));

    GlyphSet changed_gids;
    changed_gids.union_set(and_gids);
    changed_gids.union_set(or_gids);
    changed_gids.union_set(exclusive_gids);
    for (uint32_t gid : changed_gids) {
      InvalidateGlyphInformation(GlyphSet{gid}, SegmentSet{segment_index});
    }

    if (and_gids.empty() && or_gids.empty()) {
      inert_segments.insert(segment_index);
    }

    for (uint32_t and_gid : exclusive_gids) {
      // TODO(garretrieger): if we are assigning an exclusive gid there should
      // be no other and segments, check and error if this is violated.
      glyph_condition_set.AddAndCondition(and_gid, segment_index);
    }

    for (uint32_t and_gid : and_gids) {
      glyph_condition_set.AddAndCondition(and_gid, segment_index);
    }

    for (uint32_t or_gid : or_gids) {
      glyph_condition_set.AddOrCondition(or_gid, segment_index);
    }

    return changed_gids;
  }

  // Update the glyph groups for 'glyphs'.
  //
  // The glyph condition set must be up to date and fully computed prior to
  // calling this.
  Status GroupGlyphs(const GlyphSet& glyphs) {
    return glyph_groupings.GroupGlyphs(segmentation_info, glyph_condition_set,
                                       glyph_closure_cache, glyphs);
  }

  // Caches and logging
  GlyphClosureCache glyph_closure_cache;

  // Init
  hb_face_unique_ptr original_face;
  RequestedSegmentationInformation segmentation_info;

  uint32_t patch_size_min_bytes = 0;
  uint32_t patch_size_max_bytes = UINT32_MAX;

  // Phase 1 - derived from segments and init information
  GlyphConditionSet glyph_condition_set;
  SegmentSet inert_segments;

  // Phase 2 - derived from glyph_condition_set and init information.
  GlyphGroupings glyph_groupings;
};

Status AnalyzeSegment(const RequestedSegmentationInformation& segmentation_info,
                      GlyphClosureCache& closure_cache,
                      const SegmentSet& segment_ids, GlyphSet& and_gids,
                      GlyphSet& or_gids, GlyphSet& exclusive_gids) {
  if (segment_ids.empty()) {
    return absl::OkStatus();
  }

  // This function tests various closures using the segment codepoints to
  // determine what conditions are present for the inclusion of closure glyphs.
  //
  // At a high level we do the following (where s_i is the segment being
  // tested):
  //
  // * Set A: glyph closure on original font of the union of all segments.
  // * Set B: glyph closure on original font of the union of all segments except
  //          for s_i
  // * Set I: (glyph closure on original font of s_0 union s_i) - (glyph closure
  //           on original font of s_0)
  // * Set D: A - B, the set of glyphs that are dropped when s_i is removed.
  //
  // Then we know the following:
  // * Glyphs in I should be included whenever s_i is activated.
  // * s_i is necessary for glyphs in D to be required, but other segments may
  //   be needed too.
  //
  // Furthermore we can intersect I and D to produce three sets:
  // * D - I: the activation condition for these glyphs is s_i AND …
  //          Where … is one or more additional segments.
  // * I - D: the activation conditions for these glyphs is s_i OR …
  //          Where … is one or more additional segments.
  // * D intersection I: the activation conditions for these glyphs is only s_i
  SubsetDefinition except_segment = segmentation_info.InitFontSegment();
  for (uint32_t s = 0; s < segmentation_info.Segments().size(); s++) {
    if (segment_ids.contains(s)) {
      continue;
    }
    except_segment.Union(segmentation_info.Segments()[s]);
  }

  auto B_except_segment_closure =
      TRY(closure_cache.GlyphClosure(except_segment));

  SubsetDefinition only_segment = segmentation_info.InitFontSegment();
  for (segment_index_t s_id : segment_ids) {
    only_segment.Union(segmentation_info.Segments()[s_id]);
  }

  auto I_only_segment_closure = TRY(closure_cache.GlyphClosure(only_segment));
  I_only_segment_closure.subtract(segmentation_info.InitFontGlyphs());

  GlyphSet D_dropped = segmentation_info.FullClosure();
  D_dropped.subtract(B_except_segment_closure);

  and_gids.union_set(D_dropped);
  and_gids.subtract(I_only_segment_closure);

  or_gids.union_set(I_only_segment_closure);
  or_gids.subtract(D_dropped);

  exclusive_gids.union_set(I_only_segment_closure);
  exclusive_gids.intersect(D_dropped);

  return absl::OkStatus();
}

// Calculates the estimated size of a patch for original_face which includes
// 'gids'.
//
// Will typically overestimate the size since we use a faster, but less
// effective version of brotli (quality 9 instead of 11) to generate the
// estimate.
StatusOr<uint32_t> EstimatePatchSizeBytes(hb_face_t* original_face,
                                          const GlyphSet& gids) {
  FontData font_data(original_face);
  CompatId id;
  // Since this is just an estimate and we don't need ultra precise numbers run
  // at a lower brotli quality to improve performance.
  GlyphKeyedDiff diff(font_data, id,
                      {FontHelper::kGlyf, FontHelper::kGvar, FontHelper::kCFF,
                       FontHelper::kCFF2},
                      8);

  auto patch_data = TRY(diff.CreatePatch(gids));
  return patch_data.size();
}

void MergeSegments(const RequestedSegmentationInformation& segmentation_info,
                   const SegmentSet& segments, SubsetDefinition& base) {
  for (segment_index_t next : segments) {
    base.Union(segmentation_info.Segments()[next]);
  }
}

// Calculates the estimated size of a patch which includes all glyphs exclusive
// to the listed codepoints. Will typically overestimate the size since we use
// a faster, but less effective version of brotli to generate the estimate.
StatusOr<uint32_t> EstimatePatchSizeBytes(SegmentationContext& context,
                                          const SegmentSet& segment_ids) {
  GlyphSet and_gids;
  GlyphSet or_gids;
  GlyphSet exclusive_gids;
  TRYV(context.AnalyzeSegment(segment_ids, and_gids, or_gids, exclusive_gids));
  return EstimatePatchSizeBytes(context.original_face.get(), exclusive_gids);
}

// Check a proposed merger to see if it would result in mixing codepoint only
// and feature only segments.
bool WouldMixFeaturesAndCodepoints(
    const RequestedSegmentationInformation& segment_info,
    segment_index_t base_segment_index, const SegmentSet& segments) {
  const auto& base = segment_info.Segments()[base_segment_index];
  bool base_codepoints_only =
      !base.codepoints.empty() && base.feature_tags.empty();
  bool base_features_only =
      base.codepoints.empty() && !base.feature_tags.empty();

  if (!base_codepoints_only && !base_features_only) {
    return false;
  }

  for (segment_index_t id : segments) {
    const auto& s = segment_info.Segments()[id];

    if (base_codepoints_only && !s.feature_tags.empty()) {
      return true;
    } else if (base_features_only && !s.codepoints.empty()) {
      return true;
    }
  }

  return false;
}

// Attempt to merge to_merge_semgents into base_segment_index. If maximum
// patch size would be exceeded does not merge and returns nullopt.
//
// Otherwise the segment definitions are merged and any affected downstream info
// (glyph conditions and glyph groupings) are invalidated. The set of
// invalidated glyph ids is returned.
StatusOr<std::optional<GlyphSet>> TryMerge(
    SegmentationContext& context, segment_index_t base_segment_index,
    const SegmentSet& to_merge_segments_) {
  if (WouldMixFeaturesAndCodepoints(context.segmentation_info,
                                    base_segment_index, to_merge_segments_)) {
    // Because we don't yet have a good cost function for evaluating potential
    // mergers: the merger if it doesn't find a previous merge candidate will
    // try to merge together segments that are composed of codepoints with a
    // segment that adds an optional feature. Since this feature segments are
    // likely rarely used this will inflate the size of the patches for those
    // codepoint segments unnecessarily.
    //
    // So for now just don't merge cases where we would be combining codepoint
    // only segments with feature only segments.
    VLOG(0) << "  Merge would mix features into a codepoint only segment, "
               "skipping.";
    return std::nullopt;
  }

  // Create a merged segment, and remove all of the others
  SegmentSet to_merge_segments = to_merge_segments_;
  SegmentSet to_merge_segments_with_base = to_merge_segments_;
  to_merge_segments.erase(base_segment_index);
  to_merge_segments_with_base.insert(base_segment_index);

  bool new_segment_is_inert =
      context.inert_segments.contains(base_segment_index) &&
      to_merge_segments.is_subset_of(context.inert_segments);
  if (new_segment_is_inert) {
    VLOG(0)
        << "  Merged segment will be inert, closure analysis will be skipped.";
  }

  const auto& segments = context.segmentation_info.Segments();
  uint32_t size_before = segments[base_segment_index].codepoints.size();

  SubsetDefinition merged_segment = segments[base_segment_index];
  MergeSegments(context.segmentation_info, to_merge_segments, merged_segment);

  GlyphSet gid_conditions_to_update;
  for (segment_index_t segment_index : to_merge_segments) {
    // segments which are being removed/changed may appear in gid_conditions, we
    // need to update those (and the down stream and/or glyph groups) to reflect
    // the removal/change and allow recalculation during the GroupGlyphs steps
    //
    // Changes caused by adding new segments into the base segment will be
    // handled by the next AnalyzeSegment step.
    gid_conditions_to_update.union_set(
        context.glyph_condition_set.GlyphsWithSegment(segment_index));
  }

  uint32_t new_patch_size = 0;
  if (!new_segment_is_inert) {
    new_patch_size =
        TRY(EstimatePatchSizeBytes(context, to_merge_segments_with_base));
  } else {
    // For inert patches we can precompute the glyph set saving a closure
    // operation
    GlyphSet merged_glyphs = gid_conditions_to_update;
    merged_glyphs.union_set(
        context.glyph_condition_set.GlyphsWithSegment(base_segment_index));
    new_patch_size =
        TRY(EstimatePatchSizeBytes(context.original_face.get(), merged_glyphs));
  }
  if (new_patch_size > context.patch_size_max_bytes) {
    return std::nullopt;
  }

  uint32_t size_after = context.segmentation_info.MergeSegments(
      base_segment_index, to_merge_segments, merged_segment);
  VLOG(0) << "  Merged " << size_before << " codepoints up to " << size_after
          << " codepoints for segment " << base_segment_index
          << ". New patch size " << new_patch_size << " bytes.";

  // Remove the fallback segment or group, it will be fully recomputed by
  // GroupGlyphs
  context.glyph_groupings.RemoveFallbackSegments(to_merge_segments);

  // Regardless of wether the new segment is inert all of the information
  // associated with the segments removed by the merge should be removed.
  context.InvalidateGlyphInformation(gid_conditions_to_update,
                                     to_merge_segments);

  if (new_segment_is_inert) {
    // The newly formed segment will be inert which means we can construct the
    // new condition sets and glyph groupings here instead of using the closure
    // analysis to do it. The new segment is simply the union of all glyphs
    // associated with each segment that is part of the merge.
    // (gid_conditons_to_update)
    context.inert_segments.insert(base_segment_index);
    for (glyph_id_t gid : gid_conditions_to_update) {
      context.glyph_condition_set.AddAndCondition(gid, base_segment_index);
    }
    context.glyph_groupings.AddGlyphsToExclusiveGroup(base_segment_index,
                                                      gid_conditions_to_update);

    // We've now fully updated information for these glyphs so don't need to
    // return them.
    gid_conditions_to_update.clear();
  } else {
    context.inert_segments.erase(base_segment_index);
  }

  return gid_conditions_to_update;
}

/*
 * Search for a composite condition which can be merged into base_segment_index.
 *
 * Returns the set of glyphs invalidated by the merge if found and the merge
 * suceeded.
 */
StatusOr<std::optional<GlyphSet>> TryMergingACompositeCondition(
    SegmentationContext& context, segment_index_t base_segment_index,
    const ActivationCondition& base_condition) {
  auto candidate_conditions =
      context.glyph_groupings.TriggeringSegmentToConditions(base_segment_index);
  for (ActivationCondition next_condition : candidate_conditions) {
    if (next_condition.IsFallback()) {
      // Merging the fallback will cause all segments to be merged into one,
      // which is undesirable so don't consider the fallback.
      continue;
    }

    if (next_condition < base_condition || next_condition == base_condition) {
      // all conditions before base_condition are already processed, so we only
      // want to search after base_condition.
      continue;
    }

    SegmentSet triggering_segments = next_condition.TriggeringSegments();
    if (!triggering_segments.contains(base_segment_index)) {
      continue;
    }

    auto modified_gids =
        TRY(TryMerge(context, base_segment_index, triggering_segments));
    if (!modified_gids.has_value()) {
      continue;
    }

    VLOG(0) << "  Merging segments from composite patch into segment "
            << base_segment_index << ": " << next_condition.ToString();
    return modified_gids;
  }

  return std::nullopt;
}

/*
 * Search for a base segment after base_segment_index which can be merged into
 * base_segment_index without exceeding the maximum patch size.
 *
 * Returns the set of glyphs invalidated by the merge if found and the merge
 * suceeded.
 */
template <typename ConditionAndGlyphIt>
StatusOr<std::optional<GlyphSet>> TryMergingABaseSegment(
    SegmentationContext& context, segment_index_t base_segment_index,
    const ConditionAndGlyphIt& condition_it) {
  // TODO(garretrieger): this currently merges at most one segment at a time
  //  into base. we could likely significantly improve performance (ie.
  //  reducing number of closure and brotli ops) by choosing multiple segments
  //  at once if it seems likely the new patch size will be within the
  //  thresholds. A rough estimate of patch size can be generated by summing the
  //  individual patch sizes of the existing patches for each segment. Finally
  //  we can run the merge, and check if the actual patch size is within bounds.
  //
  //  As part of this we should start caching patch size results so the
  //  individual patch sizes don't need to be recomputed later on.
  auto next_condition_it = condition_it;
  next_condition_it++;
  while (next_condition_it !=
         context.glyph_groupings.ConditionsAndGlyphs().end()) {
    auto next_condition = next_condition_it->first;
    if (!next_condition.IsExclusive()) {
      // Only interested in other base patches.
      next_condition_it++;
      continue;
    }

    SegmentSet triggering_segments = next_condition.TriggeringSegments();

    auto modified_gids =
        TRY(TryMerge(context, base_segment_index, triggering_segments));
    if (!modified_gids.has_value()) {
      next_condition_it++;
      continue;
    }

    VLOG(0) << "  Merging segments from base patch into segment "
            << base_segment_index << ": " << next_condition.ToString();
    return modified_gids;
  }

  return std::nullopt;
}

// Computes the estimated size of the patch for a segment and returns true if it
// is below the minimum.
StatusOr<bool> IsPatchTooSmall(SegmentationContext& context,
                               segment_index_t base_segment_index,
                               const GlyphSet& glyphs) {
  uint32_t patch_size_bytes =
      TRY(EstimatePatchSizeBytes(context.original_face.get(), glyphs));
  if (patch_size_bytes >= context.patch_size_min_bytes) {
    return false;
  }

  VLOG(0) << "Patch for segment " << base_segment_index << " is too small "
          << "(" << patch_size_bytes << " < " << context.patch_size_min_bytes
          << "). Merging...";

  return true;
}

/*
 * Searches segments starting from start_segment for the next who's exclusive
 * gids patch is too small. If found, try increasing the size of the patch via
 * merging.
 *
 * If a merge was performed returns the segment which was modified to allow
 * groupings to be updated.
 */
StatusOr<std::optional<std::pair<segment_index_t, GlyphSet>>>
MergeNextBaseSegment(SegmentationContext& context, uint32_t start_segment) {
  auto start_condition =
      ActivationCondition::exclusive_segment(start_segment, 0);
  for (auto it = context.glyph_groupings.ConditionsAndGlyphs().lower_bound(
           start_condition);
       it != context.glyph_groupings.ConditionsAndGlyphs().end(); it++) {
    const auto& condition = it->first;
    if (!condition.IsExclusive()) {
      continue;
    }

    segment_index_t base_segment_index =
        (*condition.conditions().begin()->begin());
    if (base_segment_index < start_segment) {
      // Already processed, skip
      continue;
    }

    if (!TRY(IsPatchTooSmall(context, base_segment_index, it->second))) {
      continue;
    }

    auto modified_gids = TRY(
        TryMergingACompositeCondition(context, base_segment_index, condition));
    if (modified_gids.has_value()) {
      // Return to the parent method so it can reanalyze and reform groups
      return std::pair(base_segment_index, *modified_gids);
    }

    modified_gids =
        TRY(TryMergingABaseSegment(context, base_segment_index, it));
    if (modified_gids.has_value()) {
      // Return to the parent method so it can reanalyze and reform groups
      return std::pair(base_segment_index, *modified_gids);
    }

    VLOG(0) << "Unable to get segment " << base_segment_index
            << " above minimum size. Continuing to next segment.";
  }

  return std::nullopt;
}

/*
 * Checks that the incrementally generated glyph conditions and groupings in
 * context match what would have been produced by a non incremental process.
 *
 * Returns OkStatus() if they match.
 */
Status ValidateIncrementalGroupings(hb_face_t* face,
                                    const SegmentationContext& context) {
  SegmentationContext non_incremental_context(
      face, context.segmentation_info.InitFontSegment(),
      context.segmentation_info.Segments());
  non_incremental_context.patch_size_min_bytes = 0;
  non_incremental_context.patch_size_max_bytes = UINT32_MAX;

  // Compute the glyph groupings/conditions from scratch to compare against the
  // incrementall produced ones.
  for (segment_index_t segment_index = 0;
       segment_index < context.segmentation_info.Segments().size();
       segment_index++) {
    TRY(non_incremental_context.ReprocessSegment(segment_index));
  }

  GlyphSet all_glyphs;
  uint32_t glyph_count = hb_face_get_glyph_count(face);
  all_glyphs.insert_range(0, glyph_count - 1);
  TRYV(non_incremental_context.GroupGlyphs(all_glyphs));

  if (non_incremental_context.glyph_groupings.ConditionsAndGlyphs() !=
      context.glyph_groupings.ConditionsAndGlyphs()) {
    return absl::FailedPreconditionError(
        "conditions_and_glyphs aren't correct.");
  }

  if (non_incremental_context.glyph_condition_set !=
      context.glyph_condition_set) {
    return absl::FailedPreconditionError("glyph_condition_set isn't correct.");
  }

  if (non_incremental_context.glyph_groupings.AndGlyphGroups() !=
      context.glyph_groupings.AndGlyphGroups()) {
    return absl::FailedPreconditionError("and_glyph groups aren't correct.");
  }

  if (non_incremental_context.glyph_groupings.OrGlyphGroups() !=
      context.glyph_groupings.OrGlyphGroups()) {
    return absl::FailedPreconditionError("or_glyph groups aren't correct.");
  }

  return absl::OkStatus();
}

StatusOr<GlyphSegmentation> ClosureGlyphSegmenter::CodepointToGlyphSegments(
    hb_face_t* face, SubsetDefinition initial_segment,
    std::vector<SubsetDefinition> codepoint_segments,
    uint32_t patch_size_min_bytes, uint32_t patch_size_max_bytes) const {
  uint32_t glyph_count = hb_face_get_glyph_count(face);
  if (!glyph_count) {
    return absl::InvalidArgumentError("Provided font has no glyphs.");
  }

  SegmentationContext context(face, initial_segment, codepoint_segments);
  context.patch_size_min_bytes = patch_size_min_bytes;
  context.patch_size_max_bytes = patch_size_max_bytes;

  // ### Generate the initial conditions and groupings by processing all
  // segments and glyphs. ###
  VLOG(0) << "Forming initial segmentation plan.";
  for (segment_index_t segment_index = 0;
       segment_index < context.segmentation_info.Segments().size();
       segment_index++) {
    TRY(context.ReprocessSegment(segment_index));
  }
  context.glyph_closure_cache.LogClosureCount("Inital segment analysis");

  GlyphSet all_glyphs;
  all_glyphs.insert_range(0, glyph_count - 1);
  TRYV(context.GroupGlyphs(all_glyphs));
  context.glyph_closure_cache.LogClosureCount("Condition grouping");

  if (patch_size_min_bytes == 0) {
    // No merging will be needed so we're done.
    return context.ToGlyphSegmentation();
  }

  // ### Iteratively merge segments and incrementally reprocess affected data.
  segment_index_t last_merged_segment_index = 0;
  while (true) {
    auto merged = TRY(MergeNextBaseSegment(context, last_merged_segment_index));
    if (!merged.has_value()) {
      // Nothing was merged so we're done.
      TRYV(ValidateIncrementalGroupings(face, context));
      return context.ToGlyphSegmentation();
    }

    const auto& [merged_segment_index, modified_gids] = *merged;
    last_merged_segment_index = merged_segment_index;

    GlyphSet analysis_modified_gids;
    if (!context.inert_segments.contains(last_merged_segment_index)) {
      VLOG(0) << "Re-analyzing segment " << last_merged_segment_index
              << " due to merge.";
      analysis_modified_gids =
          TRY(context.ReprocessSegment(last_merged_segment_index));
    }
    analysis_modified_gids.union_set(modified_gids);

    TRYV(context.GroupGlyphs(analysis_modified_gids));

    context.glyph_closure_cache.LogClosureCount("Condition grouping");
  }

  return absl::InternalError("unreachable");
}

}  // namespace ift::encoder
