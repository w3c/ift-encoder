#include "ift/proto/patch_map.h"

#include <cstdio>
#include <cstring>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "common/font_data.h"
#include "common/font_helper.h"
#include "common/sparse_bit_set.h"
#include "gtest/gtest.h"
#include "ift/proto/patch_encoding.h"

using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::Span;
using absl::Status;
using absl::StrCat;
using common::FontData;
using common::FontHelper;
using common::SparseBitSet;

namespace ift::proto {

class PatchMapTest : public ::testing::Test {
 protected:
  PatchMapTest() {}
};

TEST_F(PatchMapTest, GetEntries) {
  PatchMap::Entry entries[] = {
      {{30, 32}, 1, TABLE_KEYED_FULL},
      {{55, 56, 57}, 2, GLYPH_KEYED},
  };

  PatchMap map{entries[0], entries[1]};
  Span<const PatchMap::Entry> expected(entries);

  ASSERT_EQ(map.GetEntries(), expected);
}

TEST_F(PatchMapTest, Equal) {
  PatchMap map1{
      {{30, 32}, 1, TABLE_KEYED_FULL},
      {{55, 56, 57}, 2, GLYPH_KEYED},
  };

  PatchMap map2{
      {{30, 32}, 1, TABLE_KEYED_FULL},
      {{55, 56, 57}, 2, GLYPH_KEYED},
  };

  PatchMap map3{
      {{30, 32}, 1, TABLE_KEYED_FULL},
      {{55, 56, 57}, 3, GLYPH_KEYED},
  };

  PatchMap map4{
      {{30, 32}, 1, TABLE_KEYED_FULL},
      {{55, 56, 57}, 2, TABLE_KEYED_FULL},
  };

  PatchMap map5{
      {{30, 32}, 1, TABLE_KEYED_FULL},
      {{55, 56, 58}, 2, GLYPH_KEYED},
  };

  ASSERT_EQ(map1, map2);
  ASSERT_FALSE(map1 == map3);
  ASSERT_NE(map1, map3);
  ASSERT_NE(map1, map4);
  ASSERT_NE(map1, map5);
}

TEST_F(PatchMapTest, AddEntry) {
  PatchMap map{
      {{30, 32}, 1, TABLE_KEYED_FULL},
      {{55, 56, 57}, 2, GLYPH_KEYED},
  };

  auto sc = map.AddEntry({77, 79, 80}, 5, TABLE_KEYED_FULL);
  ASSERT_TRUE(sc.ok()) << sc;

  PatchMap expected = {
      {{30, 32}, 1, TABLE_KEYED_FULL},
      {{55, 56, 57}, 2, GLYPH_KEYED},
      {{77, 79, 80}, 5, TABLE_KEYED_FULL},
  };

  ASSERT_EQ(map, expected);

  sc = map.AddEntry({1, 2, 3}, 3, GLYPH_KEYED);
  ASSERT_TRUE(sc.ok()) << sc;

  expected = {
      {{30, 32}, 1, TABLE_KEYED_FULL},
      {{55, 56, 57}, 2, GLYPH_KEYED},
      {{77, 79, 80}, 5, TABLE_KEYED_FULL},
      {{1, 2, 3}, 3, GLYPH_KEYED},
  };

  ASSERT_EQ(map, expected);
}

TEST_F(PatchMapTest, AddEntry_Ignored) {
  PatchMap map{
      {{30, 32}, 1, TABLE_KEYED_FULL},
      {{55, 56, 57}, 2, GLYPH_KEYED},
  };

  auto sc = map.AddEntry({77, 79, 80}, 5, TABLE_KEYED_FULL, true);
  ASSERT_TRUE(sc.ok()) << sc;

  PatchMap::Entry e = {{77, 79, 80}, 5, TABLE_KEYED_FULL};
  e.ignored = true;

  PatchMap expected = {
      {{30, 32}, 1, TABLE_KEYED_FULL},
      {{55, 56, 57}, 2, GLYPH_KEYED},
      e,
  };
}

TEST_F(PatchMapTest, AddEntry_WithCopyIndices) {
  PatchMap map{
      {{30, 32}, 1, TABLE_KEYED_FULL},
      {{55, 56, 57}, 2, GLYPH_KEYED},
  };

  PatchMap::Coverage cov;
  cov.child_indices = {0, 1};

  auto sc = map.AddEntry(cov, 5, TABLE_KEYED_FULL);
  ASSERT_TRUE(sc.ok()) << sc;

  ASSERT_EQ(map.GetEntries().at(2).coverage, cov);
}

TEST_F(PatchMapTest, AddEntry_InvalidCopyIndices) {
  PatchMap map{
      {{30, 32}, 1, TABLE_KEYED_FULL},
      {{55, 56, 57}, 2, GLYPH_KEYED},
  };

  PatchMap::Coverage cov;
  cov.child_indices = {0, 2};

  auto sc = map.AddEntry(cov, 5, TABLE_KEYED_FULL);
  ASSERT_EQ(sc, absl::InvalidArgumentError(
                    "Invalid copy index. 2 is out of bounds."));
}

TEST_F(PatchMapTest, IsInvalidating) {
  ASSERT_FALSE(PatchMap::Entry({}, 0, GLYPH_KEYED).IsInvalidating());
  ASSERT_TRUE(PatchMap::Entry({}, 0, TABLE_KEYED_FULL).IsInvalidating());
  ASSERT_TRUE(PatchMap::Entry({}, 0, TABLE_KEYED_PARTIAL).IsInvalidating());
}

}  // namespace ift::proto
