#include "ift/encoder/subset_definition.h"

#include <vector>

#include "absl/container/btree_set.h"
#include "common/font_helper.h"
#include "common/int_set.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/proto/patch_encoding.h"
#include "ift/proto/patch_map.h"

using absl::btree_set;
using common::FontHelper;
using common::IntSet;
using ift::proto::PatchEncoding;
using ift::proto::PatchMap;

namespace ift::encoder {

void PrintTo(const SubsetDefinition& def, std::ostream* os) {
  *os << "[{";

  bool first = true;
  for (uint32_t cp : def.codepoints) {
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
  codepoints.subtract(other.codepoints);
  gids.subtract(other.codepoints);
  feature_tags = subtract(feature_tags, other.feature_tags);
  design_space = subtract(design_space, other.design_space);
}

void SubsetDefinition::Union(const SubsetDefinition& other) {
  codepoints.union_set(other.codepoints);
  gids.union_set(other.gids);
  feature_tags.insert(other.feature_tags.begin(), other.feature_tags.end());

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
  codepoints.union_into(hb_subset_input_unicode_set(input));

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

  gids.union_into(hb_subset_input_glyph_set(input));
}

/*
XXXXXX
PatchMap::Coverage SubsetDefinition::ToCoverage() const {
  PatchMap::Coverage coverage;
  coverage.codepoints = codepoints;
  coverage.features = feature_tags;
  for (const auto& [tag, range] : design_space) {
    coverage.design_space[tag] = range;
  }
  return coverage;
}
  */

std::vector<PatchMap::Entry> SubsetDefinition::ToEntries(
    PatchEncoding encoding, uint32_t last_patch_id, uint32_t next_entry_index,
    std::vector<uint32_t> patch_ids) const {
  std::vector<PatchMap::Entry> entries;

  if (!codepoints.empty()) {
    PatchMap::Entry entry;
    entry.encoding = encoding;
    entry.coverage.codepoints = codepoints;
    entries.push_back(entry);
  }

  if (!feature_tags.empty()) {
    PatchMap::Entry entry;
    entry.encoding = encoding;
    entry.coverage.features = feature_tags;
    entries.push_back(entry);
  }

  if (!design_space.empty()) {
    PatchMap::Entry entry;
    entry.encoding = encoding;
    entry.coverage.design_space.insert(design_space.begin(),
                                       design_space.end());
    entries.push_back(entry);
  }

  if (entries.size() > 1) {
    // Use a new entry to disjuntively match all of the entries from above.
    PatchMap::Entry entry;
    entry.coverage.conjunctive = false;
    entry.encoding = encoding;

    for (auto& e : entries) {
      entry.coverage.child_indices.insert(next_entry_index++);
      e.ignored = true;
      e.patch_indices.push_back(++last_patch_id);
    }
    entries.push_back(entry);
  }

  // Last entry is the one that maps the patch ids
  auto& e = entries.back();
  e.patch_indices = patch_ids;
  e.ignored = false;

  return entries;
}

}  // namespace ift::encoder