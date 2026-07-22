#ifndef IFT_TABLE_KEYED_DIFF_H_
#define IFT_TABLE_KEYED_DIFF_H_

#include <initializer_list>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "ift/common/binary_diff.h"
#include "ift/common/brotli_binary_diff.h"
#include "ift/common/compat_id.h"
#include "ift/common/font_data.h"

namespace ift {

struct TableDiffKey {
  ift::common::FontData base;
  ift::common::FontData derived;

  TableDiffKey() = default;

  TableDiffKey(const ift::common::FontData& b, const ift::common::FontData& d) {
    base.shallow_copy(b);
    derived.shallow_copy(d);
  }

  TableDiffKey(const TableDiffKey& other) {
    base.shallow_copy(other.base);
    derived.shallow_copy(other.derived);
  }

  TableDiffKey& operator=(const TableDiffKey& other) {
    if (this != &other) {
      base.shallow_copy(other.base);
      derived.shallow_copy(other.derived);
    }
    return *this;
  }

  TableDiffKey(TableDiffKey&&) = default;
  TableDiffKey& operator=(TableDiffKey&&) = default;

  bool operator==(const TableDiffKey& other) const {
    return base == other.base && derived == other.derived;
  }

  template <typename H>
  friend H AbslHashValue(H h, const TableDiffKey& key) {
    return H::combine(std::move(h), key.base.str(), key.derived.str());
  }
};

using TableDiffCache =
    absl::flat_hash_map<TableDiffKey, std::optional<ift::common::FontData>>;

/* Creates a per table brotli binary diff of two fonts. */
class TableKeyedDiff : public ift::common::BinaryDiff {
 public:
  explicit TableKeyedDiff(ift::common::CompatId base_compat_id,
                          TableDiffCache* cache = nullptr)
      : binary_diff_(11), base_compat_id_(base_compat_id), cache_(cache) {}

  TableKeyedDiff(ift::common::CompatId base_compat_id,
                 std::initializer_list<const char*> excluded_tags,
                 TableDiffCache* cache = nullptr)
      : binary_diff_(11),
        base_compat_id_(base_compat_id),
        excluded_tags_(),
        replaced_tags_(),
        cache_(cache) {
    std::copy(excluded_tags.begin(), excluded_tags.end(),
              std::inserter(excluded_tags_, excluded_tags_.begin()));
  }

  TableKeyedDiff(ift::common::CompatId base_compat_id,
                 absl::btree_set<std::string> excluded_tags,
                 absl::btree_set<std::string> replaced_tags,
                 TableDiffCache* cache = nullptr)
      : binary_diff_(11),
        base_compat_id_(base_compat_id),
        excluded_tags_(std::move(excluded_tags)),
        replaced_tags_(std::move(replaced_tags)),
        cache_(cache) {}

  void SetCache(TableDiffCache* cache) { cache_ = cache; }
  TableDiffCache* cache() const { return cache_; }

  absl::Status Diff(const ift::common::FontData& font_base,
                    const ift::common::FontData& font_derived,
                    ift::common::FontData* patch /* OUT */) const override;

 private:
  void AddAllMatching(const absl::flat_hash_set<hb_tag_t>& tags,
                      absl::btree_set<std::string>& result) const;
  absl::btree_set<std::string> TagsToDiff(
      const absl::flat_hash_set<hb_tag_t>& before,
      const absl::flat_hash_set<hb_tag_t>& after) const;

  ift::common::BrotliBinaryDiff binary_diff_;
  ift::common::CompatId base_compat_id_;
  absl::btree_set<std::string> excluded_tags_;
  absl::btree_set<std::string> replaced_tags_;
  TableDiffCache* cache_ = nullptr;
};

}  // namespace ift

#endif  // IFT_TABLE_KEYED_DIFF_H_
