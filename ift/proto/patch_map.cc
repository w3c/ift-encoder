#include "ift/proto/patch_map.h"

#include <iterator>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "common/font_helper.h"
#include "common/int_set.h"
#include "common/sparse_bit_set.h"
#include "ift/feature_registry/feature_registry.h"
#include "ift/proto/patch_encoding.h"

using absl::btree_set;
using absl::flat_hash_set;
using absl::Span;
using absl::Status;
using absl::StatusOr;
using common::FontHelper;
using common::IntSet;
using common::SparseBitSet;
using ift::feature_registry::FeatureTagToIndex;
using ift::feature_registry::IndexToFeatureTag;

namespace ift::proto {

template <typename S>
static bool sets_intersect(const S& a, const S& b) {
  bool a_smaller = a.size() < b.size();
  const auto& smaller = a_smaller ? a : b;
  const auto& larger = a_smaller ? b : a;
  for (uint32_t v : smaller) {
    if (larger.contains(v)) {
      return true;
    }
  }
  return false;
}

void PrintTo(const PatchMap::Coverage& coverage, std::ostream* os) {
  const IntSet& sorted_codepoints = coverage.codepoints;

  if (!coverage.features.empty() || !coverage.design_space.empty()) {
    *os << "{";
  }
  *os << "{";
  for (auto it = sorted_codepoints.begin(); it != sorted_codepoints.end();
       it++) {
    *os << *it;
    auto next = it;
    if (++next != sorted_codepoints.end()) {
      *os << ", ";
    }
  }
  *os << "}";
  if (coverage.features.empty() && coverage.design_space.empty()) {
    return;
  }

  if (!coverage.features.empty()) {
    *os << ", {";
    absl::btree_set<hb_tag_t> sorted_features;
    std::copy(coverage.features.begin(), coverage.features.end(),
              std::inserter(sorted_features, sorted_features.begin()));

    for (auto it = sorted_features.begin(); it != sorted_features.end(); it++) {
      std::string tag;
      tag.resize(5);
      snprintf(tag.data(), 5, "%c%c%c%c", HB_UNTAG(*it));
      tag.resize(4);
      *os << tag;
      auto next = it;
      if (++next != sorted_features.end()) {
        *os << ", ";
      }
    }
    *os << "}";
  }

  *os << ", {";
  if (!coverage.design_space.empty()) {
    for (const auto& [tag, range] : coverage.design_space) {
      *os << FontHelper::ToString(tag) << ": ";
      PrintTo(range, os);
      *os << ", ";
    }
  }

  *os << "}";
}

void PrintTo(const PatchMap::Entry& entry, std::ostream* os) {
  PrintTo(entry.coverage, os);
  *os << ", " << entry.patch_index << ", " << entry.encoding;
}

void PrintTo(const PatchMap& map, std::ostream* os) {
  *os << "[" << std::endl;
  for (const auto& e : map.entries_) {
    *os << "  Entry {";
    PrintTo(e, os);
    *os << "}," << std::endl;
  }
  *os << "]";
}

Span<const PatchMap::Entry> PatchMap::GetEntries() const { return entries_; }

Status PatchMap::AddEntry(const PatchMap::Coverage& coverage,
                          uint32_t patch_index, PatchEncoding encoding,
                          bool ignored) {
  // If child indices are present ensure they refer only to entries prior to
  // this one.
  if (!coverage.child_indices.empty()) {
    for (uint32_t index : coverage.child_indices) {
      if (index >= entries_.size()) {
        return absl::InvalidArgumentError(
            absl::StrCat("Invalid copy index. ", index, " is out of bounds."));
      }
    }
  }

  Entry e;
  e.coverage = coverage;
  e.patch_index = patch_index;
  e.encoding = encoding;
  e.ignored = ignored;
  entries_.push_back(std::move(e));
  return absl::OkStatus();
}

}  // namespace ift::proto
