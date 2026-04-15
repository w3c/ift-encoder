#include "ift/encoder/segmentation_context.h"

#include <cstdint>

#include "absl/status/status.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/dependency_closure.h"
#include "ift/encoder/glyph_condition_set.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/init_subset_defaults.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"

using ift::config::CLOSURE_AND_VALIDATE_DEP_GRAPH;
using ift::config::CLOSURE_ONLY;
using ift::config::ConditionAnalysisMode;
using ift::config::DEP_GRAPH_ONLY;
using ift::config::UnmappedGlyphHandling;

using absl::Status;
using absl::StatusOr;
using ift::common::GlyphSet;
using ift::common::IntSet;
using ift::common::SegmentSet;

namespace ift::encoder {

Status SegmentationContext::ValidateSegmentation(
    const GlyphSegmentation& segmentation) const {
  GlyphSet visited;
  const auto& initial_closure = segmentation.InitialFontGlyphClosure();
  for (const auto& [id, gids] : segmentation.GidSegments()) {
    for (glyph_id_t gid : gids) {
      if (initial_closure.contains(gid)) {
        return absl::FailedPreconditionError(absl::StrCat(
            "Initial font glyph g", gid, " is present in a patch."));
      }
      if (visited.contains(gid)) {
        return absl::FailedPreconditionError(
            "Glyph segments are not disjoint.");
      }
      visited.insert(gid);
    }
  }

  GlyphSet full_minus_initial = segmentation_info_->FullClosure();
  full_minus_initial.subtract(initial_closure);

  if (full_minus_initial != visited) {
    GlyphSet missing = full_minus_initial;
    missing.subtract(visited);
    return absl::FailedPreconditionError(
        "Not all glyphs in the full closure have been placed. Missing: " +
        missing.ToString());
  }

  for (const auto& condition : segmentation.Conditions()) {
    auto it = segmentation.GidSegments().find(condition.activated());
    if (it == segmentation.GidSegments().end()) {
      return absl::FailedPreconditionError(absl::StrCat("Patch ", condition.activated(), " does not have glyphs specified"));
    }

    const auto& glyphs = it->second;
    for (const auto& sub_group : condition.conditions()) {
      if (TRY(glyph_closure_cache->HasAdditionalConditions(&SegmentationInfo(), sub_group, glyphs))) {
        return absl::FailedPreconditionError(
          absl::StrCat("Found a condition which does not satisfy the glyph closure requirement: ",
          condition.ToString(), " ", glyphs.ToString(), ", sub group ", sub_group.ToString(), " failed the check."
          ));
      }
    }
  }

  return absl::OkStatus();
}

Status SegmentationContext::ReprocessChanged(InvalidationSet modified) {
  segment_index_t last_merged_segment_index = modified.base_segment;

  if (condition_analysis_mode_ != DEP_GRAPH_ONLY) {
    if (!InertSegments().contains(last_merged_segment_index)) {
      VLOG(1) << "Re-analyzing segment " << last_merged_segment_index
              << " due to merge.";
      GlyphSet analysis_modified_gids =
          TRY(ReprocessSegment(last_merged_segment_index));
      modified.glyphs.union_set(analysis_modified_gids);
    }
  } else {
    modified.glyphs.union_set(
        (*dependency_closure_)
            ->SegmentsToAffectedGlyphs({modified.base_segment}));
    TransferDependencyGraphGlyphConditions(modified.glyphs);
  }

  return GroupGlyphs(modified.glyphs, modified.segments);
}

Status SegmentationContext::ReprocessAll() {
  if (condition_analysis_mode_ != DEP_GRAPH_ONLY) {
    for (segment_index_t segment_index = 0;
         segment_index < SegmentationInfo().Segments().size();
         segment_index++) {
      TRY(ReprocessSegment(segment_index));
    }
  } else {
    // Pull conditions directly out of the dep graph instead of running closure
    // processing.
    TransferDependencyGraphGlyphConditions(
        segmentation_info_->NonInitFontGlyphs());
  }

  return GroupGlyphs(SegmentationInfo().NonInitFontGlyphs(), {});
}

StatusOr<GlyphSet> SegmentationContext::ReprocessSegment(
    segment_index_t segment_index) {
  if (segmentation_info_->Segments()[segment_index].Definition().Empty()) {
    // Empty segment is a noop;
    return GlyphSet{};
  }

  GlyphSet and_gids;
  GlyphSet or_gids;
  GlyphSet exclusive_gids;
  TRYV(AnalyzeSegment({segment_index}, and_gids, or_gids, exclusive_gids));

  GlyphSet changed_gids;
  changed_gids.union_set(and_gids);
  changed_gids.union_set(or_gids);
  changed_gids.union_set(exclusive_gids);

  for (uint32_t gid : changed_gids) {
    glyph_condition_set.InvalidateGlyphInformation(GlyphSet{gid},
                                                   SegmentSet{segment_index});
  }

  if (and_gids.empty() && or_gids.empty()) {
    inert_segments_.insert(segment_index);
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

/*
 * Invalidates all grouping information and fully reprocesses all segments.
 */
Status SegmentationContext::ReassignInitSubset(SubsetDefinition new_def) {
  // Figure out what's going to change before making the change so that we
  // can utilize the dep graph to locate affected segments.
  new_def = TRY(glyph_closure_cache->ExpandClosure(new_def));
  GlyphSet removed_gids = SegmentationInfo().NonInitFontGlyphs();
  removed_gids.intersect(new_def.gids);

  SegmentSet segments_with_changed_defs;
  uint32_t s_index = 0;
  for (auto& s : segmentation_info_->Segments()) {
    if (s.Definition().codepoints.intersects(new_def.codepoints)) {
      segments_with_changed_defs.insert(s_index);
    }
    for (hb_tag_t f : s.Definition().feature_tags) {
      if (new_def.feature_tags.contains(f)) {
        segments_with_changed_defs.insert(s_index);
        break;
      }
    }
    s_index++;
  }

  SegmentSet segments_to_reprocess;
  if (!segmentation_info_->Segments().empty()) {
    segments_to_reprocess.insert_range(
        0, segmentation_info_->Segments().size() - 1);
  }
  if (dependency_closure_.has_value()) {
    SubsetDefinition new_def_delta = new_def;
    new_def_delta.Subtract(SegmentationInfo().InitFontSegment());
    // If dep graph is enabled we can use it to narrow the set of segments that
    // need reprocessing.
    segments_to_reprocess =
        TRY((*dependency_closure_)->SegmentsThatInteractWith(new_def_delta));
  }

  TRYV(segmentation_info_->ReassignInitSubset(*glyph_closure_cache, new_def));

  if (dependency_closure_.has_value()) {
    TRYV((*dependency_closure_)->InitFontChanged(segments_to_reprocess));
  }

  // All segments depend on the init subset def, so we must reprocess
  // everything. First reset condition set information:
  GlyphConditionSet previous_glyph_condition_set = glyph_condition_set;

  glyph_condition_set.InvalidateGlyphInformation(removed_gids);
  glyph_condition_set.InvalidateGlyphInformation(segments_to_reprocess);
  inert_segments_.subtract(segments_to_reprocess);

  // Then reprocess segments:
  if (condition_analysis_mode_ != DEP_GRAPH_ONLY) {
    for (segment_index_t segment_index : segments_to_reprocess) {
      TRY(ReprocessSegment(segment_index));
    }
  } else {
    GlyphSet gids;
    for (segment_index_t s : segments_to_reprocess) {
      gids.union_set((*dependency_closure_)->SegmentsToAffectedGlyphs({s}));
    }
    TransferDependencyGraphGlyphConditions(gids);
  }

  // the groupings can be incrementally recomputed by looking at what conditions
  // have changed.
  GlyphSet changed_gids;
  changed_gids.union_set(removed_gids);
  for (glyph_id_t gid : SegmentationInfo().NonInitFontGlyphs()) {
    const auto& new_conditions = glyph_condition_set.ConditionsFor(gid);
    if (previous_glyph_condition_set.ConditionsFor(gid) != new_conditions) {
      changed_gids.insert(gid);
    }
  }

  TRYV(GroupGlyphs(changed_gids, segments_with_changed_defs));

  return absl::OkStatus();
}

static void PrintDiff(absl::string_view set_name, const GlyphSet& closure,
                      const GlyphSet& dep) {
  std::string op = " == ";
  if (closure != dep) {
    op = " != ";
  }

  LOG(ERROR) << "Set " << set_name << ": closure glyphs " << closure.ToString()
             << op << " dependency glyphs " << dep.ToString();
}

Status SegmentationContext::AnalyzeSegment(const SegmentSet& segment_ids,
                                           GlyphSet& and_gids,
                                           GlyphSet& or_gids,
                                           GlyphSet& exclusive_gids) {
  ConditionAnalysisMode effective_mode = condition_analysis_mode_;
  GlyphSet dep_and_gids = and_gids;
  GlyphSet dep_or_gids = or_gids;
  GlyphSet dep_exclusive_gids = exclusive_gids;
  if (dependency_closure_.has_value()) {
    auto accuracy = TRY((*dependency_closure_)
                            ->AnalyzeSegment(segment_ids, dep_and_gids,
                                             dep_or_gids, dep_exclusive_gids));
    if (accuracy == DependencyClosure::INACCURATE) {
      effective_mode = CLOSURE_ONLY;
    }
  }

  if (effective_mode == CLOSURE_ONLY ||
      effective_mode == CLOSURE_AND_VALIDATE_DEP_GRAPH) {
    TRYV(glyph_closure_cache->AnalyzeSegment(
        *segmentation_info_, segment_ids, and_gids, or_gids, exclusive_gids));
  } else {
    or_gids.union_set(dep_or_gids);
    and_gids.union_set(dep_and_gids);
    exclusive_gids.union_set(dep_exclusive_gids);
  }

  if (effective_mode == CLOSURE_AND_VALIDATE_DEP_GRAPH) {
    if (and_gids != dep_and_gids || or_gids != dep_or_gids ||
        exclusive_gids != dep_exclusive_gids) {
      LOG(ERROR) << "Mismatch between closure and dependency analysis "
                    "conditions for segments "
                 << segment_ids.ToString();
      for (segment_index_t s : segment_ids) {
        LOG(ERROR) << "segment[" << s << "].codepoints = "
                   << segmentation_info_->Segments()
                          .at(s)
                          .Definition()
                          .codepoints.ToString();
        LOG(ERROR) << "segment[" << s << "].features.size() = "
                   << segmentation_info_->Segments()
                          .at(s)
                          .Definition()
                          .feature_tags.size();
      }
      PrintDiff("AND", and_gids, dep_and_gids);
      PrintDiff("OR ", or_gids, dep_or_gids);
      PrintDiff("EXC", exclusive_gids, dep_exclusive_gids);
      LOG(ERROR) << "init codepoints = "
                 << segmentation_info_->InitFontSegment().codepoints.ToString();
      LOG(ERROR) << "init glyphs = "
                 << segmentation_info_->InitFontGlyphs().ToString();
      return absl::InternalError(
          "Dependency graph conditions does not match the closure analysis "
          "conditions");
    }
  }

  return absl::OkStatus();
}

StatusOr<SegmentationContext>
SegmentationContext::InitializeSegmentationContext(
    hb_face_t* face, SubsetDefinition initial_segment,
    std::vector<Segment> segments,
    UnmappedGlyphHandling unmapped_glyph_handling,
    ConditionAnalysisMode condition_analysis_mode, uint32_t brotli_quality,
    uint32_t init_font_brotli_quality) {
  if (!hb_face_get_glyph_count(face)) {
    return absl::InvalidArgumentError("Provided font has no glyphs.");
  }

  // The IFT compiler has a set of defaults always included in the initial font
  // add them here so we correctly factor them into the generated segmentation.
  AddInitSubsetDefaults(initial_segment);

  // No merging is done during init.
  SegmentationContext context = TRY(SegmentationContext::Create(
      face, initial_segment, segments, unmapped_glyph_handling,
      condition_analysis_mode, brotli_quality, init_font_brotli_quality));

  // ### Generate the initial conditions and groupings by processing all
  // segments and glyphs. ###
  VLOG(0) << "Forming initial segmentation plan.";
  TRYV(context.ReprocessAll());

  return context;
}

void SegmentationContext::TransferDependencyGraphGlyphConditions(
    const GlyphSet& gids) {
  const auto& conditions = (*dependency_closure_)->AllGlyphConditions();
  for (glyph_id_t g : gids) {
    ActivationCondition condition = conditions.at(g);
    if (condition.IsUnitary()) {
      condition = ActivationCondition::exclusive_segment(
          *condition.TriggeringSegments().begin(), 0);
    }
    glyph_condition_set.SetCondition(g, condition);
  }
}

}  // namespace ift::encoder