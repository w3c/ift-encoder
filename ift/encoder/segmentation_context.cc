#include "ift/encoder/segmentation_context.h"

#include <cstdint>

#include "absl/status/status.h"
#include "common/int_set.h"
#include "common/try.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/patch_size_cache.h"
#include "ift/encoder/types.h"

using absl::Status;
using absl::StatusOr;
using common::GlyphSet;
using common::IntSet;
using common::SegmentSet;

namespace ift::encoder {

Status SegmentationContext::ValidateSegmentation(
    const GlyphSegmentation& segmentation) const {
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

  IntSet full_minus_initial = segmentation_info_.FullClosure();
  full_minus_initial.subtract(initial_closure);

  if (full_minus_initial != visited) {
    return absl::FailedPreconditionError(
        "Not all glyphs in the full closure have been placed.");
  }

  return absl::OkStatus();
}

StatusOr<GlyphSet> SegmentationContext::ReprocessSegment(
    segment_index_t segment_index) {
  GlyphSet and_gids;
  GlyphSet or_gids;
  GlyphSet exclusive_gids;
  TRYV(glyph_closure_cache.AnalyzeSegment(segmentation_info_, {segment_index},
                                          and_gids, or_gids, exclusive_gids));

  GlyphSet changed_gids;
  changed_gids.union_set(and_gids);
  changed_gids.union_set(or_gids);
  changed_gids.union_set(exclusive_gids);
  for (uint32_t gid : changed_gids) {
    InvalidateGlyphInformation(GlyphSet{gid}, SegmentSet{segment_index});
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

StatusOr<segment_index_t> SegmentationContext::ComputeSegmentCutoff() const {
  // For this computation to keep things simple we consider only exclusive
  // segments.
  //
  // Since this is just meant to compute a rough cutoff point below which
  // probabilites are too small to have any real impact on the final costs,
  // considering only exclusive segments is good enough for this calculation and
  // significantly simplifies things.

  // First compute the total cost for all active segments
  double total_cost = 0.0;
  double overhead = merge_strategy_.NetworkOverheadCost();
  for (segment_index_t s : active_segments_) {
    auto segment_glyphs = glyph_groupings.ExclusiveGlyphs(s);
    if (segment_glyphs.empty()) {
      continue;
    }

    double size = TRY(patch_size_cache->GetPatchSize(segment_glyphs));
    double probability = segmentation_info_.Segments()[s].Probability();
    total_cost += probability * (size + overhead);
  }

  double cutoff_tail_cost =
      total_cost * merge_strategy_.OptimizationCutoffFraction();
  segment_index_t previous_segment_index = UINT32_MAX;
  for (auto it = active_segments_.rbegin(); it != active_segments_.rend();
       it++) {
    segment_index_t s = *it;
    auto segment_glyphs = glyph_groupings.ExclusiveGlyphs(s);
    if (segment_glyphs.empty()) {
      continue;
    }

    double size = TRY(patch_size_cache->GetPatchSize(segment_glyphs));
    double probability = segmentation_info_.Segments()[s].Probability();
    cutoff_tail_cost -= probability * (size + overhead);
    if (cutoff_tail_cost < 0.0) {
      // This segment puts us above the cutoff, so set the cutoff as the
      // previous segment.
      return previous_segment_index;
    }

    previous_segment_index = s;
  }

  return previous_segment_index;
}

}  // namespace ift::encoder