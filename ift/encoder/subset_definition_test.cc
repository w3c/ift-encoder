#include "ift/encoder/subset_definition.h"

#include <optional>

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
    a.feature_tags = {HB_TAG('f', 'o', 'o', ' '), HB_TAG('b', 'a', 'r', ' '),
                      HB_TAG('b', 'a', 'z', ' ')};

    SubsetDefinition b{3, 5, 6};
    b.gids = {8, 10};
    b.feature_tags = {HB_TAG('f', 'o', 'o', ' '), HB_TAG('a', 'b', 'c', 'd')};

    SubsetDefinition c{1, 2, 4};
    c.gids = {7, 9};
    c.feature_tags = {HB_TAG('b', 'a', 'r', ' '), HB_TAG('b', 'a', 'z', ' ')};

    SubsetDefinition def = a;
    def.Subtract(b);
    ASSERT_EQ(def, c);

    SubsetDefinition d{5, 6};
    d.gids = {10};
    d.feature_tags = {HB_TAG('a', 'b', 'c', 'd')};

    def = b;
    def.Subtract(a);
    ASSERT_EQ(def, d);
  }
}

void TestDesignSpaceSubtraction(hb_tag_t tag_a, AxisRange a, hb_tag_t tag_b,
                                AxisRange b,
                                std::optional<AxisRange> expected) {
  SubsetDefinition sa;
  sa.design_space = {{tag_a, a}};

  SubsetDefinition sb;
  sb.design_space = {{tag_b, b}};

  sa.Subtract(sb);

  if (expected.has_value()) {
    ASSERT_EQ(sa.design_space.at(tag_a), *expected);
  } else {
    ASSERT_FALSE(sa.design_space.contains(tag_a));
  }
}

void TestDesignSpaceSubtraction(AxisRange a, AxisRange b,
                                std::optional<AxisRange> expected) {
  hb_tag_t tag = HB_TAG('a', 'b', 'c', 'd');
  TestDesignSpaceSubtraction(tag, a, tag, b, expected);
}

TEST_F(SubsetDefinitionTest, SubtractDesignSpace) {
  // Case 1 disjoint
  TestDesignSpaceSubtraction(*AxisRange::Range(100, 200),
                             *AxisRange::Range(200, 300),
                             *AxisRange::Range(100, 200));
  TestDesignSpaceSubtraction(*AxisRange::Range(100, 200),
                             *AxisRange::Range(201, 300),
                             *AxisRange::Range(100, 200));

  // Case 2 'b' super set
  TestDesignSpaceSubtraction(*AxisRange::Range(100, 200),
                             *AxisRange::Range(100, 200), std::nullopt);
  TestDesignSpaceSubtraction(*AxisRange::Range(100, 200),
                             *AxisRange::Range(100, 300), std::nullopt);
  TestDesignSpaceSubtraction(*AxisRange::Range(100, 200),
                             *AxisRange::Range(90, 200), std::nullopt);
  TestDesignSpaceSubtraction(*AxisRange::Range(100, 200),
                             *AxisRange::Range(90, 300), std::nullopt);

  // Case 3 'a' strict super set
  TestDesignSpaceSubtraction(*AxisRange::Range(100, 200),
                             *AxisRange::Range(101, 199),
                             *AxisRange::Range(100, 200));

  // Case 4 everything else
  TestDesignSpaceSubtraction(*AxisRange::Range(100, 200),
                             *AxisRange::Range(150, 200),
                             *AxisRange::Range(100, 150));
  TestDesignSpaceSubtraction(*AxisRange::Range(100, 200),
                             *AxisRange::Range(150, 300),
                             *AxisRange::Range(100, 150));

  TestDesignSpaceSubtraction(*AxisRange::Range(100, 200),
                             *AxisRange::Range(100, 150),
                             *AxisRange::Range(150, 200));
  TestDesignSpaceSubtraction(*AxisRange::Range(100, 200),
                             *AxisRange::Range(50, 150),
                             *AxisRange::Range(150, 200));

  // Different tags
  TestDesignSpaceSubtraction(
      HB_TAG('f', 'o', 'o', ' '), *AxisRange::Range(100, 200),
      HB_TAG('b', 'a', 'r', ' '), *AxisRange::Range(150, 250),
      *AxisRange::Range(100, 200));
}

void TestUnion(SubsetDefinition a, SubsetDefinition b, SubsetDefinition ab) {
  SubsetDefinition u = a;
  u.Union(b);
  ASSERT_EQ(u, ab);

  u = b;
  u.Union(a);
  ASSERT_EQ(u, ab);
}

TEST_F(SubsetDefinitionTest, Union) {
  SubsetDefinition a{1, 2, 3, 4};
  a.feature_tags = {
      HB_TAG('f', 'o', 'o', ' '),
  };
  a.design_space = {
      {HB_TAG('w', 'g', 'h', 't'), AxisRange::Point(300)},
  };

  SubsetDefinition b{2, 8, 9};
  b.feature_tags = {
      HB_TAG('b', 'a', 'r', ' '),
  };
  b.design_space = {
      {HB_TAG('w', 'd', 't', 'h'), AxisRange::Point(75)},
  };

  SubsetDefinition ab{1, 2, 3, 4, 8, 9};
  ab.feature_tags = {
      HB_TAG('f', 'o', 'o', ' '),
      HB_TAG('b', 'a', 'r', ' '),
  };
  ab.design_space = {
      {HB_TAG('w', 'g', 'h', 't'), AxisRange::Point(300)},
      {HB_TAG('w', 'd', 't', 'h'), AxisRange::Point(75)},
  };

  TestUnion(a, b, ab);
}

TEST_F(SubsetDefinitionTest, UnionDesignSpace) {
  SubsetDefinition a;
  a.design_space = {
      {HB_TAG('w', 'g', 'h', 't'), AxisRange::Point(300)},
  };

  SubsetDefinition b;
  b.design_space = {
      {HB_TAG('w', 'g', 'h', 't'), AxisRange::Point(700)},
  };

  SubsetDefinition ab;
  ab.design_space = {
      {HB_TAG('w', 'g', 'h', 't'), *AxisRange::Range(300, 700)},
  };

  // Point - Point
  TestUnion(a, b, ab);

  // Point - Range
  b.design_space = {
      {HB_TAG('w', 'g', 'h', 't'), *AxisRange::Range(100, 200)},
  };
  ab.design_space = {
      {HB_TAG('w', 'g', 'h', 't'), *AxisRange::Range(100, 300)},
  };
  TestUnion(a, b, ab);

  // Range - Range
  a.design_space = {
      {HB_TAG('w', 'g', 'h', 't'), *AxisRange::Range(300, 600)},
  };
  ab.design_space = {
      {HB_TAG('w', 'g', 'h', 't'), *AxisRange::Range(100, 600)},
  };
  TestUnion(a, b, ab);

  // Range - Range
  a.design_space = {
      {HB_TAG('w', 'g', 'h', 't'), *AxisRange::Range(50, 350)},
  };
  ab.design_space = {
      {HB_TAG('w', 'g', 'h', 't'), *AxisRange::Range(50, 350)},
  };
  TestUnion(a, b, ab);

  // Range - Range
  b.design_space = {
      {HB_TAG('w', 'g', 'h', 't'), *AxisRange::Range(200, 400)},
  };
  ab.design_space = {
      {HB_TAG('w', 'g', 'h', 't'), *AxisRange::Range(50, 400)},
  };
  TestUnion(a, b, ab);
}

}  // namespace ift::encoder