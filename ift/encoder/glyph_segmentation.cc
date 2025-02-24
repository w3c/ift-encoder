#include "ift/encoder/glyph_segmentation.h"

#include <cstdint>
#include <cstdio>
#include <sstream>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "common/compat_id.h"
#include "common/font_data.h"
#include "common/font_helper.h"
#include "common/hb_set_unique_ptr.h"

using absl::btree_map;
using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using common::CompatId;
using common::FontData;
using common::FontHelper;
using common::hb_face_unique_ptr;
using common::hb_set_unique_ptr;
using common::make_hb_face;
using common::make_hb_set;
using common::to_hash_set;

namespace ift::encoder {

Status GlyphSegmentation::GroupsToSegmentation(
    const btree_map<btree_set<segment_index_t>, btree_set<glyph_id_t>>&
        and_glyph_groups,
    const btree_map<btree_set<segment_index_t>, btree_set<glyph_id_t>>&
        or_glyph_groups,
    const btree_set<segment_index_t>& fallback_group,
    std::vector<segment_index_t>& patch_id_to_segment_index,
    GlyphSegmentation& segmentation) {
  patch_id_t next_id = 0;
  std::vector<patch_id_t> segment_to_patch_id;

  // Map segments into patch ids
  for (const auto& [and_segments, glyphs] : and_glyph_groups) {
    if (and_segments.size() != 1) {
      continue;
    }

    segment_index_t segment = *and_segments.begin();
    segmentation.patches_.insert(std::pair(next_id, glyphs));
    segmentation.conditions_.insert(
        ActivationCondition::and_patches({next_id}, next_id));

    if (segment + 1 > segment_to_patch_id.size()) {
      uint32_t size = segment_to_patch_id.size();
      for (uint32_t i = 0; i < (segment + 1) - size; i++) {
        segment_to_patch_id.push_back(-1);
      }
    }

    patch_id_to_segment_index.push_back(segment);
    segment_to_patch_id[segment] = next_id++;
  }

  for (const auto& [and_segments, glyphs] : and_glyph_groups) {
    if (and_segments.size() == 1) {
      // already processed above
      continue;
    }

    btree_set<patch_id_t> and_patches;
    for (segment_index_t segment : and_segments) {
      if (segment_to_patch_id[segment] == -1) {
        return absl::InternalError(StrCat(
            "Segment s", segment,
            " does not have an assigned patch id (found in an and_segment)."));
      }
      and_patches.insert(segment_to_patch_id[segment]);
    }

    segmentation.patches_.insert(std::pair(next_id, glyphs));
    segmentation.conditions_.insert(
        ActivationCondition::and_patches(and_patches, next_id));

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
    btree_set<patch_id_t> or_patches;
    for (segment_index_t segment : or_segments) {
      if (segment_to_patch_id[segment] == -1) {
        return absl::InternalError(StrCat(
            "Segment s", segment,
            " does not have an assigned patch id (found in an or_segment)."));
      }

      if (!or_patches.insert(segment_to_patch_id[segment]).second) {
        return absl::InternalError(
            StrCat("Two different segments are mapped to the same patch: s",
                   segment, " -> p", segment_to_patch_id[segment]));
      }
    }
    bool is_fallback = (or_segments == fallback_group);
    segmentation.patches_.insert(std::pair(next_id, glyphs));
    segmentation.conditions_.insert(
        ActivationCondition::or_patches(or_patches, next_id, is_fallback));

    next_id++;
  }

  return absl::OkStatus();
}

GlyphSegmentation::ActivationCondition
GlyphSegmentation::ActivationCondition::and_patches(
    const absl::btree_set<patch_id_t>& ids, patch_id_t activated) {
  ActivationCondition conditions;
  conditions.activated_ = activated;

  for (auto id : ids) {
    conditions.conditions_.push_back({id});
  }

  return conditions;
}

GlyphSegmentation::ActivationCondition
GlyphSegmentation::ActivationCondition::or_patches(
    const absl::btree_set<patch_id_t>& ids, patch_id_t activated,
    bool is_fallback) {
  ActivationCondition conditions;
  conditions.activated_ = activated;
  conditions.conditions_.push_back(ids);
  conditions.is_fallback_ = is_fallback;

  return conditions;
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

std::string GlyphSegmentation::ActivationCondition::ToString() const {
  std::stringstream out;
  out << "if (";
  bool first = true;
  for (const auto& set : conditions()) {
    if (!first) {
      out << " AND ";
    } else {
      first = false;
    }

    if (set.size() > 1) {
      out << "(";
    }
    bool first_inner = true;
    for (uint32_t id : set) {
      if (!first_inner) {
        out << " OR ";
      } else {
        first_inner = false;
      }
      out << "p" << id;
    }
    if (set.size() > 1) {
      out << ")";
    }
  }
  out << ") then p" << activated();
  return out.str();
}

std::string GlyphSegmentation::ToString() const {
  std::stringstream out;
  out << "initial font: ";
  output_set("gid", InitialFontGlyphs().begin(), InitialFontGlyphs().end(),
             out);
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

bool GlyphSegmentation::ActivationCondition::operator<(
    const ActivationCondition& other) const {
  if (conditions_.size() != other.conditions_.size()) {
    return conditions_.size() < other.conditions_.size();
  }

  auto a = conditions_.begin();
  auto b = other.conditions_.begin();
  while (a != conditions_.end() && b != other.conditions_.end()) {
    if (a->size() != b->size()) {
      return a->size() < b->size();
    }

    auto aa = a->begin();
    auto bb = b->begin();
    while (aa != a->end() && bb != b->end()) {
      if (*aa != *bb) {
        return *aa < *bb;
      }
      aa++;
      bb++;
    }

    a++;
    b++;
  }

  if (activated_ != other.activated_) {
    return activated_ < other.activated_;
  }

  // These two are equal
  return false;
}

}  // namespace ift::encoder