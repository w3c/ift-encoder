#ifndef IFT_ENCODER_GLYPH_SEGMENTER_H_
#define IFT_ENCODER_GLYPH_SEGMENTER_H_

#include "ift/encoder/glyph_segmentation.h"

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"

namespace ift::encoder {

/*
 * A glyph segmenter takes a desired unicode code point based segmentation and produces
 * the a glyph based segmentation which utilizes the code point segments as the conditions
 * to trigger the load of the glyph segments.
 *
 * The produced glyph segmentation will satisfy the glyph closure requirement.
 */
class GlyphSegmenter {
  public:
   virtual ~GlyphSegmenter() = default;

   virtual absl::StatusOr<GlyphSegmentation> CodepointToGlyphSegments(
      hb_face_t* face, absl::flat_hash_set<hb_codepoint_t> initial_segment,
      std::vector<absl::flat_hash_set<hb_codepoint_t>> codepoint_segments,
      uint32_t patch_size_min_bytes = 0,
      uint32_t patch_size_max_bytes = UINT32_MAX) const = 0;
};

}  // namespace ift::encoder

#endif