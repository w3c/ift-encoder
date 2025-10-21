#ifndef IFT_ENCODER_SEGMENTATION_CONTEXT_H_
#define IFT_ENCODER_SEGMENTATION_CONTEXT_H_

#include <cstdint>
#include <memory>

#include "absl/status/status.h"
#include "common/font_data.h"
#include "common/int_set.h"
#include "common/try.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/glyph_condition_set.h"
#include "ift/encoder/glyph_groupings.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/patch_size_cache.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"

namespace ift::encoder {

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
                      const std::vector<Segment>& segments,
                      uint32_t brotli_quality,
                      uint32_t init_font_brotli_quality)
      : patch_size_cache(new PatchSizeCacheImpl(face, brotli_quality)),
        patch_size_cache_for_init_font(
            new PatchSizeCacheImpl(face, init_font_brotli_quality)),
        glyph_closure_cache(face),
        original_face(common::make_hb_face(hb_face_reference(face))),
        segmentation_info_(segments, initial_segment, glyph_closure_cache),
        glyph_condition_set(hb_face_get_glyph_count(face)),
        glyph_groupings(segments, hb_face_get_glyph_count(face)),
        brotli_quality_(brotli_quality) {}

  unsigned BrotliQuality() const { return brotli_quality_; }

  // Convert the information in this context into a finalized GlyphSegmentation
  // representation.
  absl::StatusOr<GlyphSegmentation> ToGlyphSegmentation() const {
    GlyphSegmentation segmentation =
        TRY(glyph_groupings.ToGlyphSegmentation(segmentation_info_));
    glyph_closure_cache.LogCacheStats();
    TRYV(ValidateSegmentation(segmentation));
    return segmentation;
  }

  const common::SegmentSet& InertSegments() const { return inert_segments_; }

  const RequestedSegmentationInformation& SegmentationInfo() const {
    return segmentation_info_;
  }

  // Assign a new merged segment to base and clear all of the segments that
  // were merged into it.
  uint32_t AssignMergedSegment(segment_index_t base,
                               const common::SegmentSet& to_merge,
                               const Segment& merged_segment, bool is_inert) {
    unsigned count =
        segmentation_info_.AssignMergedSegment(base, to_merge, merged_segment);
    inert_segments_.subtract(to_merge);
    if (is_inert) {
      inert_segments_.insert(base);
    } else {
      inert_segments_.erase(base);
    }
    return count;
  }

  /*
   * Removes all condition and grouping information related to all gids in
   * glyphs.
   */
  void InvalidateGlyphInformation(const common::GlyphSet& glyphs,
                                  const common::SegmentSet& segments) {
    // unmapped, and and/or glyph groups are down stream of glyph conditions
    // so must be invalidated. Do this before modifying the conditions.
    for (uint32_t gid : glyphs) {
      const auto& condition = glyph_condition_set.ConditionsFor(gid);
      glyph_groupings.InvalidateGlyphInformation(condition, gid);
    }

    glyph_condition_set.InvalidateGlyphInformation(glyphs, segments);
  }

  /*
   * Invalidates all grouping information and fully reprocesses all segments.
   */
  absl::Status ReassignInitSubset(SubsetDefinition new_def) {
    unsigned glyph_count = hb_face_get_glyph_count(original_face.get());

    segmentation_info_.ReassignInitSubset(glyph_closure_cache,
                                          std::move(new_def));

    // All segments depend on the init subset def, so we must reprocess
    // everything. First reset grouping information:
    glyph_condition_set = GlyphConditionSet(glyph_count);
    glyph_groupings =
        GlyphGroupings(SegmentationInfo().Segments(), glyph_count);
    inert_segments_.clear();

    // Then reprocess segments:
    for (segment_index_t segment_index = 0;
         segment_index < SegmentationInfo().Segments().size();
         segment_index++) {
      TRY(ReprocessSegment(segment_index));
    }

    common::GlyphSet all_glyphs;
    all_glyphs.insert_range(0, glyph_count - 1);
    TRYV(GroupGlyphs(all_glyphs));
    glyph_closure_cache.LogClosureCount(
        "Segmentation reprocess for init def change.");

    return absl::OkStatus();
  }

  // Performs a closure analysis on codepoints and returns the associated
  // and, or, and exclusive glyph sets.
  absl::Status AnalyzeSegment(const common::SegmentSet& segment_ids,
                              common::GlyphSet& and_gids,
                              common::GlyphSet& or_gids,
                              common::GlyphSet& exclusive_gids) {
    return glyph_closure_cache.AnalyzeSegment(
        segmentation_info_, segment_ids, and_gids, or_gids, exclusive_gids);
  }

  // Generates updated glyph conditions and glyph groupings for segment_index
  // which has the provided set of codepoints.
  absl::StatusOr<common::GlyphSet> ReprocessSegment(
      segment_index_t segment_index);

  // Update the glyph groups for 'glyphs'.
  //
  // The glyph condition set must be up to date and fully computed prior to
  // calling this.
  absl::Status GroupGlyphs(const common::GlyphSet& glyphs) {
    return glyph_groupings.GroupGlyphs(segmentation_info_, glyph_condition_set,
                                       glyph_closure_cache, glyphs);
  }

 private:
  /*
   * Ensures that the produce segmentation is:
   * - Disjoint (no duplicated glyphs) and doesn't overlap what's in the initial
   * font.
   * - Fully covers the full closure.
   */
  absl::Status ValidateSegmentation(
      const GlyphSegmentation& segmentation) const;

  // If the merging strategy specifies a minimum cost threshold, this computes
  // the segment where we should stop computing optimized merges. This is
  // considered to be the place where the potentional upside of optimization is
  // too small to be worthwhile.
  absl::StatusOr<segment_index_t> ComputeSegmentCutoff() const;

 public:
  // Caches and logging
  std::unique_ptr<PatchSizeCache> patch_size_cache;
  std::unique_ptr<PatchSizeCache> patch_size_cache_for_init_font;
  GlyphClosureCache glyph_closure_cache;

  // Init
  common::hb_face_unique_ptr original_face;

 private:
  RequestedSegmentationInformation segmentation_info_;

 public:
  // == Phase 1 - derived from segments and init information
  GlyphConditionSet glyph_condition_set;

  // == Phase 2 - derived from glyph_condition_set and init information.
  GlyphGroupings glyph_groupings;

 private:
  // == Merging Segment metadata
  // segments that don't interact with anything
  common::SegmentSet inert_segments_;

  unsigned brotli_quality_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_SEGMENTATION_CONTEXT_H_