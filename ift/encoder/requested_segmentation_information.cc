#include "ift/encoder/requested_segmentation_information.h"

#include <vector>

#include "ift/encoder/segment.h"

namespace ift::encoder {

RequestedSegmentationInformation::RequestedSegmentationInformation(
    std::vector<Segment> segments, SubsetDefinition init_font_segment,
    GlyphClosureCache& closure_cache)
    : segments_(std::move(segments)),
      init_font_segment_(std::move(init_font_segment)) {
  SubsetDefinition all;
  all.Union(init_font_segment_);
  for (const auto& s : segments_) {
    all.Union(s.Definition());
  }

  {
    auto closure = closure_cache.GlyphClosure(init_font_segment_);
    if (closure.ok()) {
      init_font_glyphs_ = std::move(*closure);
    }
  }

  auto closure = closure_cache.GlyphClosure(all);
  if (closure.ok()) {
    full_closure_ = std::move(*closure);
  }
}

}  // namespace ift::encoder