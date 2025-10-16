#ifndef IFT_ENCODER_REQUESTED_SEGMENTATION_INFORMATION_H_
#define IFT_ENCODER_REQUESTED_SEGMENTATION_INFORMATION_H_

#include "common/font_helper.h"
#include "common/int_set.h"
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
  RequestedSegmentationInformation(std::vector<Segment> segments,
                                   SubsetDefinition init_font_segment,
                                   GlyphClosureCache& closure_cache);

  // Merge all of the segments in to_merge into base, assigned it
  // a new subset definition "merged_segment".
  uint32_t AssignMergedSegment(segment_index_t base,
                               const common::SegmentSet& to_merge,
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

  void ReassignInitSubset(GlyphClosureCache& closure_cache,
                          SubsetDefinition new_def) {
    init_font_segment_ = std::move(new_def);

    SubsetDefinition all;
    all.Union(init_font_segment_);
    for (const auto& s : segments_) {
      all.Union(s.Definition());
    }

    {
      auto closure = closure_cache.GlyphClosure(all);
      if (closure.ok()) {
        full_closure_ = std::move(*closure);
      }
    }

    while (ExpandInitClosure(closure_cache)) {
      // Expand the init closure until it stops changing.
    }

    // Changing the init font subset may have caused additional codepoints to be
    // moved to the init font. We need to update the segment definitions to
    // remove these.
    for (auto& s : segments_) {
      s.Definition().codepoints.subtract(init_font_segment_.codepoints);
    }
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

  const common::GlyphSet& InitFontGlyphs() const { return init_font_glyphs_; }

  const common::GlyphSet& FullClosure() const { return full_closure_; }

  const std::vector<Segment>& Segments() const { return segments_; }

  const std::vector<SubsetDefinition> SegmentSubsetDefinitions() const {
    std::vector<SubsetDefinition> out;
    for (const auto& s : segments_) {
      out.push_back(s.Definition());
    }
    return out;
  }

 private:
  // Recomputes the init font glyph and codepoint closure, returns true
  // if the init font segment was modified.
  bool ExpandInitClosure(GlyphClosureCache& closure_cache) {
    auto closure = closure_cache.GlyphClosure(init_font_segment_);
    if (closure.ok()) {
      init_font_glyphs_ = std::move(*closure);
    }

    // Changing the init font subset may have caused additional codepoints to be
    // moved to the init font. We need to update the segment definitions to
    // remove these.
    common::CodepointSet init_font_codepoints =
        common::FontHelper::GidsToUnicodes(closure_cache.Face(),
                                           init_font_glyphs_);

    bool changed = false;
    if (!init_font_glyphs_.is_subset_of(init_font_segment_.gids)) {
      changed = true;
      init_font_segment_.gids.union_set(init_font_glyphs_);
    }

    if (!init_font_codepoints.is_subset_of(init_font_segment_.codepoints)) {
      changed = true;
      init_font_segment_.codepoints.union_set(init_font_codepoints);
    }

    return changed;
  }

  std::vector<Segment> segments_;
  SubsetDefinition init_font_segment_;
  common::GlyphSet init_font_glyphs_;
  common::GlyphSet full_closure_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_REQUESTED_SEGMENTATION_INFORMATION_H_