#include "ift/encoder/complex_condition_finder.h"

#include "absl/container/btree_map.h"
#include "common/font_data.h"
#include "common/int_set.h"
#include "gtest/gtest.h"
#include "ift/encoder/closure_glyph_segmenter.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/segmentation_context.h"
#include "ift/encoder/types.h"
#include "ift/freq/probability_bound.h"

using absl::btree_map;
using absl::StatusOr;
using common::FontData;
using common::GlyphSet;
using common::hb_face_unique_ptr;
using common::make_hb_face;
using common::SegmentSet;
using ift::freq::ProbabilityBound;

namespace ift::encoder {

class ComplexConditionFinderTest : public ::testing::Test {
 protected:
  ComplexConditionFinderTest()
      : roboto(make_hb_face(nullptr)), segmenter(1, 1) {
    roboto = from_file("common/testdata/Roboto-Regular.ttf");
  }

  hb_face_unique_ptr from_file(const char* filename) {
    hb_blob_t* blob = hb_blob_create_from_file_or_fail(filename);
    if (!blob) {
      assert(false);
    }
    FontData result(blob);
    hb_blob_destroy(blob);
    return result.face();
  }

  SubsetDefinition CombinedDefinition(const SegmentationContext& context,
                                      const SegmentSet& segments) {
    SubsetDefinition def;
    for (segment_index_t s : segments) {
      def.Union(context.SegmentationInfo().Segments().at(s).Definition());
    }
    return def;
  }

  StatusOr<GlyphSet> SegmentClosure(SegmentationContext& context,
                                    const SegmentSet& segments) {
    SubsetDefinition closure_def = CombinedDefinition(context, segments);
    return context.glyph_closure_cache.GlyphClosure(closure_def);
  }

  SegmentationContext TestContext(bool basic_closure_analysis) {
    auto context = *segmenter.InitializeSegmentationContext(
        roboto.get(), {'f'},
        {
            /* 0 */ {{0x54}, ProbabilityBound::Zero()},
            /* 1 */ {{0x6C}, ProbabilityBound::Zero()},
            /* 2 */ {{0x6E}, ProbabilityBound::Zero()},
            /* 3 */ {{0x13C}, ProbabilityBound::Zero()},
            /* 4 */ {{0x146}, ProbabilityBound::Zero()},
            /* 5 */ {{0x21A}, ProbabilityBound::Zero()},
            /* 6 */ {{0xF6C3}, ProbabilityBound::Zero()},
            /* 7 */ {{0x69}, ProbabilityBound::Zero()},
        });

    if (!basic_closure_analysis) {
      // initialaztion populates the basic conditions, clear those
      // out so we can control them.
      context.glyph_condition_set.InvalidateGlyphInformation(
          {748, 756, 782}, {0, 1, 2, 3, 4, 5, 6});
    }

    return context;
  }

  hb_face_unique_ptr roboto;
  ClosureGlyphSegmenter segmenter;

  // Expected complex conditions:
  //
  // 0xF6C3, 0x54, 0x21A => g782
  // 0xF6C3, 0x6C, 0x13C => g748
  // 0xF6C3, 0x6E, 0x146 => g756
  btree_map<common::SegmentSet, common::GlyphSet> expected = {
      {{6, 1, 3}, {748}},
      {{6, 2, 4}, {756}},
      {{6, 0, 5}, {782}},
  };
};

TEST_F(ComplexConditionFinderTest, FindConditions) {
  SegmentationContext context = TestContext(false);

  auto r = FindSupersetDisjunctiveConditionsFor(context.SegmentationInfo(),
                                                context.glyph_condition_set,
                                                context.glyph_closure_cache,
                                                {
                                                    748,
                                                    756,
                                                    782,
                                                });
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_EQ(expected, *r);

  // Verify that the closure requirement is met. If all segments from
  // the minimal condition are excluded then the mapped gid should not
  // appear in the closure.
  const SegmentSet all = {0, 1, 2, 3, 4, 5, 6};
  for (const auto& [segments, gids] : *r) {
    SegmentSet except = all;
    except.subtract(segments);

    GlyphSet closure = *SegmentClosure(context, except);
    ASSERT_FALSE(closure.intersects(gids));

    closure = *SegmentClosure(context, segments);
    ASSERT_TRUE(gids.is_subset_of(closure));
  }
}

TEST_F(ComplexConditionFinderTest, FindConditions_Partial) {
  SegmentationContext context = TestContext(false);

  auto r = FindSupersetDisjunctiveConditionsFor(context.SegmentationInfo(),
                                                context.glyph_condition_set,
                                                context.glyph_closure_cache,
                                                {
                                                    748,
                                                });
  ASSERT_TRUE(r.ok()) << r.status();
  expected.erase(SegmentSet{6, 0, 5});
  expected.erase(SegmentSet{6, 2, 4});
  ASSERT_EQ(expected, *r);
}

TEST_F(ComplexConditionFinderTest, FindConditions_IncompleteExistingCondition) {
  SegmentationContext context = TestContext(false);

  context.glyph_condition_set.AddOrCondition(748, 6);
  auto r = FindSupersetDisjunctiveConditionsFor(context.SegmentationInfo(),
                                                context.glyph_condition_set,
                                                context.glyph_closure_cache,
                                                {
                                                    748,
                                                });
  ASSERT_TRUE(absl::IsInvalidArgument(r.status())) << r.status();
}

TEST_F(ComplexConditionFinderTest, FindConditions_GlyphsNotInClosure) {
  SegmentationContext context = TestContext(false);

  auto r = FindSupersetDisjunctiveConditionsFor(
      context.SegmentationInfo(), context.glyph_condition_set,
      context.glyph_closure_cache,
      {
          748,
          40  // this is not in the full closure.
      });
  ASSERT_TRUE(absl::IsInvalidArgument(r.status())) << r.status();
}

TEST_F(ComplexConditionFinderTest,
       FindConditions_WithExistingConditions_FromClosureAnalysis) {
  SegmentationContext context = TestContext(true);

  auto r = FindSupersetDisjunctiveConditionsFor(context.SegmentationInfo(),
                                                context.glyph_condition_set,
                                                context.glyph_closure_cache,
                                                {
                                                    748,
                                                    756,
                                                    782,
                                                });
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_EQ(expected, *r);
}

TEST_F(ComplexConditionFinderTest, FindConditions_WithExistingConditions) {
  SegmentationContext context = TestContext(false);

  context.glyph_condition_set.AddOrCondition(748, 1);
  context.glyph_condition_set.AddOrCondition(748, 6);

  auto r = FindSupersetDisjunctiveConditionsFor(context.SegmentationInfo(),
                                                context.glyph_condition_set,
                                                context.glyph_closure_cache,
                                                {
                                                    748,
                                                    756,
                                                    782,
                                                });
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_EQ(expected, *r);
}

TEST_F(ComplexConditionFinderTest,
       FindConditions_WithExistingConditions_NoAdditionalConditions) {
  SegmentationContext context = TestContext(false);

  context.glyph_condition_set.AddOrCondition(748, 1);
  context.glyph_condition_set.AddOrCondition(748, 3);
  context.glyph_condition_set.AddOrCondition(748, 6);

  auto r = FindSupersetDisjunctiveConditionsFor(context.SegmentationInfo(),
                                                context.glyph_condition_set,
                                                context.glyph_closure_cache,
                                                {
                                                    748,
                                                    756,
                                                    782,
                                                });
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_EQ(expected, *r);
}

TEST_F(ComplexConditionFinderTest, FindConditions_RejectsInitFontGlyphs) {
  SegmentationContext context = TestContext(false);

  auto r = FindSupersetDisjunctiveConditionsFor(
      context.SegmentationInfo(), context.glyph_condition_set,
      context.glyph_closure_cache,
      {
          748,
          74,  // f - in the init closure
      });
  ASSERT_TRUE(absl::IsInvalidArgument(r.status())) << r.status();
}

TEST_F(ComplexConditionFinderTest, FindConditions_ClosureRespectsInitFont) {
  SegmentationContext context = TestContext(false);

  auto r = FindSupersetDisjunctiveConditionsFor(
      context.SegmentationInfo(), context.glyph_condition_set,
      context.glyph_closure_cache,
      {
          446,  // fi ligature - combines i with f from the init font
      });
  ASSERT_TRUE(r.ok()) << r.status();
  expected = {
      {{7}, {446}},
  };
  ASSERT_EQ(expected, *r);
}

}  // namespace ift::encoder