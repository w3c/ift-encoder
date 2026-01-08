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
  GlyphSet visited;
  const auto& initial_closure = segmentation.InitialFontGlyphClosure();
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

  GlyphSet full_minus_initial = segmentation_info_.FullClosure();
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
  if (segmentation_info_.Segments()[segment_index].Definition().Empty()) {
    // Empty segment is a noop;
    return GlyphSet{};
  }

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

}  // namespace ift::encoder