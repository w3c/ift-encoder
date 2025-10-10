#include "ift/encoder/glyph_segmentation.h"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <sstream>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "common/font_helper.h"
#include "common/int_set.h"
#include "ift/encoder/subset_definition.h"
#include "ift/proto/patch_encoding.h"
#include "ift/proto/patch_map.h"

using absl::btree_map;
using absl::btree_set;
using absl::flat_hash_map;
using absl::Span;
using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using common::CodepointSet;
using common::GlyphSet;
using common::IntSet;
using common::SegmentSet;
using ift::proto::PatchEncoding;
using ift::proto::PatchMap;

namespace ift::encoder {

Status GlyphSegmentation::GroupsToSegmentation(
    const btree_map<SegmentSet, GlyphSet>& and_glyph_groups,
    const btree_map<SegmentSet, GlyphSet>& or_glyph_groups,
    const btree_map<segment_index_t, GlyphSet>& exclusive_glyph_groups,
    const SegmentSet& fallback_group, GlyphSegmentation& segmentation) {
  patch_id_t next_id = 0;
  segmentation.patches_.clear();
  segmentation.conditions_.clear();

  // Map segments into patch ids
  for (const auto& [segment, glyphs] : exclusive_glyph_groups) {
    segmentation.patches_.insert(std::pair(next_id, glyphs));
    segmentation.conditions_.insert(
        ActivationCondition::exclusive_segment(segment, next_id++));
  }

  for (const auto& [and_segments, glyphs] : and_glyph_groups) {
    segmentation.patches_.insert(std::pair(next_id, glyphs));
    segmentation.conditions_.insert(
        ActivationCondition::and_segments(and_segments, next_id));

    next_id++;
  }

  for (const auto& [or_segments, glyphs] : or_glyph_groups) {
    if (glyphs.empty()) {
      // Some or_segments have all of their glyphs removed by the additional
      // conditions check, don't create a patch for these.
      continue;
    }

    if (or_segments.size() == 1) {
      return absl::InternalError(
          StrCat("Unexpected or_segment with only one segment: s",
                 *or_segments.begin()));
    }

    bool is_fallback = (or_segments == fallback_group);
    segmentation.patches_.insert(std::pair(next_id, glyphs));
    segmentation.conditions_.insert(
        ActivationCondition::or_segments(or_segments, next_id, is_fallback));

    next_id++;
  }

  return absl::OkStatus();
}

template <typename It>
void output_set_inner(const char* prefix, const char* seperator, It begin,
                      It end, std::stringstream& out) {
  bool first = true;
  while (begin != end) {
    if (!first) {
      out << ", ";
    } else {
      first = false;
    }
    out << prefix << *(begin++);
  }
}

template <typename It>
void output_set(const char* prefix, It begin, It end, std::stringstream& out) {
  if (begin == end) {
    out << "{}";
    return;
  }

  out << "{ ";
  output_set_inner(prefix, ", ", begin, end, out);
  out << " }";
}

std::string GlyphSegmentation::ToString() const {
  std::stringstream out;
  out << "initial font: ";
  output_set("gid", InitialFontGlyphClosure().begin(),
             InitialFontGlyphClosure().end(), out);
  out << std::endl;

  for (const auto& [segment_id, gids] : GidSegments()) {
    out << "p" << segment_id << ": ";
    output_set("gid", gids.begin(), gids.end(), out);
    out << std::endl;
  }

  for (const auto& condition : Conditions()) {
    out << condition.ToString() << std::endl;
  }

  return out.str();
}

template <typename ProtoType>
ProtoType ToSetProto(const IntSet& set) {
  ProtoType values;
  for (uint32_t v : set) {
    values.add_values(v);
  }
  return values;
}

template <typename ProtoType>
ProtoType TagsToSetProto(const btree_set<hb_tag_t>& set) {
  ProtoType values;
  for (uint32_t tag : set) {
    values.add_values(common::FontHelper::ToString(tag));
  }
  return values;
}

SegmentationPlan GlyphSegmentation::ToSegmentationPlanProto() const {
  SegmentationPlan config;

  uint32_t set_index = 0;
  for (const auto& s : Segments()) {
    if (!s.Empty()) {
      SegmentProto segment_proto;
      (*segment_proto.mutable_codepoints()) =
          ToSetProto<Codepoints>(s.codepoints);
      (*segment_proto.mutable_features()) =
          TagsToSetProto<Features>(s.feature_tags);
      (*config.mutable_segments())[set_index++] = segment_proto;
    } else {
      set_index++;
    }
  }

  for (const auto& [patch_id, gids] : GidSegments()) {
    (*config.mutable_glyph_patches())[patch_id] = ToSetProto<Glyphs>(gids);
  }

  for (const auto& c : Conditions()) {
    *config.add_glyph_patch_conditions() = c.ToConfigProto();
  }

  *config.mutable_initial_codepoints() =
      ToSetProto<Codepoints>(InitialFontSegment().codepoints);
  *config.mutable_initial_features() =
      TagsToSetProto<Features>(InitialFontSegment().feature_tags);

  if (!InitialFontSegment().gids.empty()) {
    *config.mutable_initial_glyphs() =
        ToSetProto<Glyphs>(InitialFontSegment().gids);
  }

  return config;
}

void GlyphSegmentation::CopySegments(
    const std::vector<SubsetDefinition>& segments) {
  segments_.clear();
  for (const auto& set : segments) {
    segments_.push_back(set);
  }
}

}  // namespace ift::encoder
