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

  /*
   * Generates a segmentation context for the provided segmentation input.
   *
   * This context will contain the initial groupings without doing any merging.
   * Useful for writing tests that require a initialized segmentation context.
   */
  absl::StatusOr<SegmentationContext> InitializeSegmentationContext(
      hb_face_t* face, SubsetDefinition initial_segment,
      std::vector<Segment> segments, MergeStrategy merge_strategy) const;

  /*
   * Computes the total cost (expected number of bytes transferred) for a given
   * segmentation with respect to the provided frequency data.
   */
  absl::StatusOr<SegmentationCost> TotalCost(
      hb_face_t* original_face, const GlyphSegmentation& segmentation,
      const freq::ProbabilityCalculator& probability_calculator) const;

 private:
  absl::Status MoveSegmentsToInitFont(SegmentationContext& context) const;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_CLOSURE_GLYPH_SEGMENTER_H_