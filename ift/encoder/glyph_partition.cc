#include "ift/encoder/glyph_partition.h"

#include <numeric>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "common/int_set.h"
#include "common/try.h"
#include "ift/encoder/types.h"

using absl::flat_hash_map;
using absl::Span;
using absl::Status;
using absl::StatusOr;
using common::GlyphSet;

namespace ift::encoder {

GlyphPartition::GlyphPartition(uint32_t num_glyphs)
    : rank_(num_glyphs, 0), parent_(num_glyphs), rep_to_set_() {
  std::iota(parent_.begin(), parent_.end(), 0);
}

GlyphPartition::GlyphPartition(const GlyphPartition& other)
    : rank_(other.rank_), parent_(other.parent_) {}

GlyphPartition& GlyphPartition::operator=(const GlyphPartition& other) {
  if (this == &other) {
    return *this;
  }

  rank_ = other.rank_;
  parent_ = other.parent_;
  return *this;
}

Status GlyphPartition::Union(const GlyphSet& glyphs) {
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

Status GlyphPartition::Union(glyph_id_t glyph1, glyph_id_t glyph2) {
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
  cache_valid_ = false;
  return absl::OkStatus();
}

absl::Status GlyphPartition::Union(const GlyphPartition& other) {
  if (other.parent_.size() != parent_.size()) {
    return absl::InvalidArgumentError(
        "Glyph partitions are not compatible, they must have the same number "
        "of elements.");
  }

  for (const GlyphSet& set : TRY(other.NonIdentityGroups())) {
    TRYV(Union(set));
  }

  return absl::OkStatus();
}

StatusOr<glyph_id_t> GlyphPartition::Find(glyph_id_t glyph) const {
  if (glyph >= parent_.size()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Glyph id %d is out of bounds.", glyph));
  }

  if (parent_[glyph] != glyph) {
    parent_[glyph] = TRY(Find(parent_[glyph]));
  }
  return parent_[glyph];
}

StatusOr<const GlyphSet&> GlyphPartition::GlyphsFor(glyph_id_t glyph) const {
  if (glyph >= parent_.size()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Glyph id %d is out of bounds.", glyph));
  }

  if (!cache_valid_) {
    TRYV(RebuildCache());
  }

  glyph_id_t rep = TRY(Find(glyph));
  return rep_to_set_[rep];
}

StatusOr<Span<const GlyphSet>> GlyphPartition::NonIdentityGroups() const {
  if (!cache_valid_) {
    TRYV(RebuildCache());
  }
  return non_identity_groups_;
}

Status GlyphPartition::RebuildCache() const {
  rep_to_set_.clear();
  for (glyph_id_t i = 0; i < parent_.size(); ++i) {
    rep_to_set_[TRY(Find(i))].insert(i);
  }

  non_identity_groups_.clear();
  for (const auto& [_, gids] : rep_to_set_) {
    if (gids.size() > 1) {
      non_identity_groups_.push_back(gids);
    }
  }

  // Sort so the ordering is deterministic.
  std::sort(non_identity_groups_.begin(), non_identity_groups_.end());

  cache_valid_ = true;
  return absl::OkStatus();
}

}  // namespace ift::encoder