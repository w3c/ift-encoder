#include "ift/encoder/requested_segmentation_information.h"

#include <memory>
#include <vector>

#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"

using absl::StatusOr;

namespace ift::encoder {

static bool CheckSegmentsAreDisjoint(const SubsetDefinition& init_segment,
                                     const std::vector<Segment>& segments) {
  bool segments_disjoint = true;
  SubsetDefinition full_definition = init_segment;
  for (const auto& s : segments) {
    const auto& def = s.Definition();
    if (segments_disjoint) {
      for (hb_tag_t tag : def.feature_tags) {
        if (full_definition.feature_tags.contains(tag)) {
          segments_disjoint = false;
        }
      }
      segments_disjoint =
          segments_disjoint &&
          !full_definition.codepoints.intersects(def.codepoints);
    }
    full_definition.Union(s.Definition());
  }
  return segments_disjoint;
}

StatusOr<std::unique_ptr<RequestedSegmentationInformation>> RequestedSegmentationInformation::Create(
  std::vector<Segment> segments, SubsetDefinition init_font_segment,
  GlyphClosureCache& closure_cache,
  UnmappedGlyphHandling unmapped_glyph_handling) {

  std::unique_ptr<RequestedSegmentationInformation> info(
    new RequestedSegmentationInformation(segments, init_font_segment, unmapped_glyph_handling));
  TRYV(info->ReassignInitSubset(closure_cache, init_font_segment));
  info->segments_disjoint_ = CheckSegmentsAreDisjoint(init_font_segment, info->segments_);
  return info;
}

RequestedSegmentationInformation::RequestedSegmentationInformation(
    std::vector<Segment> segments,
    SubsetDefinition init_font_segment,
    UnmappedGlyphHandling unmapped_glyph_handling)
    : segments_(std::move(segments)),
      init_font_segment_(),
      unmapped_glyph_handling_(unmapped_glyph_handling) {
  // ReassignInitSubset expects full_definition_ is already populated.
  full_definition_ = init_font_segment;
  for (const auto& s : segments_) {
    full_definition_.Union(s.Definition());
  }
}

}  // namespace ift::encoder