#ifndef IFT_ENCODER_GLYPH_PARTITION_H_
#define IFT_ENCODER_GLYPH_PARTITION_H_

#include <stdint.h>

#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/int_set.h"
#include "ift/encoder/types.h"

namespace ift::encoder {

/*
 * This stores disjoint sets of glyph IDs and can retrieve all members of
 * these sets.
 */
class GlyphPartition {
 public:
  explicit GlyphPartition(uint32_t num_glyphs);
  GlyphPartition(const GlyphPartition& other);
  GlyphPartition& operator=(const GlyphPartition& other);

  // Merge all of the sets that intersect glyphs into a single set.
  absl::Status Union(const common::GlyphSet& glyphs);

  // Merge the sets containing glyph1 and glyph2 together.
  absl::Status Union(glyph_id_t glyph1, glyph_id_t glyph2);

  // Union each set from other together in this partition.
  absl::Status Union(const GlyphPartition& other);

  // Returns a representative member of the group that glyph belongs too.
  absl::StatusOr<glyph_id_t> Find(glyph_id_t glyph) const;

  // Returns all members of the group the glyph belongs too.
  absl::StatusOr<const common::GlyphSet&> GlyphsFor(glyph_id_t glyph) const;

  // Returns all groups in this partition that have more than one member.
  absl::StatusOr<absl::Span<const common::GlyphSet>> NonIdentityGroups() const;

 private:
  absl::Status RebuildCache() const;

  std::vector<uint32_t> rank_;
  mutable std::vector<uint32_t> parent_;

  mutable bool cache_valid_ = false;
  mutable absl::flat_hash_map<glyph_id_t, common::GlyphSet> rep_to_set_;
  mutable std::vector<common::GlyphSet> non_identity_groups_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_GLYPH_PARTITION_H_