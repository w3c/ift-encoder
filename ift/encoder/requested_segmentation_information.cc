#include "ift/encoder/requested_segmentation_information.h"

#include <vector>

#include "ift/encoder/segment.h"

namespace ift::encoder {

RequestedSegmentationInformation::RequestedSegmentationInformation(
    std::vector<Segment> segments, SubsetDefinition init_font_segment,
    GlyphClosureCache& closure_cache)
    : segments_(std::move(segments)), init_font_segment_() {
  ReassignInitSubset(closure_cache, std::move(init_font_segment));

  segments_disjoint_ = true;

  full_definition_ = init_font_segment_;
  for (const auto& s : segments_) {
    const auto& def = s.Definition();
    if (segments_disjoint_) {
      for (hb_tag_t tag : def.feature_tags) {
        if (full_definition_.feature_tags.contains(tag)) {
          segments_disjoint_ = false;
        }
      }
      segments_disjoint_ =
          segments_disjoint_ &&
          !full_definition_.codepoints.intersects(def.codepoints);
    }
    full_definition_.Union(s.Definition());
  }
}

}  // namespace ift::encoder