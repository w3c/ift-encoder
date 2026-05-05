#ifndef IFT_ENCODER_REQUESTED_SEGMENTATION_INFORMATION_H_
#define IFT_ENCODER_REQUESTED_SEGMENTATION_INFORMATION_H_

#include <memory>

#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/config/common.pb.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/init_subset_defaults.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"

namespace ift::encoder {

/*
 * Stores basic information about the configuration of a requested segmentation
 */
class RequestedSegmentationInformation {
 public:
  static absl::StatusOr<std::unique_ptr<RequestedSegmentationInformation>>
  Create(std::vector<Segment> segments, SubsetDefinition init_font_segment,
         GlyphClosureCache& closure_cache,
         ift::config::UnmappedGlyphHandling unmapped_glyph_handling);

 private:
  RequestedSegmentationInformation(
      std::vector<Segment> segments, SubsetDefinition init_font_segment,
      ift::config::UnmappedGlyphHandling unmapped_glyph_handling);

 public:
  // Merge all of the segments in to_merge into base, assigned it
  // a new subset definition "merged_segment".
  uint32_t AssignMergedSegment(segment_index_t base,
                               const ift::common::SegmentSet& to_merge,
                               const Segment& merged_segment) {
    auto& base_segment = segments_[base];
    base_segment = merged_segment;
    for (segment_index_t s : to_merge) {
      // To avoid changing the indices of other segments set the ones we're
      // removing to empty sets. That effectively disables them.
      segments_[s].Clear();
    }
    return base_segment.Definition().codepoints.size();
  }

  absl::Status ReassignInitSubset(GlyphClosureCache& closure_cache,
                                  const SubsetDefinition& new_def) {
    init_font_segment_ = TRY(closure_cache.ExpandClosure(new_def));

    full_definition_.Union(init_font_segment_);
    {
      auto closure = closure_cache.GlyphClosure(full_definition_);
      if (closure.ok()) {
        full_closure_ = std::move(*closure);
      }
    }

    // Changing the init font subset may have caused additional codepoints to be
    // moved to the init font. We need to update the segment definitions to
    // remove these.
    for (auto& s : segments_) {
      // TODO XXXXX this also needs to handle features?
      if (s.Definition().codepoints.intersects(init_font_segment_.codepoints)) {
        s.Definition().codepoints.subtract(init_font_segment_.codepoints);
      }
    }
    return absl::OkStatus();
  }

  ift::config::UnmappedGlyphHandling GetUnmappedGlyphHandling() const {
    return unmapped_glyph_handling_;
  }

  const SubsetDefinition& InitFontSegment() const { return init_font_segment_; }

  // Returns the init font segment with all default always included items
  // removed
  //
  // this is useful when we need to know what non-default items are included
  // in the init font segment.
  const SubsetDefinition InitFontSegmentWithoutDefaults() const {
    SubsetDefinition result = init_font_segment_;
    SubsetDefinition defaults;
    AddInitSubsetDefaults(defaults);
    result.Subtract(defaults);
    return result;
  }

  const ift::common::GlyphSet& InitFontGlyphs() const {
    return init_font_segment_.gids;
  }
  ift::common::GlyphSet NonInitFontGlyphs() const {
    ift::common::GlyphSet out = full_closure_;
    out.subtract(InitFontGlyphs());
    return out;
  }

  const ift::common::GlyphSet& FullClosure() const { return full_closure_; }

  const SubsetDefinition& FullDefinition() const { return full_definition_; }

  bool SegmentsAreDisjoint() const { return segments_disjoint_; }

  const std::vector<Segment>& Segments() const { return segments_; }

  const std::vector<SubsetDefinition> SegmentSubsetDefinitions() const {
    std::vector<SubsetDefinition> out;
    for (const auto& s : segments_) {
      out.push_back(s.Definition());
    }
    return out;
  }

  ift::common::SegmentSet NonEmptySegments() const {
    // TODO(garretrieger): consider caching this value.
    ift::common::SegmentSet segments;
    segment_index_t index = 0;
    for (const auto& s : Segments()) {
      if (!s.Definition().Empty()) {
        segments.insert(index);
      }
      index++;
    }
    return segments;
  }

  SubsetDefinition CombinedDefinition(
      const ift::common::SegmentSet& segments) const {
    SubsetDefinition def;
    for (segment_index_t s : segments) {
      def.Union(Segments().at(s).Definition());
    }

    // Init font subset definition must be part of the closure input
    // since it contributes to reachability of things.
    def.Union(InitFontSegment());

    return def;
  }

 private:
  std::vector<Segment> segments_;
  SubsetDefinition init_font_segment_;
  SubsetDefinition full_definition_;
  ift::common::GlyphSet full_closure_;
  bool segments_disjoint_;
  enum ift::config::UnmappedGlyphHandling unmapped_glyph_handling_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_REQUESTED_SEGMENTATION_INFORMATION_H_