#include "ift/encoder/subset_definition.h"

#include "common/axis_range.h"
#include "common/font_data.h"
#include "gtest/gtest.h"
#include "hb.h"
#include "ift/proto/patch_encoding.h"
#include "ift/proto/patch_map.h"

using common::AxisRange;
using common::hb_blob_unique_ptr;
using common::hb_face_unique_ptr;
using common::make_hb_blob;
using common::make_hb_face;
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

TEST_F(SubsetDefinitionTest, IsVariableFor) {
  hb_blob_unique_ptr blob = make_hb_blob(
      hb_blob_create_from_file("common/testdata/Roboto[wdth,wght].abcd.ttf"));
  ASSERT_GT(hb_blob_get_length(blob.get()), 0);
  hb_face_unique_ptr face = make_hb_face(hb_face_create(blob.get(), 0));

  blob = make_hb_blob(
      hb_blob_create_from_file("common/testdata/Roboto-Regular.abcd.ttf"));
  ASSERT_GT(hb_blob_get_length(blob.get()), 0);
  hb_face_unique_ptr static_face = make_hb_face(hb_face_create(blob.get(), 0));

  {
    SubsetDefinition noop;
    ASSERT_TRUE(*noop.IsVariableFor(face.get()));
    ASSERT_FALSE(*noop.IsVariableFor(static_face.get()));
  }

  {
    SubsetDefinition full;
    full.design_space = {
        {HB_TAG('w', 'g', 'h', 't'), AxisRange::Point(300)},
        {HB_TAG('w', 'd', 't', 'h'), AxisRange::Point(80)},
    };
    ASSERT_FALSE(*full.IsVariableFor(face.get()));
    ASSERT_FALSE(*full.IsVariableFor(static_face.get()));
  }

  {
    SubsetDefinition partial;
    partial.design_space = {
        {HB_TAG('w', 'd', 't', 'h'), AxisRange::Point(80)},
    };
    ASSERT_TRUE(*partial.IsVariableFor(face.get()));
    ASSERT_FALSE(*partial.IsVariableFor(static_face.get()));
  }

  {
    SubsetDefinition partial_ranges;
    partial_ranges.design_space = {
        {HB_TAG('w', 'g', 'h', 't'), *AxisRange::Range(300, 350)},
        {HB_TAG('w', 'd', 't', 'h'), *AxisRange::Range(80, 85)},
    };
    ASSERT_TRUE(*partial_ranges.IsVariableFor(face.get()));
    ASSERT_FALSE(*partial_ranges.IsVariableFor(static_face.get()));
  }

  {
    SubsetDefinition point_intersection;
    point_intersection.design_space = {
        {HB_TAG('w', 'g', 'h', 't'), *AxisRange::Range(900, 1000)},
        {HB_TAG('w', 'd', 't', 'h'), AxisRange::Point(80)},
    };
    ASSERT_FALSE(*point_intersection.IsVariableFor(face.get()));
    ASSERT_FALSE(*point_intersection.IsVariableFor(static_face.get()));
  }

  {
    SubsetDefinition out_of_bounds;
    out_of_bounds.design_space = {
        {HB_TAG('w', 'g', 'h', 't'), *AxisRange::Range(2000, 3000)},
    };
    ASSERT_TRUE(*out_of_bounds.IsVariableFor(face.get()));
    ASSERT_FALSE(*out_of_bounds.IsVariableFor(static_face.get()));
  }
}

TEST_F(SubsetDefinitionTest, Subtraction) {
  {
    SubsetDefinition a{1, 2, 3, 4};
    SubsetDefinition b{3, 5, 6};
    a.Subtract(b);
    ASSERT_EQ(a, (SubsetDefinition{1, 2, 4}));
  }

  {
    SubsetDefinition a{1, 2, 3, 4};
    a.gids = {7, 8, 9};
    a.feature_tags = {HB_TAG('f', 'o', 'o', ' '), HB_TAG('b', 'a', 'r', ' ')};

    SubsetDefinition b{3, 5, 6};
    b.gids = {8, 10};
    b.feature_tags = {HB_TAG('f', 'o', 'o', ' ')};

    SubsetDefinition c{1, 2, 4};
    c.gids = {7, 9};
    c.feature_tags = {HB_TAG('b', 'a', 'r', ' ')};

    a.Subtract(b);
    ASSERT_EQ(a, c);
  }
}

}  // namespace ift::encoder