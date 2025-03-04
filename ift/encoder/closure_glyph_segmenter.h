
#ifndef IFT_ENCODER_CLOSURE_GLYPH_SEGMENTER_H_
#define IFT_ENCODER_CLOSURE_GLYPH_SEGMENTER_H_

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "ift/encoder/glyph_segmentation.h"

namespace ift::encoder {

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
      hb_face_t* face, absl::flat_hash_set<hb_codepoint_t> initial_segment,
      std::vector<absl::flat_hash_set<hb_codepoint_t>> codepoint_segments,
      uint32_t patch_size_min_bytes = 0,
      uint32_t patch_size_max_bytes = UINT32_MAX) const;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_CLOSURE_GLYPH_SEGMENTER_H_