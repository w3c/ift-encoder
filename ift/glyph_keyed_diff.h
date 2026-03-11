#ifndef IFT_GLYPH_KEYED_DIFF_H_
#define IFT_GLYPH_KEYED_DIFF_H_

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "ift/common/brotli_binary_diff.h"
#include "ift/common/compat_id.h"
#include "ift/common/font_data.h"
#include "ift/common/int_set.h"

namespace ift {

/* Generates glyph keyed patches. */
class GlyphKeyedDiff {
 public:
  GlyphKeyedDiff(const ift::common::FontData& font,
                 ift::common::CompatId base_compat_id,
                 absl::flat_hash_set<hb_tag_t> included_tags,
                 unsigned quality = 11)
      : font_(font),
        base_compat_id_(base_compat_id),
        tags_(included_tags),
        brotli_diff_(quality) {}

  absl::StatusOr<ift::common::FontData> CreatePatch(
      const ift::common::IntSet& gids) const;

 private:
  absl::StatusOr<ift::common::FontData> CreateDataStream(
      const ift::common::IntSet& gids, bool u16_gids) const;

  const ift::common::FontData& font_;
  ift::common::CompatId base_compat_id_;
  absl::flat_hash_set<hb_tag_t> tags_;
  ift::common::BrotliBinaryDiff brotli_diff_;
};

}  // namespace ift

#endif  // IFT_GLYPH_KEYED_DIFF_H_
