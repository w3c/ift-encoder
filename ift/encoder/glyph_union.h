#ifndef IFT_ENCODER_GLYPH_UNION_H_
#define IFT_ENCODER_GLYPH_UNION_H_

#include <stdint.h>

#include <vector>

#include "absl/container/node_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/int_set.h"
#include "ift/encoder/types.h"

namespace ift::encoder {

/*
 * This stores disjoint sets of glyph IDs and can retrieve all members of
 * these sets.
 */
class GlyphUnion {
 public:
  explicit GlyphUnion(uint32_t num_glyphs);
  GlyphUnion(const GlyphUnion& other);
  GlyphUnion& operator=(const GlyphUnion& other);

  // Merge all of the sets that intersect glyphs into a single set.
  absl::Status Union(const common::GlyphSet& glyphs);
  absl::Status Union(glyph_id_t glyph1, glyph_id_t glyph2);

  absl::StatusOr<glyph_id_t> Find(glyph_id_t glyph) const;

 private:
  std::vector<uint32_t> rank_;
  mutable std::vector<uint32_t> parent_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_GLYPH_UNION_H_
