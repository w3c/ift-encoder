#include "ift/encoder/segmentation_context.h"

#include <cstdint>

#include "absl/status/status.h"
#include "common/int_set.h"
#include "common/try.h"
#include "ift/encoder/dependency_closure.h"
#include "ift/encoder/glyph_condition_set.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/types.h"

using absl::Status;
using absl::StatusOr;
using common::GlyphSet;
using common::IntSet;
using common::SegmentSet;

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

  return absl::OkStatus();
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
    glyph_condition_set.InvalidateGlyphInformation(GlyphSet{gid}, SegmentSet{segment_index});
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
    // TODO XXXXX this also needs to handle features?
    if (s.Definition().codepoints.intersects(new_def.codepoints)) {
        segments_with_changed_defs.insert(s_index);
    }
    s_index++;
  }

  SegmentSet segments_to_reprocess;
  if (!segmentation_info_->Segments().empty()) {
    segments_to_reprocess.insert_range(0, segmentation_info_->Segments().size() - 1);
  }
  if (dependency_closure_.has_value()) {
    // If dep graph is enabled we can use it to narrow the set of segments that need reprocessing.
    segments_to_reprocess = TRY((*dependency_closure_)->SegmentInteractionGroup(segments_with_changed_defs));
  }

  TRYV(segmentation_info_->ReassignInitSubset(*glyph_closure_cache, new_def));

  if (dependency_closure_.has_value()) {
    TRYV((*dependency_closure_)->SegmentsChanged(true, segments_to_reprocess));
  }

  // All segments depend on the init subset def, so we must reprocess
  // everything. First reset condition set information:
  GlyphConditionSet previous_glyph_condition_set = glyph_condition_set;

  glyph_condition_set.InvalidateGlyphInformation(removed_gids);
  glyph_condition_set.InvalidateGlyphInformation(segments_to_reprocess);
  inert_segments_.subtract(segments_to_reprocess);

  // Then reprocess segments:
  for (segment_index_t segment_index : segments_to_reprocess) {
    TRY(ReprocessSegment(segment_index));
  }

  // the groupings can be incrementally recomputed by looking at what conditions
  // have changed.
  GlyphSet changed_gids;
  changed_gids.union_set(removed_gids);
  for (glyph_id_t gid : SegmentationInfo().NonInitFontGlyphs()) {
    const auto& new_conditions = glyph_condition_set.ConditionsFor(gid);
    if (previous_glyph_condition_set.ConditionsFor(gid) !=
        new_conditions) {
      changed_gids.insert(gid);
    }
  }

  TRYV(GroupGlyphs(changed_gids, segments_with_changed_defs));

  return absl::OkStatus();
}

static void PrintDiff(absl::string_view set_name, const GlyphSet& closure, const GlyphSet& dep) {
  std::string op = " == ";
  if (closure != dep) {
    op = " != ";
  }

  LOG(ERROR) << "Set " << set_name
    << ": closure glyphs " << closure.ToString() << op
    << " dependency glyphs " << dep.ToString();
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
    auto accuracy = TRY((*dependency_closure_)->AnalyzeSegment(
      segment_ids, dep_and_gids, dep_or_gids, dep_exclusive_gids));
    if (accuracy == DependencyClosure::INACCURATE) {
      effective_mode = CLOSURE_ONLY;
    }
  }

  if (effective_mode == CLOSURE_ONLY || effective_mode == CLOSURE_AND_VALIDATE_DEP_GRAPH) {
    TRYV(glyph_closure_cache->AnalyzeSegment(
      *segmentation_info_, segment_ids, and_gids, or_gids, exclusive_gids));
  } else {
    or_gids.union_set(dep_or_gids);
    and_gids.union_set(dep_and_gids);
    exclusive_gids.union_set(dep_exclusive_gids);
  }

  if (effective_mode == CLOSURE_AND_VALIDATE_DEP_GRAPH) {
    if (and_gids != dep_and_gids ||
        or_gids != dep_or_gids ||
        exclusive_gids != dep_exclusive_gids) {
      LOG(ERROR) << "Mismatch between closure and depedency analysis conditions for segments " << segment_ids.ToString();
      for (segment_index_t s : segment_ids) {
        LOG(ERROR) << "segment[" << s << "].codepoints = " << segmentation_info_->Segments().at(s).Definition().codepoints.ToString();
        LOG(ERROR) << "segment[" << s << "].features.size() = " << segmentation_info_->Segments().at(s).Definition().feature_tags.size();
      }
      PrintDiff("AND", and_gids, dep_and_gids);
      PrintDiff("OR ", or_gids, dep_or_gids);
      PrintDiff("EXC", exclusive_gids, dep_exclusive_gids);
      LOG(ERROR) << "init codepoints = " << segmentation_info_->InitFontSegment().codepoints.ToString();
      LOG(ERROR) << "init glyphs = " << segmentation_info_->InitFontGlyphs().ToString();
      return absl::InternalError("Depedency graph conditions does not match the closure analysis conditions");
    }
  }

  return absl::OkStatus();
}

}  // namespace ift::encoder