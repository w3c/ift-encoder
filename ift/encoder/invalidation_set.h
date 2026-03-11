#ifndef IFT_ENCODER_INVALIDATION_SET_H_
#define IFT_ENCODER_INVALIDATION_SET_H_

#include "ift/common/int_set.h"
#include "ift/encoder/types.h"

namespace ift::encoder {

struct InvalidationSet {
  InvalidationSet() = delete;

  InvalidationSet(segment_index_t base_segment_index)
      : glyphs(),
        segments({base_segment_index}),
        base_segment(base_segment_index) {}

  explicit InvalidationSet(ift::common::GlyphSet glyphs_,
                           ift::common::SegmentSet segments_,
                           segment_index_t base_segment_index)
      : glyphs(glyphs_), segments(segments_), base_segment(base_segment_index) {
    segments.insert(base_segment_index);
  }

  ift::common::GlyphSet glyphs;
  ift::common::SegmentSet segments;
  segment_index_t base_segment;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_INVALIDATION_SET_H_