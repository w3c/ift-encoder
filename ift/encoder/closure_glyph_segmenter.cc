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
#include "ift/encoder/glyph_segmentation.h"
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
// - Add logging
//   - timing info
// - Use merging and/or duplication to ensure minimum patch size.
//   - composite patches (NOT STARTED)
// - Multi segment combination testing with GSUB dep analysis to guide.

class RequestedSegmentationInformation;
class GlyphClosureCache;

Status AnalyzeSegment(const RequestedSegmentationInformation& segmentation_info,
                      GlyphClosureCache& closure_cache,
                      const CodepointSet& codepoints, GlyphSet& and_gids,
                      GlyphSet& or_gids, GlyphSet& exclusive_gids);

/*
 * A cache of the results of glyph closure on a specific font face.
 */
class GlyphClosureCache {
 public:
  GlyphClosureCache(hb_face_t* face)
      : preprocessed_face_(make_hb_face(hb_subset_preprocess(face))),
        original_face_(make_hb_face(hb_face_reference(face))) {}

  StatusOr<GlyphSet> GlyphClosure(const CodepointSet& codepoints) {
    auto it = glyph_closure_cache_.find(codepoints);
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

    codepoints.union_into(hb_subset_input_unicode_set(input));
    // TODO(garretrieger): configure features (and other settings) appropriately
    // based on the IFT default feature list.

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

    glyph_closure_cache_.insert(std::pair(codepoints, GlyphSet(gids)));

    return GlyphSet(gids);
  }

  StatusOr<const GlyphSet*> CodepointsToOrGids(
      const RequestedSegmentationInformation& segmentation_info,
      const CodepointSet& codepoints) {
    auto it = code_point_set_to_or_gids_cache_.find(codepoints);
    if (it != code_point_set_to_or_gids_cache_.end()) {
      code_point_set_to_or_gids_cache_hit_++;
      return &it->second;
    }

    code_point_set_to_or_gids_cache_miss_++;
    GlyphSet and_gids;
    GlyphSet or_gids;
    GlyphSet exclusive_gids;
    TRYV(AnalyzeSegment(segmentation_info, *this, codepoints, and_gids, or_gids,
                        exclusive_gids));

    auto [new_value, inserted] =
        code_point_set_to_or_gids_cache_.insert(std::pair(codepoints, or_gids));
    return &new_value->second;
  }

  void LogCacheStats() const {
    double hit_rate = 100.0 * ((double)code_point_set_to_or_gids_cache_hit_) /
                      ((double)(code_point_set_to_or_gids_cache_hit_ +
                                code_point_set_to_or_gids_cache_miss_));
    VLOG(0) << "Codepoints to or_gids cache hit rate: " << hit_rate << "% ("
            << code_point_set_to_or_gids_cache_hit_ << " hits, "
            << code_point_set_to_or_gids_cache_miss_ << " misses)";

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
  flat_hash_map<CodepointSet, GlyphSet> glyph_closure_cache_;
  uint32_t glyph_closure_cache_hit_ = 0;
  uint32_t glyph_closure_cache_miss_ = 0;
  uint32_t closure_count_cumulative_ = 0;
  uint32_t closure_count_delta_ = 0;

  // for this cache we return pointers to the sets so need node_hash_map for
  // pointer stability.
  node_hash_map<CodepointSet, GlyphSet> code_point_set_to_or_gids_cache_;
  uint32_t code_point_set_to_or_gids_cache_hit_ = 0;
  uint32_t code_point_set_to_or_gids_cache_miss_ = 0;
};

/*
 * Stores basic information about the configuration of a requested segmentation
 */
class RequestedSegmentationInformation {
 public:
  RequestedSegmentationInformation(std::vector<CodepointSet> segments,
                                   CodepointSet init_font_codepoints,
                                   GlyphClosureCache& closure_cache)
      : segments_(std::move(segments)),
        init_font_codepoints_(std::move(init_font_codepoints)) {
    all_codepoints_.union_set(init_font_codepoints_);
    for (const auto& s : segments_) {
      all_codepoints_.union_set(s);
    }

    {
      auto closure = closure_cache.GlyphClosure(init_font_codepoints_);
      if (closure.ok()) {
        init_font_glyphs_ = std::move(*closure);
      }
    }

    auto closure = closure_cache.GlyphClosure(all_codepoints_);
    if (closure.ok()) {
      full_closure_ = std::move(*closure);
    }
  }

  uint32_t MergeSegments(segment_index_t base, const SegmentSet& to_merge,
                         const CodepointSet& merged_codepoints) {
    segments_[base].union_set(merged_codepoints);
    for (segment_index_t s : to_merge) {
      // To avoid changing the indices of other segments set the ones we're
      // removing to empty sets. That effectively disables them.
      segments_[s].clear();
    }
    return segments_[base].size();
  }

  const CodepointSet& InitFontCodepoints() const {
    return init_font_codepoints_;
  }

  const GlyphSet& InitFontGlyphs() const { return init_font_glyphs_; }

  const GlyphSet& FullClosure() const { return full_closure_; }

  const CodepointSet& AllCodepoints() const { return all_codepoints_; }

  const std::vector<CodepointSet>& Segments() const { return segments_; }

 private:
  std::vector<CodepointSet> segments_;
  CodepointSet init_font_codepoints_;
  GlyphSet init_font_glyphs_;
  CodepointSet all_codepoints_;
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
 * Grouping of the glyphs into a font by the associated activation conditions.
 */
class GlyphGroupings {
 public:
  GlyphGroupings(const std::vector<CodepointSet>& segments) {
    uint32_t index = 0;
    for (const auto& s : segments) {
      if (!s.empty()) {
        fallback_segments_.insert(index);
      }
      index++;
    }
  }

  const btree_map<GlyphSegmentation::ActivationCondition, GlyphSet>&
  ConditionsAndGlyphs() const {
    return conditions_and_glyphs_;
  }

  const btree_map<SegmentSet, GlyphSet>& AndGlyphGroups() const {
    return and_glyph_groups_;
  }

  const btree_map<SegmentSet, GlyphSet>& OrGlyphGroups() const {
    return or_glyph_groups_;
  }

  // Index from segment index to the conditions that reference it.
  const btree_set<GlyphSegmentation::ActivationCondition>&
  TriggeringSegmentToConditions(segment_index_t segment) const {
    static btree_set<GlyphSegmentation::ActivationCondition> empty;
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
      if (it->second.empty()) {
        and_glyph_groups_.erase(it);
        GlyphSegmentation::ActivationCondition activation_condition =
            GlyphSegmentation::ActivationCondition::and_segments(
                condition.and_segments, 0);
        if (condition.and_segments.size() == 1) {
          activation_condition =
              GlyphSegmentation::ActivationCondition::exclusive_segment(
                  *condition.and_segments.begin(), 0);
        }
        RemoveConditionAndGlyphs(activation_condition);
      }
    }

    it = or_glyph_groups_.find(condition.or_segments);
    if (it != or_glyph_groups_.end()) {
      it->second.erase(gid);
      if (it->second.empty()) {
        or_glyph_groups_.erase(it);
        GlyphSegmentation::ActivationCondition activation_condition =
            GlyphSegmentation::ActivationCondition::or_segments(
                condition.or_segments, 0);
        RemoveConditionAndGlyphs(activation_condition);
      }
    }

    unmapped_glyphs_.erase(gid);
  }

  void RemoveFallbackSegments(const SegmentSet& removed_segments) {
    // Invalidate the existing fallback segment 'or group', it will be fully
    // recomputed by GroupGlyphs
    or_glyph_groups_.erase(fallback_segments_);
    for (segment_index_t segment_index : removed_segments) {
      fallback_segments_.erase(segment_index);
    }
  }

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
            GlyphSegmentation::ActivationCondition::exclusive_segment(
                *and_group.begin(), 0);
        AddConditionAndGlyphs(condition, and_glyph_groups_[and_group]);
        continue;
      }

      auto condition =
          GlyphSegmentation::ActivationCondition::and_segments(and_group, 0);
      AddConditionAndGlyphs(condition, and_glyph_groups_[and_group]);
    }

    // Any of the or_set conditions we've generated may have some additional
    // conditions that were not detected. Therefore we need to rule out the
    // presence of these additional conditions if an or group is able to be
    // used.
    for (const auto& or_group : modified_or_groups) {
      auto& glyphs = or_glyph_groups_[or_group];
      CodepointSet all_other_codepoints = segmentation_info.AllCodepoints();
      for (uint32_t s : or_group) {
        all_other_codepoints.subtract(segmentation_info.Segments()[s]);
      }

      const GlyphSet* or_gids = TRY(closure_cache.CodepointsToOrGids(
          segmentation_info, all_other_codepoints));

      // Any "OR" glyphs associated with all other codepoints have some
      // additional conditions to activate so we can't safely include them into
      // this or condition. They are instead moved to the set of unmapped
      // glyphs.
      for (uint32_t gid : *or_gids) {
        if (glyphs.erase(gid) > 0) {
          unmapped_glyphs_.insert(gid);
        }
      }

      GlyphSegmentation::ActivationCondition condition =
          GlyphSegmentation::ActivationCondition::or_segments(or_group, 0);
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
    // context.conditions_and_glyphs since
    //       all downstream processing which utilizes that map ignores the
    //       fallback segment.

    return absl::OkStatus();
  }

  StatusOr<GlyphSegmentation> ToGlyphSegmentation(
      const RequestedSegmentationInformation& segmentation_info) const {
    GlyphSegmentation segmentation(segmentation_info.InitFontCodepoints(),
                                   segmentation_info.InitFontGlyphs(),
                                   unmapped_glyphs_);
    segmentation.CopySegments(segmentation_info.Segments());
    TRYV(GlyphSegmentation::GroupsToSegmentation(
        and_glyph_groups_, or_glyph_groups_, fallback_segments_, segmentation));
    return segmentation;
  }

 private:
  void AddConditionAndGlyphs(GlyphSegmentation::ActivationCondition condition,
                             GlyphSet glyphs) {
    const auto& [new_value_it, did_insert] =
        conditions_and_glyphs_.insert(std::pair(condition, glyphs));
    for (segment_index_t s : condition.TriggeringSegments()) {
      triggering_segment_to_conditions_[s].insert(new_value_it->first);
    }
  }

  void RemoveConditionAndGlyphs(
      GlyphSegmentation::ActivationCondition condition) {
    conditions_and_glyphs_.erase(condition);
    for (segment_index_t s : condition.TriggeringSegments()) {
      triggering_segment_to_conditions_[s].erase(condition);
    }
  }

  btree_map<SegmentSet, GlyphSet> and_glyph_groups_;
  btree_map<SegmentSet, GlyphSet> or_glyph_groups_;

  // An alternate representation of and/or_glyph_groups_, derived from them.
  btree_map<GlyphSegmentation::ActivationCondition, GlyphSet>
      conditions_and_glyphs_;

  // Index that maps segments to all conditions in conditions_and_glyphs_ which
  // reference that segment.
  flat_hash_map<uint32_t, btree_set<GlyphSegmentation::ActivationCondition>>
      triggering_segment_to_conditions_;

  // Set of segments in the fallback condition.
  SegmentSet fallback_segments_;

  // These glyphs aren't mapped by any conditions and as a result should be
  // included in the fallback patch.
  common::GlyphSet unmapped_glyphs_;
};

class SegmentationContext;

class SegmentationContext {
 public:
  SegmentationContext(hb_face_t* face, const CodepointSet& initial_segment,
                      const std::vector<CodepointSet>& codepoint_segments)
      : glyph_closure_cache(face),
        original_face(make_hb_face(hb_face_reference(face))),
        segmentation_info(codepoint_segments, initial_segment,
                          glyph_closure_cache),
        glyph_condition_set(hb_face_get_glyph_count(face)),
        glyph_groupings(codepoint_segments) {}

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

  Status AnalyzeSegment(const CodepointSet& codepoints, GlyphSet& and_gids,
                        GlyphSet& or_gids, GlyphSet& exclusive_gids) {
    return ::ift::encoder::AnalyzeSegment(segmentation_info,
                                          glyph_closure_cache, codepoints,
                                          and_gids, or_gids, exclusive_gids);
  }

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

  // Phase 2 - derived from glyph_condition_set and init information.
  GlyphGroupings glyph_groupings;
};

Status AnalyzeSegment(const RequestedSegmentationInformation& segmentation_info,
                      GlyphClosureCache& closure_cache,
                      const CodepointSet& codepoints, GlyphSet& and_gids,
                      GlyphSet& or_gids, GlyphSet& exclusive_gids) {
  if (codepoints.empty()) {
    // Skip empty sets, they will never contribute any conditions.
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
  CodepointSet except_segment = segmentation_info.AllCodepoints();
  except_segment.subtract(codepoints);
  auto B_except_segment_closure =
      TRY(closure_cache.GlyphClosure(except_segment));

  CodepointSet only_segment = segmentation_info.InitFontCodepoints();
  only_segment.union_set(codepoints);
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

StatusOr<GlyphSet> AnalyzeSegment(SegmentationContext& context,
                                  segment_index_t segment_index,
                                  const CodepointSet& codepoints) {
  GlyphSet and_gids;
  GlyphSet or_gids;
  GlyphSet exclusive_gids;
  TRYV(AnalyzeSegment(context.segmentation_info, context.glyph_closure_cache,
                      codepoints, and_gids, or_gids, exclusive_gids));

  GlyphSet changed_gids;
  changed_gids.union_set(and_gids);
  changed_gids.union_set(or_gids);
  changed_gids.union_set(exclusive_gids);
  for (uint32_t gid : changed_gids) {
    context.InvalidateGlyphInformation(GlyphSet{gid},
                                       SegmentSet{segment_index});
  }

  for (uint32_t and_gid : exclusive_gids) {
    // TODO(garretrieger): if we are assigning an exclusive gid there should be
    // no other and segments, check and error if this is violated.
    context.glyph_condition_set.AddAndCondition(and_gid, segment_index);
  }

  for (uint32_t and_gid : and_gids) {
    context.glyph_condition_set.AddAndCondition(and_gid, segment_index);
  }

  for (uint32_t or_gid : or_gids) {
    context.glyph_condition_set.AddOrCondition(or_gid, segment_index);
  }

  return changed_gids;
}

StatusOr<uint32_t> PatchSizeBytes(hb_face_t* original_face,
                                  const IntSet& gids) {
  FontData font_data(original_face);
  CompatId id;
  // Since this is just an estimate and we don't need ultra precise numbers run
  // at a lower brotli quality to improve performance.
  GlyphKeyedDiff diff(font_data, id,
                      {FontHelper::kGlyf, FontHelper::kGvar, FontHelper::kCFF,
                       FontHelper::kCFF2},
                      9);

  auto patch_data = TRY(diff.CreatePatch(gids));
  return patch_data.size();
}

void MergeSegments(const RequestedSegmentationInformation& segmentation_info,
                   const IntSet& segments, IntSet& base) {
  for (uint32_t next : segments) {
    base.union_set(segmentation_info.Segments()[next]);
  }
}

StatusOr<uint32_t> EstimatePatchSize(SegmentationContext& context,
                                     const CodepointSet& codepoints) {
  GlyphSet and_gids;
  GlyphSet or_gids;
  GlyphSet exclusive_gids;
  TRYV(context.AnalyzeSegment(codepoints, and_gids, or_gids, exclusive_gids));
  return PatchSizeBytes(context.original_face.get(), exclusive_gids);
}

StatusOr<std::optional<GlyphSet>> TryMerge(
    SegmentationContext& context, segment_index_t base_segment_index,
    const SegmentSet& to_merge_segments_) {
  // Create a merged segment, and remove all of the others
  SegmentSet to_merge_segments = to_merge_segments_;
  to_merge_segments.erase(base_segment_index);

  const auto& segments = context.segmentation_info.Segments();
  uint32_t size_before = segments[base_segment_index].size();

  CodepointSet merged_codepoints = segments[base_segment_index];
  MergeSegments(context.segmentation_info, to_merge_segments,
                merged_codepoints);

  uint32_t new_patch_size = TRY(EstimatePatchSize(context, merged_codepoints));
  if (new_patch_size > context.patch_size_max_bytes) {
    return std::nullopt;
  }

  uint32_t size_after = context.segmentation_info.MergeSegments(
      base_segment_index, to_merge_segments, merged_codepoints);
  VLOG(0) << "  Merged " << size_before << " codepoints up to " << size_after
          << " codepoints for segment " << base_segment_index
          << ". New patch size " << new_patch_size << " bytes.";

  // Remove the fallback segment or group, it will be fully recomputed by
  // GroupGlyphs
  context.glyph_groupings.RemoveFallbackSegments(to_merge_segments);

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

  context.InvalidateGlyphInformation(gid_conditions_to_update,
                                     to_merge_segments);

  return gid_conditions_to_update;
}

/*
 * Search for a composite condition which can be merged into base_segment_index.
 *
 * Returns true if one was found and the merge succeeded, false otherwise.
 */
StatusOr<std::optional<GlyphSet>> TryMergingACompositeCondition(
    SegmentationContext& context, segment_index_t base_segment_index,
    const GlyphSegmentation::ActivationCondition& base_condition) {
  auto candidate_conditions =
      context.glyph_groupings.TriggeringSegmentToConditions(base_segment_index);
  for (GlyphSegmentation::ActivationCondition next_condition :
       candidate_conditions) {
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
 * Returns true if found and the merge suceeded.
 */
template <typename ConditionAndGlyphIt>
StatusOr<std::optional<GlyphSet>> TryMergingABaseSegment(
    SegmentationContext& context, segment_index_t base_segment_index,
    const ConditionAndGlyphIt& condition_it) {
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

StatusOr<bool> IsPatchTooSmall(SegmentationContext& context,
                               segment_index_t base_segment_index,
                               const GlyphSet& glyphs) {
  uint32_t patch_size_bytes =
      TRY(PatchSizeBytes(context.original_face.get(), glyphs));
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
      GlyphSegmentation::ActivationCondition::exclusive_segment(start_segment,
                                                                0);
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
 * Checks that the incrementally generated groupings in context match what would
 * have been produced by a non incremental process.
 */
Status ValidateIncrementalGroupings(hb_face_t* face,
                                    const SegmentationContext& context) {
  SegmentationContext non_incremental_context(
      face, context.segmentation_info.InitFontCodepoints(),
      context.segmentation_info.Segments());
  non_incremental_context.patch_size_min_bytes = 0;
  non_incremental_context.patch_size_max_bytes = UINT32_MAX;

  // Compute the glyph groupings/conditions from scratch to compare against the
  // incrementall produced ones.
  segment_index_t segment_index = 0;
  for (const auto& segment :
       non_incremental_context.segmentation_info.Segments()) {
    TRY(AnalyzeSegment(non_incremental_context, segment_index, segment));
    segment_index++;
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
    hb_face_t* face, CodepointSet initial_segment,
    std::vector<CodepointSet> codepoint_segments, uint32_t patch_size_min_bytes,
    uint32_t patch_size_max_bytes) const {
  uint32_t glyph_count = hb_face_get_glyph_count(face);
  if (!glyph_count) {
    return absl::InvalidArgumentError("Provided font has no glyphs.");
  }

  SegmentationContext context(face, initial_segment, codepoint_segments);
  context.patch_size_min_bytes = patch_size_min_bytes;
  context.patch_size_max_bytes = patch_size_max_bytes;

  VLOG(0) << "Forming initial segmentation plan.";
  segment_index_t segment_index = 0;
  for (const auto& segment : context.segmentation_info.Segments()) {
    TRY(AnalyzeSegment(context, segment_index, segment));
    segment_index++;
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
    VLOG(0) << "Re-analyzing segment " << last_merged_segment_index
            << " due to merge.";

    GlyphSet analysis_modified_gids = TRY(AnalyzeSegment(
        context, last_merged_segment_index,
        context.segmentation_info.Segments()[last_merged_segment_index]));
    analysis_modified_gids.union_set(modified_gids);

    TRYV(context.GroupGlyphs(analysis_modified_gids));

    context.glyph_closure_cache.LogClosureCount("Condition grouping");
  }

  return absl::InternalError("unreachable");
}

}  // namespace ift::encoder
