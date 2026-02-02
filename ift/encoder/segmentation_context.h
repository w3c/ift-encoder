#ifndef IFT_ENCODER_SEGMENTATION_CONTEXT_H_
#define IFT_ENCODER_SEGMENTATION_CONTEXT_H_

#include <cstdint>
#include <memory>
#include <optional>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "common/font_data.h"
#include "common/int_set.h"
#include "common/try.h"
#include "hb.h"
#include "ift/encoder/dependency_closure.h"
#include "ift/encoder/estimated_patch_size_cache.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/glyph_condition_set.h"
#include "ift/encoder/glyph_groupings.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/patch_size_cache.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"
#include "util/segmenter_config.pb.h"

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
  static absl::StatusOr<SegmentationContext> Create(
    hb_face_t* face, const SubsetDefinition& initial_segment,
    const std::vector<Segment>& segments,
    UnmappedGlyphHandling unmapped_glyph_handling,
    ConditionAnalysisMode condition_analysis_mode,
    uint32_t brotli_quality,
    uint32_t init_font_brotli_quality) {
    // TODO(garretrieger): argument list is getting long, switch to a builder pattern
    // for construction.
    SegmentationContext context(
      face, initial_segment, segments, unmapped_glyph_handling,
      condition_analysis_mode, brotli_quality, init_font_brotli_quality);

    if ((condition_analysis_mode == CLOSURE_AND_DEP_GRAPH) ||
        (condition_analysis_mode == CLOSURE_AND_VALIDATE_DEP_GRAPH)) {
      context.dependency_closure_ = TRY(DependencyClosure::Create(context.segmentation_info_.get(), context.original_face.get()));
    }
    return context;
  }

 private:
  SegmentationContext(hb_face_t* face, const SubsetDefinition& initial_segment,
                      const std::vector<Segment>& segments,
                      UnmappedGlyphHandling unmapped_glyph_handling,
                      ConditionAnalysisMode condition_analysis_mode,
                      uint32_t brotli_quality,
                      uint32_t init_font_brotli_quality)
      : patch_size_cache(NewPatchSizeCache(face, brotli_quality)),
        patch_size_cache_for_init_font(
            NewPatchSizeCache(face, init_font_brotli_quality)),
        glyph_closure_cache(face),
        original_face(common::make_hb_face(hb_face_reference(face))),
        segmentation_info_(std::make_unique<RequestedSegmentationInformation>(
          segments, initial_segment, glyph_closure_cache, unmapped_glyph_handling)),
        dependency_closure_(std::nullopt),
        glyph_condition_set(hb_face_get_glyph_count(face)),
        glyph_groupings(hb_face_get_glyph_count(face)),
        brotli_quality_(brotli_quality),
        condition_analysis_mode_(condition_analysis_mode) {}
 public:
  unsigned BrotliQuality() const { return brotli_quality_; }

  // Convert the information in this context into a finalized GlyphSegmentation
  // representation.
  absl::StatusOr<GlyphSegmentation> ToGlyphSegmentation() const {
    GlyphSegmentation segmentation =
        TRY(glyph_groupings.ToGlyphSegmentation(*segmentation_info_));
    TRYV(ValidateSegmentation(segmentation));
    return segmentation;
  }

  void LogClosureStatistics() const {
    uint64_t dep_graph_closures = 0;
    uint64_t dep_graph_inaccurate = 0;
    if (dependency_closure_.has_value()) {
      dep_graph_closures = (*dependency_closure_)->AccurateResults() * 2;
      dep_graph_inaccurate = (*dependency_closure_)->InaccurateResults();
    }

    uint64_t potential_closures =
      glyph_closure_cache.CacheHits() + glyph_closure_cache.CacheMisses() +
      dep_graph_closures;

    double hb_subset_rate = 100.0 * ((double) glyph_closure_cache.CacheMisses() / (double) potential_closures);
    double cache_hit_rate = 100.0 * ((double) glyph_closure_cache.CacheHits() / (double) potential_closures);
    double dep_graph_hit_rate = 100.0 * ((double) dep_graph_closures / (double) potential_closures);
    uint64_t other_closures = glyph_closure_cache.CacheHits() + glyph_closure_cache.CacheMisses() - (2 * dep_graph_inaccurate);

    VLOG(0) << ">> Of " << potential_closures << " potential closure operations:" << std::endl
      << "  " << glyph_closure_cache.CacheMisses() << " (" << hb_subset_rate << "%)  were handled by hb-subset-plan" << std::endl
      << "  " << dep_graph_closures
      << " (" << dep_graph_hit_rate << "%) were handled by dep graph" << std::endl
      << "  " << glyph_closure_cache.CacheHits() << " (" << cache_hit_rate << "%) were provided by the cache" << std::endl
      << "  " << other_closures << " were from something other than AnalyzeSegment()" << std::endl;
  }

  const common::SegmentSet& InertSegments() const { return inert_segments_; }

  const RequestedSegmentationInformation& SegmentationInfo() const {
    return *segmentation_info_;
  }

  ConditionAnalysisMode GetConditionAnalysisMode() const {
    return condition_analysis_mode_;
  }

  // Assign a new merged segment to base and clear all of the segments that
  // were merged into it.
  uint32_t AssignMergedSegment(segment_index_t base,
                               const common::SegmentSet& to_merge,
                               const Segment& merged_segment, bool is_inert) {
    unsigned count =
        segmentation_info_->AssignMergedSegment(base, to_merge, merged_segment);
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
  absl::Status InvalidateGlyphInformation(const common::GlyphSet& glyphs,
                                  const common::SegmentSet& segments) {
    // TODO(garretrieger): now that invalidation here is only for glyph
    // condition set we should consider changing this so that invalidation is
    // internal to glyph condition set reprocessing (like with GroupGlyphs).
    //
    // Note: glyph_groupings will be automatically invalidated as needed when
    // group glyphs is called.
    glyph_condition_set.InvalidateGlyphInformation(glyphs, segments);

    if (dependency_closure_.has_value()) {
      return (*dependency_closure_)->SegmentsChanged(false, segments);
    }
    return absl::OkStatus();
  }

  /*
   * Invalidates all grouping information and fully reprocesses all segments.
   */
  absl::Status ReassignInitSubset(SubsetDefinition new_def);

  // Performs a closure analysis on codepoints and returns the associated
  // and, or, and exclusive glyph sets.
  absl::Status AnalyzeSegment(const common::SegmentSet& segment_ids,
                              common::GlyphSet& and_gids,
                              common::GlyphSet& or_gids,
                              common::GlyphSet& exclusive_gids);

  // Generates updated glyph conditions and glyph groupings for segment_index
  // which has the provided set of codepoints.
  absl::StatusOr<common::GlyphSet> ReprocessSegment(
      segment_index_t segment_index);

  // Update the glyph groups for 'glyphs'.
  //
  // The glyph condition set must be up to date and fully computed prior to
  // calling this.
  absl::Status GroupGlyphs(const common::GlyphSet& glyphs,
                           const common::SegmentSet& modified_segments) {

    std::optional<DependencyClosure*> maybe_dep_closure = std::nullopt;
    if (dependency_closure_.has_value()) {
      maybe_dep_closure = (*dependency_closure_).get();
    }
    return glyph_groupings.GroupGlyphs(*segmentation_info_, glyph_condition_set,
                                       glyph_closure_cache, maybe_dep_closure,
                                       glyphs, modified_segments);
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

  static std::unique_ptr<PatchSizeCache> NewPatchSizeCache(
      hb_face_t* face, uint32_t brotli_quality) {
    if (brotli_quality == 0) {
      auto cache = EstimatedPatchSizeCache::New(face);
      if (cache.ok()) {
        return std::move(*cache);
      }
    }
    return std::unique_ptr<PatchSizeCache>(
        new PatchSizeCacheImpl(face, brotli_quality));
  }

 public:
  // Caches and logging
  std::unique_ptr<PatchSizeCache> patch_size_cache;
  std::unique_ptr<PatchSizeCache> patch_size_cache_for_init_font;
  GlyphClosureCache glyph_closure_cache;

  // Init
  common::hb_face_unique_ptr original_face;

 private:
  std::unique_ptr<RequestedSegmentationInformation> segmentation_info_;
  std::optional<std::unique_ptr<DependencyClosure>> dependency_closure_;

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

  ConditionAnalysisMode condition_analysis_mode_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_SEGMENTATION_CONTEXT_H_