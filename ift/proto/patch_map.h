#ifndef IFT_PROTO_PATCH_MAP_H_
#define IFT_PROTO_PATCH_MAP_H_

#include <cstdint>
#include <initializer_list>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "common/axis_range.h"
#include "hb.h"
#include "ift/proto/patch_encoding.h"

namespace ift::proto {

/*
 * Abstract representation of a map from subset definitions too patches.
 */
class PatchMap {
 public:
  static bool IsInvalidating(PatchEncoding encoding) {
    return encoding == TABLE_KEYED_PARTIAL || encoding == TABLE_KEYED_FULL;
  }

  struct Coverage {
    // TODO(garretrieger): move constructors?

    Coverage() {}
    Coverage(std::initializer_list<uint32_t> codepoints_list)
        : codepoints(codepoints_list) {}
    Coverage(const absl::flat_hash_set<uint32_t>& codepoints_list)
        : codepoints(codepoints_list) {}

    friend void PrintTo(const Coverage& point, std::ostream* os);

    bool operator==(const Coverage& other) const {
      return other.codepoints == codepoints && other.features == features &&
             other.design_space == design_space;
    }

    uint32_t SmallestCodepoint() const {
      uint32_t min = 0xFFFFFFFF;
      for (uint32_t cp : codepoints) {
        if (cp < min) {
          min = cp;
        }
      }
      return min;
    }

    // TODO(garretrieger): use hb sets instead?
    absl::flat_hash_set<uint32_t> codepoints;
    absl::btree_set<hb_tag_t> features;
    absl::btree_map<hb_tag_t, common::AxisRange> design_space;

    // If true copy mode
    // (https://w3c.github.io/IFT/Overview.html#mapping-entry-copymodeandcount)
    // is "append", other it's "union".
    bool copy_mode_append = false;
    // Set of copy indices
    // (https://w3c.github.io/IFT/Overview.html#mapping-entry-copyindices)
    // values are the indices of previous entries.
    absl::btree_set<uint32_t> copy_indices;
  };

  struct Entry {
    // TODO(garretrieger): move constructors?

    Entry() {}
    Entry(std::initializer_list<uint32_t> codepoints, uint32_t patch_idx,
          PatchEncoding enc)
        : coverage(codepoints), patch_index(patch_idx), encoding(enc) {}

    friend void PrintTo(const Entry& point, std::ostream* os);

    bool operator==(const Entry& other) const {
      return other.coverage == coverage && other.patch_index == patch_index &&
             other.encoding == encoding && other.ignored == ignored;
    }

    bool IsInvalidating() const { return PatchMap::IsInvalidating(encoding); }

    Coverage coverage;
    uint32_t patch_index;
    PatchEncoding encoding;
    bool ignored = false;
  };

  // TODO(garretrieger): move constructors?
  PatchMap() {}
  PatchMap(std::initializer_list<Entry> entries) : entries_(entries) {}

  friend void PrintTo(const PatchMap& point, std::ostream* os);

  bool operator==(const PatchMap& other) const {
    return other.entries_ == entries_;
  }

  bool operator!=(const PatchMap& other) const { return !(*this == other); }

  absl::Span<const Entry> GetEntries() const;

  absl::Status AddEntry(const Coverage& coverage, uint32_t patch_index,
                        ift::proto::PatchEncoding encoding,
                        bool ignored = false);

 private:
  // TODO(garretrieger): keep an index which maps from patch_index to entry
  // index for faster deletions.
  std::vector<Entry> entries_;
};

}  // namespace ift::proto

#endif  // IFT_PROTO_PATCH_MAP_H_