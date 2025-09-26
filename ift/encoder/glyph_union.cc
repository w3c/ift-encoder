#include "ift/encoder/glyph_union.h"

#include <numeric>

#include "absl/strings/str_format.h"
#include "common/int_set.h"
#include "common/try.h"
#include "ift/encoder/types.h"

using absl::Status;
using absl::StatusOr;
using common::GlyphSet;

namespace ift::encoder {

GlyphUnion::GlyphUnion(uint32_t num_glyphs)
    : rank_(num_glyphs, 0), parent_(num_glyphs) {
  std::iota(parent_.begin(), parent_.end(), 0);
}

GlyphUnion::GlyphUnion(const GlyphUnion& other)
    : rank_(other.rank_), parent_(other.parent_) {}

GlyphUnion& GlyphUnion::operator=(const GlyphUnion& other) {
  if (this == &other) {
    return *this;
  }

  rank_ = other.rank_;
  parent_ = other.parent_;
  return *this;
}

Status GlyphUnion::Union(const GlyphSet& glyphs) {
  if (glyphs.empty()) {
    return absl::OkStatus();
  }

  auto it = glyphs.begin();
  glyph_id_t first_gid = *it;
  ++it;

  if (first_gid >= parent_.size()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Glyph id %d is out of bounds.", first_gid));
  }

  for (; it != glyphs.end(); ++it) {
    glyph_id_t second_gid = *it;
    if (second_gid >= parent_.size()) {
      return absl::InvalidArgumentError(
          absl::StrFormat("Glyph id %d is out of bounds.", second_gid));
    }
    TRYV(Union(first_gid, second_gid));
  }
  return absl::OkStatus();
}

Status GlyphUnion::Union(glyph_id_t glyph1, glyph_id_t glyph2) {
  if (glyph1 >= parent_.size()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Glyph id %d is out of bounds.", glyph1));
  }
  if (glyph2 >= parent_.size()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Glyph id %d is out of bounds.", glyph2));
  }

  glyph_id_t root1 = TRY(Find(glyph1));
  glyph_id_t root2 = TRY(Find(glyph2));

  if (root1 != root2) {
    if (rank_[root1] < rank_[root2]) {
      parent_[root1] = root2;
    } else if (rank_[root1] > rank_[root2]) {
      parent_[root2] = root1;
    } else {
      parent_[root2] = root1;
      rank_[root1]++;
    }
  }
  return absl::OkStatus();
}

StatusOr<glyph_id_t> GlyphUnion::Find(glyph_id_t glyph) const {
  if (glyph >= parent_.size()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Glyph id %d is out of bounds.", glyph));
  }

  if (parent_[glyph] != glyph) {
    parent_[glyph] = TRY(Find(parent_[glyph]));
  }
  return parent_[glyph];
}

}  // namespace ift::encoder
