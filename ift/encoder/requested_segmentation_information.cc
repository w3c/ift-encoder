#include "ift/encoder/requested_segmentation_information.h"

#include <vector>

#include "ift/encoder/segment.h"

namespace ift::encoder {

RequestedSegmentationInformation::RequestedSegmentationInformation(
    std::vector<Segment> segments, SubsetDefinition init_font_segment,
    GlyphClosureCache& closure_cache)
    : segments_(std::move(segments)), init_font_segment_() {
  ReassignInitSubset(closure_cache, std::move(init_font_segment));
}

}  // namespace ift::encoder