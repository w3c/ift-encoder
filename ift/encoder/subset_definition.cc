#include "ift/encoder/subset_definition.h"

#include <optional>
#include <vector>

#include "absl/container/btree_set.h"
#include "common/axis_range.h"
#include "common/font_helper.h"
#include "common/int_set.h"
#include "ift/proto/patch_encoding.h"
#include "ift/proto/patch_map.h"

using absl::btree_set;
using absl::flat_hash_map;
using absl::StatusOr;
using common::AxisRange;
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

  if (!def.gids.empty()) {
    *os << ", {";
    first = true;
    for (uint32_t gid : def.gids) {
      if (!first) {
        *os << ", ";
      }
      first = false;
      *os << "g" << gid;
    }
    *os << "}";
  }

  if (!def.feature_tags.empty()) {
    *os << ", {";
    bool first = true;
    for (hb_tag_t tag : def.feature_tags) {
      if (!first) {
        *os << ", ";
      }
      first = false;
      *os << FontHelper::ToString(tag);
    }
    *os << "}";
  }

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
void subtract_sets(S& a, const S& b) {
  // Depending on which set is bigger use the implementation
  // that iterates the fewest elements.
  if (a.size() < b.size()) {
    for (auto it = a.begin(); it != a.end();) {
      if (b.contains(*it)) {
        it = a.erase(it);
      } else {
        ++it;
      }
    }
    return;
  }

  for (uint32_t v : b) {
    a.erase(v);
  }
}

std::optional<AxisRange> subtract(const AxisRange& a, const AxisRange& b) {
  // Result of subtraction must be a single continous range, since subset
  // defs store one range per axis.

  // There are four cases we need to handle:
  // 1. ranges do not intersect, the subtraction is a noop.
  // 2. range b is a superset of a. This removes range a.
  // 3. range a is a strict superset of b (on both bounds). Since we can't split
  // a, this is also a noop.
  // 4. ranges intersect, the intersecting portion is removed from a.

  if (!a.Intersects(b)) {
    return a;
  }

  // B superset
  if (b.start() <= a.start() && b.end() >= a.end()) {
    return std::nullopt;
  }

  // A strict superset
  if (a.start() < b.start() && a.end() > b.end()) {
    return a;
  }

  if (a.start() < b.start()) {
    return *AxisRange::Range(a.start(), b.start());
  } else {
    return *AxisRange::Range(b.end(), a.end());
  }
}

design_space_t subtract(const design_space_t& a, const design_space_t& b) {
  design_space_t c;

  for (const auto& [tag, range] : a) {
    auto e = b.find(tag);
    if (e == b.end()) {
      c[tag] = range;
      continue;
    }

    std::optional<AxisRange> new_range = subtract(range, e->second);
    if (new_range.has_value()) {
      c[tag] = *new_range;
    }
  }

  return c;
}

void SubsetDefinition::Subtract(const SubsetDefinition& other) {
  codepoints.subtract(other.codepoints);
  gids.subtract(other.gids);
  subtract_sets(feature_tags, other.feature_tags);
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

    // For axis space we only suppport a single continous interval so if
    // we have two disjoint intervals then form a new single interval that
    // is a super set of the two input intervals.
    float min = std::min(range.start(), existing->second.start());
    float max = std::max(range.end(), existing->second.end());
    design_space[tag] = *AxisRange::Range(min, max);
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
  if (patch_ids.empty()) {
    e.ignored = true;
    // No mapping provided so this entry will be ignored and the patch id we are
    // free to assign whatever value.
    e.patch_indices.push_back(++last_patch_id);
  } else {
    e.patch_indices = patch_ids;
    e.ignored = false;
  }

  return entries;
}

StatusOr<bool> SubsetDefinition::IsVariableFor(hb_face_t* face) const {
  flat_hash_map<hb_tag_t, AxisRange> face_design_space =
      TRY(FontHelper::GetDesignSpace(face));

  for (const auto& [tag, face_range] : face_design_space) {
    auto it = design_space.find(tag);
    if (it == design_space.end()) {
      if (face_range.IsRange()) {
        return true;
      }
      continue;
    }

    AxisRange subset_range = it->second;
    std::optional<AxisRange> intersection =
        subset_range.Intersection(face_range);
    if (intersection.has_value() && intersection->IsRange()) {
      return true;
    }
  }

  return false;
}

}  // namespace ift::encoder