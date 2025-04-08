#ifndef IFT_TABLE_KEYED_DIFF_H_
#define IFT_TABLE_KEYED_DIFF_H_

#include <initializer_list>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "common/binary_diff.h"
#include "common/brotli_binary_diff.h"
#include "common/compat_id.h"
#include "common/font_data.h"

namespace ift {

/* Creates a per table brotli binary diff of two fonts. */
class TableKeyedDiff : public common::BinaryDiff {
 public:
  TableKeyedDiff(common::CompatId base_compat_id)
      : binary_diff_(11), base_compat_id_(base_compat_id) {}

  TableKeyedDiff(common::CompatId base_compat_id,
                 std::initializer_list<const char*> excluded_tags)
      : binary_diff_(11),
        base_compat_id_(base_compat_id),
        excluded_tags_(),
        replaced_tags_() {
    std::copy(excluded_tags.begin(), excluded_tags.end(),
              std::inserter(excluded_tags_, excluded_tags_.begin()));
  }

  TableKeyedDiff(common::CompatId base_compat_id,
                 absl::btree_set<std::string> excluded_tags,
                 absl::btree_set<std::string> replaced_tags)
      : binary_diff_(11),
        base_compat_id_(base_compat_id),
        excluded_tags_(),
        replaced_tags_() {
    excluded_tags_ = excluded_tags;
    replaced_tags_ = replaced_tags;
  }

  absl::Status Diff(const common::FontData& font_base,
                    const common::FontData& font_derived,
                    common::FontData* patch /* OUT */) const override;

 private:
  void AddAllMatching(const absl::flat_hash_set<hb_tag_t>& tags,
                      absl::btree_set<std::string>& result) const;
  absl::btree_set<std::string> TagsToDiff(
      const absl::flat_hash_set<hb_tag_t>& before,
      const absl::flat_hash_set<hb_tag_t>& after) const;

  common::BrotliBinaryDiff binary_diff_;
  common::CompatId base_compat_id_;
  absl::btree_set<std::string> excluded_tags_;
  absl::btree_set<std::string> replaced_tags_;
};

}  // namespace ift

#endif  // IFT_TABLE_KEYED_DIFF_H_
