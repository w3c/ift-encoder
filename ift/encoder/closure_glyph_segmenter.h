#ifndef IFT_ENCODER_CLOSURE_GLYPH_SEGMENTER_H_
#define IFT_ENCODER_CLOSURE_GLYPH_SEGMENTER_H_

#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/merge_strategy.h"
#include "ift/encoder/segmentation_context.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/probability_calculator.h"
#include "util/common.pb.h"
#include "util/segmenter_config.pb.h"

namespace ift::encoder {

struct SegmentationCost {
  double total_cost;
  double cost_for_non_segmented;
  double ideal_cost;
};

/*
 * This generates a glyph segmentation of a font which satisifies the closure
 * requirement by utilizing a a font subsetter closure function to detect glyph
 * dependencies in the font.
 *
 * This is highly experimental and work in progress code that aims to explore
 * and validate the approach of using a subsetter closure function to generate
 * glyph segmentations.
 *
 * More details about this specific approach can be found in:
 * ../../docs/experimental/closure_glyph_segmentation.md.
 */
class ClosureGlyphSegmenter {
 public:
  ClosureGlyphSegmenter(uint32_t brotli_quality,
                        uint32_t init_font_merging_brotli_quality,
                        UnmappedGlyphHandling unmapped_glyph_handling,
                        ConditionAnalysisMode condition_analysis_mode)
      : brotli_quality_(brotli_quality),
        init_font_merging_brotli_quality_(init_font_merging_brotli_quality),
        unmapped_glyph_handling_(unmapped_glyph_handling),
        condition_analysis_mode_(condition_analysis_mode) {}

  /*
   * Analyzes a set of codepoint segments using a subsetter closure and computes
   * a GlyphSegmentation which will satisfy the "glyph closure requirement" for
   * the provided font face.
   *
   * initial_segment is the set of codepoints that will be placed into the
   * initial ift font.
   */
  absl::StatusOr<GlyphSegmentation> CodepointToGlyphSegments(
      hb_face_t* face, SubsetDefinition initial_segment,
      const std::vector<SubsetDefinition>& subset_definitions,
      std::optional<MergeStrategy> strategy = std::nullopt) const;

  absl::StatusOr<GlyphSegmentation> CodepointToGlyphSegments(
      hb_face_t* face, SubsetDefinition initial_segment,
      const std::vector<SubsetDefinition>& subset_definitions,
      absl::btree_map<common::SegmentSet, MergeStrategy> merge_groups) const;

  /*
   * Generates a segmentation context for the provided segmentation input.
   *
   * This context will contain the initial groupings without doing any merging.
   * Useful for writing tests that require a initialized segmentation context.
   */
  absl::StatusOr<SegmentationContext> InitializeSegmentationContext(
      hb_face_t* face, SubsetDefinition initial_segment,
      std::vector<Segment> segments) const;

  /*
   * Computes the total cost (expected number of bytes transferred) for a given
   * segmentation with respect to the provided frequency data.
   */
  absl::StatusOr<SegmentationCost> TotalCost(
      hb_face_t* original_face, const GlyphSegmentation& segmentation,
      const freq::ProbabilityCalculator& probability_calculator) const;

  /*
   * Computes the total cost of the fallback patch (expected number of bytes
   * transferred)
   */
  absl::Status FallbackCost(hb_face_t* original_face,
                            const GlyphSegmentation& segmentation,
                            uint32_t& fallback_glyphs_size,
                            uint32_t& all_glyphs_size) const;

 private:
  uint32_t brotli_quality_;
  uint32_t init_font_merging_brotli_quality_;
  UnmappedGlyphHandling unmapped_glyph_handling_;
  ConditionAnalysisMode condition_analysis_mode_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_CLOSURE_GLYPH_SEGMENTER_H_