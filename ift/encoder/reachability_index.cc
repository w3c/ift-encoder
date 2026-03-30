#include "ift/encoder/reachability_index.h"

#include "ift/common/int_set.h"

using absl::btree_set;
using ift::common::CodepointSet;
using ift::common::GlyphSet;
using ift::common::SegmentSet;

namespace ift::encoder {

void ReachabilityIndex::AddGlyph(segment_index_t segment, glyph_id_t glyph) {
  segments_by_glyph_[glyph].insert(segment);
  glyphs_by_segment_[segment].insert(glyph);
}

void ReachabilityIndex::AddCodepoint(segment_index_t segment,
                                     hb_codepoint_t codepoint) {
  segments_by_codepoint_[codepoint].insert(segment);
  codepoints_by_segment_[segment].insert(codepoint);
}

void ReachabilityIndex::AddFeature(segment_index_t segment, hb_tag_t feature) {
  segments_by_feature_[feature].insert(segment);
  features_by_segment_[segment].insert(feature);
}

void ReachabilityIndex::ClearSegment(segment_index_t segment) {
  auto glyphs_it = glyphs_by_segment_.find(segment);
  if (glyphs_it != glyphs_by_segment_.end()) {
    for (glyph_id_t gid : glyphs_it->second) {
      auto seg_it = segments_by_glyph_.find(gid);
      if (seg_it == segments_by_glyph_.end()) {
        continue;
      }
      seg_it->second.erase(segment);
      if (seg_it->second.empty()) {
        segments_by_glyph_.erase(seg_it);
      }
    }
    glyphs_by_segment_.erase(glyphs_it);
  }

  auto codepoints_it = codepoints_by_segment_.find(segment);
  if (codepoints_it != codepoints_by_segment_.end()) {
    for (hb_codepoint_t cp : codepoints_it->second) {
      auto seg_it = segments_by_codepoint_.find(cp);
      if (seg_it == segments_by_codepoint_.end()) {
        continue;
      }
      seg_it->second.erase(segment);
      if (seg_it->second.empty()) {
        segments_by_codepoint_.erase(seg_it);
      }
    }
    codepoints_by_segment_.erase(codepoints_it);
  }

  auto features_it = features_by_segment_.find(segment);
  if (features_it != features_by_segment_.end()) {
    for (hb_tag_t tag : features_it->second) {
      auto seg_it = segments_by_feature_.find(tag);
      if (seg_it == segments_by_feature_.end()) {
        continue;
      }
      seg_it->second.erase(segment);
      if (seg_it->second.empty()) {
        segments_by_feature_.erase(seg_it);
      }
    }
    features_by_segment_.erase(features_it);
  }
}

const ift::common::SegmentSet& ReachabilityIndex::SegmentsForGlyph(
    glyph_id_t glyph) const {
  auto it = segments_by_glyph_.find(glyph);
  if (it == segments_by_glyph_.end()) {
    return empty_segments_;
  }
  return it->second;
}

const ift::common::GlyphSet& ReachabilityIndex::GlyphsForSegment(
    segment_index_t segment) const {
  auto it = glyphs_by_segment_.find(segment);
  if (it == glyphs_by_segment_.end()) {
    return empty_glyphs_;
  }
  return it->second;
}

const ift::common::SegmentSet& ReachabilityIndex::SegmentsForCodepoint(
    hb_codepoint_t codepoint) const {
  auto it = segments_by_codepoint_.find(codepoint);
  if (it == segments_by_codepoint_.end()) {
    return empty_segments_;
  }
  return it->second;
}

const ift::common::CodepointSet& ReachabilityIndex::CodepointsForSegment(
    segment_index_t segment) const {
  auto it = codepoints_by_segment_.find(segment);
  if (it == codepoints_by_segment_.end()) {
    return empty_codepoints_;
  }
  return it->second;
}

const ift::common::SegmentSet& ReachabilityIndex::SegmentsForFeature(
    hb_tag_t feature) const {
  auto it = segments_by_feature_.find(feature);
  if (it == segments_by_feature_.end()) {
    return empty_segments_;
  }
  return it->second;
}

const absl::btree_set<hb_tag_t>& ReachabilityIndex::FeaturesForSegment(
    segment_index_t segment) const {
  auto it = features_by_segment_.find(segment);
  if (it == features_by_segment_.end()) {
    return empty_features_;
  }
  return it->second;
}

}  // namespace ift::encoder