#ifndef IFT_ENCODER_REQUESTED_SEGMENTATION_INFORMATION_H_
#define IFT_ENCODER_REQUESTED_SEGMENTATION_INFORMATION_H_

#include <memory>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "ift/common/int_set.h"
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
         GlyphClosureCache& closure_cache);

 private:
  RequestedSegmentationInformation(std::vector<Segment> segments,
                                   SubsetDefinition init_font_segment);

 public:
  // Merge all of the segments in to_merge into base, assigned it
  // a new subset definition "merged_segment".
  uint32_t AssignMergedSegment(segment_index_t base,
                               const ift::common::SegmentSet& to_merge,
                               const Segment& merged_segment) {
    auto& base_segment = segments_[base];
    RemoveFromIndex(base, base_segment.Definition().codepoints);
    base_segment = merged_segment;
    AddToIndex(base, base_segment.Definition().codepoints);

    for (segment_index_t s : to_merge) {
      // To avoid changing the indices of other segments set the ones we're
      // removing to empty sets. That effectively disables them.
      RemoveFromIndex(s, segments_[s].Definition().codepoints);
      segments_[s].Clear();
    }
    return base_segment.Definition().codepoints.size();
  }

  void ResetSegments(std::vector<Segment> segments);

  absl::Status ReassignInitSubset(GlyphClosureCache& closure_cache,
                                  const SubsetDefinition& new_def);

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
  const ift::common::GlyphSet& NonInitFontGlyphs() const {
    return non_init_font_glyphs_;
  }
  const ift::common::GlyphSet& NonInitFontNonEmptyGlyphs() const {
    return non_init_font_non_empty_glyphs_;
  }

  const ift::common::GlyphSet& FullClosure() const { return full_closure_; }
  const ift::common::CodepointSet& FullCodepointClosure() const {
    return full_codepoint_closure_;
  }

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

  ift::common::SegmentSet SegmentsForCodepoints(
      const ift::common::CodepointSet& codepoints) const;

 private:
  void AddToIndex(segment_index_t seg_idx,
                  const ift::common::CodepointSet& codepoints);
  void RemoveFromIndex(segment_index_t seg_idx,
                       const ift::common::CodepointSet& codepoints);

  std::vector<Segment> segments_;
  SubsetDefinition init_font_segment_;
  SubsetDefinition full_definition_;
  ift::common::GlyphSet full_closure_;
  ift::common::CodepointSet full_codepoint_closure_;
  ift::common::GlyphSet checked_empty_glyphs_;
  ift::common::GlyphSet empty_glyphs_;
  ift::common::GlyphSet non_init_font_glyphs_;
  ift::common::GlyphSet non_init_font_non_empty_glyphs_;

  bool segments_disjoint_;
  absl::flat_hash_map<hb_codepoint_t, ift::common::SegmentSet>
      codepoint_to_segments_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_REQUESTED_SEGMENTATION_INFORMATION_H_