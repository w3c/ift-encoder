#include "ift/encoder/subset_definition.h"

#include "absl/container/btree_set.h"
#include "common/font_helper.h"

using absl::btree_set;
using common::FontHelper;
using ift::proto::PatchMap;

namespace ift::encoder {

void PrintTo(const SubsetDefinition& def, std::ostream* os) {
  *os << "[{";

  btree_set<uint32_t> sorted;
  for (uint32_t cp : def.codepoints) {
    sorted.insert(cp);
  }

  bool first = true;
  for (uint32_t cp : sorted) {
    if (!first) {
      *os << ", ";
    }
    first = false;
    *os << cp;
  }

  *os << "}";

  if (!def.design_space.empty()) {
    *os << ", {";
    bool first = true;
    for (const auto& [tag, range] : def.design_space) {
      if (!first) {
        *os << ", ";
      }
      first = false;
      *os << FontHelper::ToString(tag) << ": ";
      PrintTo(range, os);
    }
    *os << "}";
  }

  *os << "]";
}

template <typename S>
S subtract(const S& a, const S& b) {
  S c;
  for (uint32_t v : a) {
    if (!b.contains(v)) {
      c.insert(v);
    }
  }
  return c;
}

design_space_t subtract(const design_space_t& a, const design_space_t& b) {
  design_space_t c;

  for (const auto& [tag, range] : a) {
    auto e = b.find(tag);
    if (e == b.end()) {
      c[tag] = range;
      continue;
    }

    if (e->second.IsPoint()) {
      // range minus a point, does nothing.
      c[tag] = range;
    }

    // TODO(garretrieger): this currently operates only at the axis
    //  level. Partial ranges within an axis are not supported.
    //  to implement this we'll need to subtract the two ranges
    //  from each other. However, this can produce two resulting ranges
    //  instead of one.
    //
    //  It's likely that we'll forbid disjoint ranges, so we can simply
    //  error out if a configuration would result in one.
  }

  return c;
}

void SubsetDefinition::Subtract(const SubsetDefinition& other) {
  codepoints = subtract(codepoints, other.codepoints);
  gids = subtract(gids, other.gids);
  feature_tags = subtract(feature_tags, other.feature_tags);
  design_space = subtract(design_space, other.design_space);
}

void SubsetDefinition::Union(const SubsetDefinition& other) {
  std::copy(other.codepoints.begin(), other.codepoints.end(),
            std::inserter(codepoints, codepoints.begin()));
  std::copy(other.gids.begin(), other.gids.end(),
            std::inserter(gids, gids.begin()));
  std::copy(other.feature_tags.begin(), other.feature_tags.end(),
            std::inserter(feature_tags, feature_tags.begin()));

  for (const auto& [tag, range] : other.design_space) {
    auto existing = design_space.find(tag);
    if (existing == design_space.end()) {
      design_space[tag] = range;
      continue;
    }

    // TODO(garretrieger): this is a simplified implementation that
    //  only allows expanding a point to a range. This needs to be
    //  updated to handle a generic union.
    //
    //  It's likely that we'll forbid disjoint ranges, so we can simply
    //  error out if a configuration would result in one.
    if (existing->second.IsPoint() && range.IsRange()) {
      design_space[tag] = range;
    }
  }
}

void SubsetDefinition::ConfigureInput(hb_subset_input_t* input,
                                      hb_face_t* face) const {
  hb_set_t* unicodes = hb_subset_input_unicode_set(input);
  for (hb_codepoint_t cp : codepoints) {
    hb_set_add(unicodes, cp);
  }

  hb_set_t* features =
      hb_subset_input_set(input, HB_SUBSET_SETS_LAYOUT_FEATURE_TAG);
  for (hb_tag_t tag : feature_tags) {
    hb_set_add(features, tag);
  }

  for (const auto& [tag, range] : design_space) {
    hb_subset_input_set_axis_range(input, face, tag, range.start(), range.end(),
                                   NAN);
  }

  if (gids.empty()) {
    return;
  }

  hb_set_t* gids_set = hb_subset_input_glyph_set(input);
  hb_set_add(gids_set, 0);
  for (hb_codepoint_t gid : gids) {
    hb_set_add(gids_set, gid);
  }
}

PatchMap::Coverage SubsetDefinition::ToCoverage() const {
  PatchMap::Coverage coverage;
  coverage.codepoints = codepoints;
  coverage.features = feature_tags;
  for (const auto& [tag, range] : design_space) {
    coverage.design_space[tag] = range;
  }
  return coverage;
}

}  // namespace ift::encoder