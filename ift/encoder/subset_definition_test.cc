#include "ift/encoder/subset_definition.h"

#include "gtest/gtest.h"
#include "ift/proto/patch_encoding.h"
#include "ift/proto/patch_map.h"

using ift::proto::PatchEncoding;
using ift::proto::PatchMap;

namespace ift::encoder {

class SubsetDefinitionTest : public ::testing::Test {};

TEST_F(SubsetDefinitionTest, ToEntries_Simple) {
  SubsetDefinition codepoints{4, 5, 6};
  ASSERT_EQ(codepoints.ToEntries(PatchEncoding::TABLE_KEYED_FULL, 5, 10, {42}),
            std::vector{PatchMap::Entry({4, 5, 6}, 42,
                                        PatchEncoding::TABLE_KEYED_FULL)});

  SubsetDefinition features;
  features.feature_tags = {HB_TAG('f', 'o', 'o', 'o')};
  PatchMap::Entry expected;
  expected.coverage.features = {HB_TAG('f', 'o', 'o', 'o')};
  expected.patch_indices = {11, 12};
  expected.encoding = PatchEncoding::GLYPH_KEYED;
  ASSERT_EQ(features.ToEntries(PatchEncoding::GLYPH_KEYED, 5, 10, {11, 12}),
            std::vector{expected});
}

TEST_F(SubsetDefinitionTest, ToEntries_Composite) {
  SubsetDefinition combined{4, 5, 6};
  combined.feature_tags = {HB_TAG('f', 'o', 'o', 'o')};

  PatchMap::Entry e1;
  e1.coverage.codepoints = {4, 5, 6};
  e1.ignored = true;
  e1.patch_indices = {6};
  e1.encoding = PatchEncoding::GLYPH_KEYED;

  PatchMap::Entry e2;
  e2.coverage.features = {HB_TAG('f', 'o', 'o', 'o')};
  e2.ignored = true;
  e2.patch_indices = {7};
  e2.encoding = PatchEncoding::GLYPH_KEYED;

  PatchMap::Entry e3;
  e3.coverage.child_indices = {10, 11};
  e3.coverage.conjunctive = false;
  e3.patch_indices = {11, 12};
  e3.encoding = PatchEncoding::GLYPH_KEYED;

  auto expected = std::vector{e1, e2, e3};
  ASSERT_EQ(combined.ToEntries(PatchEncoding::GLYPH_KEYED, 5, 10, {11, 12}),
            expected);
}

}  // namespace ift::encoder