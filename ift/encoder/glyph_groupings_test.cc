#include "ift/encoder/glyph_groupings.h"

#include <memory>

#include "absl/container/flat_hash_map.h"
#include "common/font_data.h"
#include "common/int_set.h"
#include "gtest/gtest.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/complex_condition_finder.h"
#include "ift/encoder/dependency_closure.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/glyph_condition_set.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"
#include "ift/freq/probability_bound.h"
#include "ift/encoder/init_subset_defaults.h"

namespace ift::encoder {

using absl::btree_map;
using absl::flat_hash_map;
using common::CodepointSet;
using common::FontData;
using common::GlyphSet;
using common::hb_face_unique_ptr;
using common::make_hb_face;
using common::SegmentSet;
using freq::ProbabilityBound;

void PrintTo(const btree_map<ActivationCondition, common::GlyphSet>& conditions,
             std::ostream* os) {
  *os << "conditions:\n";
  for (const auto& [c, gids] : conditions) {
    *os << "  " << c.ToString() << " => " << gids.ToString();
    if (c.IsExclusive()) {
      *os << " [excl]";
    }
    *os << "\n";
  }
}

class GlyphGroupingsTest : public ::testing::Test {
 protected:
  GlyphGroupingsTest()
      : roboto_(from_file("common/testdata/Roboto-Regular.ttf")),
        segments_({
            Segment({'a', 'b'}, ProbabilityBound::Zero()),  // s0
            Segment({'c', 'd'}, ProbabilityBound::Zero()),  // s1
            Segment({'e', 'f'}, ProbabilityBound::Zero()),  // s2
            Segment({'g'}, ProbabilityBound::Zero()),       // s3
            Segment({'h'}, ProbabilityBound::Zero()),       // s4
        }),

        glyph_groupings_(hb_face_get_glyph_count(roboto_.get())),
        segments_complex_({
            Segment({0x54}, ProbabilityBound::Zero()),    // s0
            Segment({0x6C}, ProbabilityBound::Zero()),    // s1
            Segment({0x13C}, ProbabilityBound::Zero()),   // s2
            Segment({0x21A}, ProbabilityBound::Zero()),   // s3
            Segment({0xF6C3}, ProbabilityBound::Zero()),  // s4
        }),
        glyph_groupings_complex_(hb_face_get_glyph_count(roboto_.get())) {
    uint32_t num_glyphs = hb_face_get_glyph_count(roboto_.get());

    SubsetDefinition init_font_segment;
    AddInitSubsetDefaults(init_font_segment);

    closure_cache_ = std::make_unique<GlyphClosureCache>(roboto_.get());
    requested_segmentation_info_ =
        std::make_unique<RequestedSegmentationInformation>(
            segments_, init_font_segment, *closure_cache_, PATCH);

    glyph_conditions_ = std::make_unique<GlyphConditionSet>(num_glyphs);

    hb_font_t* font = hb_font_create(roboto_.get());
    std::vector<hb_codepoint_t> codepoints = {'a', 'b', 'c', 'd', 'e',
                                              'f', 'g', 'h', 'j', 'k'};
    for (hb_codepoint_t cp : codepoints) {
      glyph_id_t gid;
      hb_font_get_nominal_glyph(font, cp, &gid);
      cp_to_gid_[cp] = gid;
      glyphs_to_group_.insert(gid);
    }
    hb_font_destroy(font);

    // Exclusive glyphs for segment 0
    glyph_conditions_->AddAndCondition(cp_to_gid_['a'], 0);
    glyph_conditions_->AddAndCondition(cp_to_gid_['b'], 0);

    // Exclusive glyphs for segment 1
    glyph_conditions_->AddAndCondition(cp_to_gid_['c'], 1);
    glyph_conditions_->AddAndCondition(cp_to_gid_['d'], 1);

    // Exclusive glyphs for segment 3
    glyph_conditions_->AddAndCondition(cp_to_gid_['k'], 3);
    glyph_conditions_->AddAndCondition(cp_to_gid_['k'], 3);

    // Conjunctive on segments 2 and 3
    glyph_conditions_->AddAndCondition(cp_to_gid_['e'], 2);
    glyph_conditions_->AddAndCondition(cp_to_gid_['e'], 3);
    glyph_conditions_->AddAndCondition(cp_to_gid_['f'], 2);
    glyph_conditions_->AddAndCondition(cp_to_gid_['f'], 3);

    // Disjunctive on segments 3 and 4
    glyph_conditions_->AddOrCondition(cp_to_gid_['g'], 3);
    glyph_conditions_->AddOrCondition(cp_to_gid_['g'], 4);
    glyph_conditions_->AddOrCondition(cp_to_gid_['h'], 3);
    glyph_conditions_->AddOrCondition(cp_to_gid_['h'], 4);

    // Disjunctive on segments 2 and 3
    glyph_conditions_->AddOrCondition(cp_to_gid_['j'], 2);
    glyph_conditions_->AddOrCondition(cp_to_gid_['j'], 3);

    SetupComplexCase();
  }

  void SetupComplexCase() {
    hb_font_t* font = hb_font_create(roboto_.get());
    std::vector<hb_codepoint_t> codepoints = {0x54, 0x6C, 0x13C, 0x21A, 0xF6C3};

    for (hb_codepoint_t cp : codepoints) {
      glyph_id_t gid;
      hb_font_get_nominal_glyph(font, cp, &gid);
      cp_to_gid_[cp] = gid;
      glyphs_to_group_complex_.insert(gid);
    }
    hb_font_destroy(font);

    glyph_conditions_complex_ = std::make_unique<GlyphConditionSet>(
        hb_face_get_glyph_count(roboto_.get()));
    glyph_conditions_complex_->AddAndCondition(ToGlyph(0x54), 0);
    glyph_conditions_complex_->AddAndCondition(ToGlyph(0x6C), 1);

    SubsetDefinition init_font_segment;
    AddInitSubsetDefaults(init_font_segment);

    requested_segmentation_info_complex_ =
        std::make_unique<RequestedSegmentationInformation>(
            segments_complex_, init_font_segment, *closure_cache_,
            FIND_CONDITIONS);
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

  GlyphSet ToGlyphs(CodepointSet codepoints) {
    GlyphSet out;
    for (hb_codepoint_t cp : codepoints) {
      out.insert(cp_to_gid_[cp]);
    }
    return out;
  }

  glyph_id_t ToGlyph(hb_codepoint_t cp) { return cp_to_gid_.at(cp); }

  hb_face_unique_ptr roboto_;
  std::unique_ptr<GlyphClosureCache> closure_cache_;
  std::unique_ptr<RequestedSegmentationInformation>
      requested_segmentation_info_;

  std::vector<Segment> segments_;
  std::unique_ptr<GlyphConditionSet> glyph_conditions_;
  GlyphGroupings glyph_groupings_;
  GlyphSet glyphs_to_group_;
  flat_hash_map<hb_codepoint_t, glyph_id_t> cp_to_gid_;

  std::vector<Segment> segments_complex_;
  std::unique_ptr<GlyphConditionSet> glyph_conditions_complex_;
  GlyphGroupings glyph_groupings_complex_;
  GlyphSet glyphs_to_group_complex_;
  std::unique_ptr<RequestedSegmentationInformation>
      requested_segmentation_info_complex_;
};

TEST_F(GlyphGroupingsTest, SimpleGrouping) {
  auto sc = glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                         *glyph_conditions_, *closure_cache_, std::nullopt,
                                         glyphs_to_group_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  // Condition map:
  // s0 -> {a, b}
  // s1 -> {c, d}
  // s3 -> {k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s3 OR s4 -> {g, h}
  btree_map<ActivationCondition, common::GlyphSet> expected = {
      {ActivationCondition::exclusive_segment(0, 0), ToGlyphs({'a', 'b'})},
      {ActivationCondition::exclusive_segment(1, 0), ToGlyphs({'c', 'd'})},
      {ActivationCondition::exclusive_segment(3, 0), ToGlyphs({'k'})},
      {ActivationCondition::and_segments({2, 3}, 0), ToGlyphs({'e', 'f'})},
      {ActivationCondition::or_segments({2, 3}, 0), ToGlyphs({'j'})},
      {ActivationCondition::or_segments({3, 4}, 0), ToGlyphs({'g', 'h'})},
  };

  ASSERT_EQ(expected, glyph_groupings_.ConditionsAndGlyphs());
}

TEST_F(GlyphGroupingsTest, SegmentChange) {
  auto sc = glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                         *glyph_conditions_, *closure_cache_, std::nullopt,
                                         glyphs_to_group_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  // Now in the glyph condition set combine segments s1 into s0
  // the patch cominbation request
  GlyphConditionSet new_conditions(hb_face_get_glyph_count(roboto_.get()));

  // Exclusive glyphs for segment 0
  new_conditions.AddAndCondition(cp_to_gid_['a'], 0);
  new_conditions.AddAndCondition(cp_to_gid_['b'], 0);
  new_conditions.AddAndCondition(cp_to_gid_['c'], 0);
  new_conditions.AddAndCondition(cp_to_gid_['d'], 0);

  // Exclusive glyphs for segment 3
  new_conditions.AddAndCondition(cp_to_gid_['k'], 3);

  // Conjunctive on segments 2 and 3
  new_conditions.AddAndCondition(cp_to_gid_['e'], 2);
  new_conditions.AddAndCondition(cp_to_gid_['e'], 3);
  new_conditions.AddAndCondition(cp_to_gid_['f'], 2);
  new_conditions.AddAndCondition(cp_to_gid_['f'], 3);

  // Disjunctive on segments 3 and 4
  new_conditions.AddOrCondition(cp_to_gid_['g'], 3);
  new_conditions.AddOrCondition(cp_to_gid_['g'], 4);
  new_conditions.AddOrCondition(cp_to_gid_['h'], 3);
  new_conditions.AddOrCondition(cp_to_gid_['h'], 4);

  // Disjunctive on segments 2 and 3
  new_conditions.AddOrCondition(cp_to_gid_['j'], 2);
  new_conditions.AddOrCondition(cp_to_gid_['j'], 3);

  // Recompute the grouping
  sc = glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                    new_conditions, *closure_cache_, std::nullopt,
                                    glyphs_to_group_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  // Condition map:
  // s0 -> {a, b, c, d}
  // s3 -> {k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s3 OR s4 -> {g, h}
  btree_map<ActivationCondition, common::GlyphSet> expected = {
      {ActivationCondition::exclusive_segment(0, 0),
       ToGlyphs({'a', 'b', 'c', 'd'})},
      {ActivationCondition::exclusive_segment(3, 0), ToGlyphs({'k', 'k'})},
      {ActivationCondition::and_segments({2, 3}, 0), ToGlyphs({'e', 'f'})},
      {ActivationCondition::or_segments({2, 3}, 0), ToGlyphs({'j'})},
      {ActivationCondition::or_segments({3, 4}, 0), ToGlyphs({'g', 'h'})},
  };

  ASSERT_EQ(expected, glyph_groupings_.ConditionsAndGlyphs());
}

TEST_F(GlyphGroupingsTest, CombinePatches) {
  auto sc = glyph_groupings_.CombinePatches(ToGlyphs({'g'}), ToGlyphs({'b'}));
  ASSERT_TRUE(sc.ok()) << sc;

  sc = glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                    *glyph_conditions_, *closure_cache_, std::nullopt,
                                    glyphs_to_group_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  // Condition map:
  // s1 -> {c, d}
  // s3 -> {k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s0 OR s3 OR s4 -> {a, b, g, h}
  btree_map<ActivationCondition, common::GlyphSet> expected = {
      {ActivationCondition::exclusive_segment(1, 0), ToGlyphs({'c', 'd'})},
      {ActivationCondition::exclusive_segment(3, 0), ToGlyphs({'k', 'k'})},
      {ActivationCondition::and_segments({2, 3}, 0), ToGlyphs({'e', 'f'})},
      {ActivationCondition::or_segments({2, 3}, 0), ToGlyphs({'j'})},
      {ActivationCondition::or_segments({0, 3, 4}, 0),
       ToGlyphs({'a', 'b', 'g', 'h'})},
  };

  ASSERT_EQ(expected, glyph_groupings_.ConditionsAndGlyphs());
}

TEST_F(GlyphGroupingsTest, CombinePatches_WithInertSpecialCase) {
  // Initial Condition map:
  // s0 -> {a, b}
  // s1 -> {c, d}
  // s3 -> {k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s3 OR s4 -> {g, h}
  auto sc = glyph_groupings_.CombinePatches(ToGlyphs({'a'}), ToGlyphs({'c'}));
  ASSERT_TRUE(sc.ok()) << sc;

  sc = glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                    *glyph_conditions_, *closure_cache_, std::nullopt,
                                    glyphs_to_group_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  // Combined Condition map:
  // s0 OR s1 -> {a, b, c, d}
  // s3 -> {k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s3 OR s4 -> {g, h}

  // Create a new exclusive patch without using GroupGlyphs(), merges s1 and s3
  // into s3
  sc = glyph_groupings_.AddGlyphsToExclusiveGroup(3, ToGlyphs({'c', 'd', 'k'}));
  ASSERT_TRUE(sc.ok()) << sc;

  // s3 should get pulled into the combined patch of s0 and s1:
  // s0 OR s1 OR s3 -> {a, b, c, d, k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s3 OR s4 -> {g, h}
  btree_map<ActivationCondition, common::GlyphSet> expected = {
      {ActivationCondition::or_segments({0, 3}, 0),
       ToGlyphs({'a', 'b', 'c', 'd', 'k'})},
      {ActivationCondition::and_segments({2, 3}, 0), ToGlyphs({'e', 'f'})},
      {ActivationCondition::or_segments({2, 3}, 0), ToGlyphs({'j'})},
      {ActivationCondition::or_segments({3, 4}, 0), ToGlyphs({'g', 'h'})},
  };

  ASSERT_EQ(expected, glyph_groupings_.ConditionsAndGlyphs());
}

TEST_F(GlyphGroupingsTest, CombinePatches_Invalidates) {
  // Form grouping without union's
  auto sc = glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                         *glyph_conditions_, *closure_cache_, std::nullopt,
                                         glyphs_to_group_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  sc = glyph_groupings_.CombinePatches(ToGlyphs({'g'}), ToGlyphs({'b'}));
  ASSERT_TRUE(sc.ok()) << sc;

  sc =
      glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                   *glyph_conditions_, *closure_cache_, std::nullopt, {}, {});
  ASSERT_TRUE(sc.ok()) << sc;

  // UnionPatches + GroupGlyphs() will automatically invalidate and then fix
  // the groupings as needed, so condition map should now be:
  // s1 -> {c, d}
  // s3 -> {k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s0 OR s3 OR s4 -> {a, b, g, h}
  btree_map<ActivationCondition, common::GlyphSet> expected = {
      {ActivationCondition::exclusive_segment(1, 0), ToGlyphs({'c', 'd'})},
      {ActivationCondition::exclusive_segment(3, 0), ToGlyphs({'k', 'k'})},
      {ActivationCondition::and_segments({2, 3}, 0), ToGlyphs({'e', 'f'})},
      {ActivationCondition::or_segments({2, 3}, 0), ToGlyphs({'j'})},
      {ActivationCondition::or_segments({0, 3, 4}, 0),
       ToGlyphs({'a', 'b', 'g', 'h'})},
  };

  ASSERT_EQ(expected, glyph_groupings_.ConditionsAndGlyphs());
}

TEST_F(GlyphGroupingsTest, CombinePatches_PartialUpdate) {
  // Form grouping without union's
  auto sc = glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                         *glyph_conditions_, *closure_cache_, std::nullopt,
                                         glyphs_to_group_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  sc = glyph_groupings_.CombinePatches(ToGlyphs({'g'}), ToGlyphs({'b'}));
  ASSERT_TRUE(sc.ok()) << sc;

  // Now simulate a partial invalidation that intersects the merged patch
  // and see if grouping correctly reforms the full mapping. Invalidates:
  // s0 -> {a, b}
  sc = glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                    *glyph_conditions_, *closure_cache_, std::nullopt,
                                    ToGlyphs({'a', 'b'}), {});
  ASSERT_TRUE(sc.ok()) << sc;

  // Expected condition map:
  // s1 -> {c, d}
  // s3 -> {k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s0 OR s3 OR s4 -> {a, b, g, h}
  btree_map<ActivationCondition, common::GlyphSet> expected = {
      {ActivationCondition::exclusive_segment(1, 0), ToGlyphs({'c', 'd'})},
      {ActivationCondition::exclusive_segment(3, 0), ToGlyphs({'k', 'k'})},
      {ActivationCondition::and_segments({2, 3}, 0), ToGlyphs({'e', 'f'})},
      {ActivationCondition::or_segments({2, 3}, 0), ToGlyphs({'j'})},
      {ActivationCondition::or_segments({0, 3, 4}, 0),
       ToGlyphs({'a', 'b', 'g', 'h'})},
  };

  ASSERT_EQ(expected, glyph_groupings_.ConditionsAndGlyphs());

  // Do another partial invalidation this time on: s3 OR s4 -> {g, h}
  sc = glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                    *glyph_conditions_, *closure_cache_, std::nullopt,
                                    ToGlyphs({'g', 'h'}), {});
  ASSERT_TRUE(sc.ok()) << sc;

  // Groupings should still be the same.
  ASSERT_EQ(expected, glyph_groupings_.ConditionsAndGlyphs());
}

TEST_F(GlyphGroupingsTest, CombinePatches_Noop) {
  auto sc = glyph_groupings_.CombinePatches(ToGlyphs({'a'}), ToGlyphs({'b'}));
  ASSERT_TRUE(sc.ok()) << sc;
  sc = glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                    *glyph_conditions_, *closure_cache_, std::nullopt,
                                    glyphs_to_group_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  // The combination is a noop since it only combines things already in the
  // same patch. Expected condition map:
  // s0 -> {a, b}
  // s1 -> {c, d}
  // s3 -> {k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s3 OR s4 -> {g, h}
  btree_map<ActivationCondition, common::GlyphSet> expected = {
      {ActivationCondition::exclusive_segment(0, 0), ToGlyphs({'a', 'b'})},
      {ActivationCondition::exclusive_segment(1, 0), ToGlyphs({'c', 'd'})},
      {ActivationCondition::exclusive_segment(3, 0), ToGlyphs({'k', 'k'})},
      {ActivationCondition::and_segments({2, 3}, 0), ToGlyphs({'e', 'f'})},
      {ActivationCondition::or_segments({2, 3}, 0), ToGlyphs({'j'})},
      {ActivationCondition::or_segments({3, 4}, 0), ToGlyphs({'g', 'h'})},
  };

  ASSERT_EQ(expected, glyph_groupings_.ConditionsAndGlyphs());
}

TEST_F(GlyphGroupingsTest, CombinePatches_DoesntAffectConjunction) {
  auto sc = glyph_groupings_.CombinePatches(ToGlyphs({'d'}), ToGlyphs({'e'}));
  ASSERT_TRUE(sc.ok()) << sc;

  sc = glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                    *glyph_conditions_, *closure_cache_, std::nullopt,
                                    glyphs_to_group_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  // Condition map:
  // s0 -> {a, b}
  // s1 -> {c, d}
  // s3 -> {k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s3 OR s4 -> {g, h}
  btree_map<ActivationCondition, common::GlyphSet> expected = {
      {ActivationCondition::exclusive_segment(0, 0), ToGlyphs({'a', 'b'})},
      {ActivationCondition::exclusive_segment(1, 0), ToGlyphs({'c', 'd'})},
      {ActivationCondition::exclusive_segment(3, 0), ToGlyphs({'k', 'k'})},
      {ActivationCondition::and_segments({2, 3}, 0), ToGlyphs({'e', 'f'})},
      {ActivationCondition::or_segments({2, 3}, 0), ToGlyphs({'j'})},
      {ActivationCondition::or_segments({3, 4}, 0), ToGlyphs({'g', 'h'})},
  };

  ASSERT_EQ(expected, glyph_groupings_.ConditionsAndGlyphs());
}

TEST_F(GlyphGroupingsTest, CombinePatches_SegmentChanges) {
  auto sc = glyph_groupings_.CombinePatches(ToGlyphs({'g'}), ToGlyphs({'b'}));
  ASSERT_TRUE(sc.ok()) << sc;

  sc = glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                    *glyph_conditions_, *closure_cache_, std::nullopt,
                                    glyphs_to_group_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  // Now in the glyph condition set combine segments s1 into s0
  // the patch cominbation request
  GlyphConditionSet new_conditions(hb_face_get_glyph_count(roboto_.get()));

  // Exclusive glyphs for segment 0
  new_conditions.AddAndCondition(cp_to_gid_['a'], 0);
  new_conditions.AddAndCondition(cp_to_gid_['b'], 0);
  new_conditions.AddAndCondition(cp_to_gid_['c'], 0);
  new_conditions.AddAndCondition(cp_to_gid_['d'], 0);

  // Exclusive glyphs for segment 3
  new_conditions.AddAndCondition(cp_to_gid_['k'], 3);
  new_conditions.AddAndCondition(cp_to_gid_['k'], 3);

  // Conjunctive on segments 2 and 3
  new_conditions.AddAndCondition(cp_to_gid_['e'], 2);
  new_conditions.AddAndCondition(cp_to_gid_['e'], 3);
  new_conditions.AddAndCondition(cp_to_gid_['f'], 2);
  new_conditions.AddAndCondition(cp_to_gid_['f'], 3);

  // Disjunctive on segments 3 and 4
  new_conditions.AddOrCondition(cp_to_gid_['g'], 3);
  new_conditions.AddOrCondition(cp_to_gid_['g'], 4);
  new_conditions.AddOrCondition(cp_to_gid_['h'], 3);
  new_conditions.AddOrCondition(cp_to_gid_['h'], 4);

  // Disjunctive on segments 2 and 3
  new_conditions.AddOrCondition(cp_to_gid_['j'], 2);
  new_conditions.AddOrCondition(cp_to_gid_['j'], 3);

  // Recompute the grouping
  sc = glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                    new_conditions, *closure_cache_, std::nullopt,
                                    ToGlyphs({'a', 'b', 'c', 'd'}), {});
  ASSERT_TRUE(sc.ok()) << sc;

  // Condition map:
  // s3 -> {k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s0 OR s3 OR s4 -> {a, b, c, , d, g, h}
  btree_map<ActivationCondition, common::GlyphSet> expected = {
      {ActivationCondition::exclusive_segment(3, 0), ToGlyphs({'k', 'k'})},
      {ActivationCondition::and_segments({2, 3}, 0), ToGlyphs({'e', 'f'})},
      {ActivationCondition::or_segments({2, 3}, 0), ToGlyphs({'j'})},
      {ActivationCondition::or_segments({0, 3, 4}, 0),
       ToGlyphs({'a', 'b', 'c', 'd', 'g', 'h'})},
  };

  ASSERT_EQ(expected, glyph_groupings_.ConditionsAndGlyphs());
}

TEST_F(GlyphGroupingsTest, EqualityRespectsPatchCombination) {
  auto sc = glyph_groupings_.CombinePatches(ToGlyphs({'g'}), ToGlyphs({'b'}));
  ASSERT_TRUE(sc.ok()) << sc;

  sc = glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                    *glyph_conditions_, *closure_cache_, std::nullopt,
                                    glyphs_to_group_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  GlyphGroupings other(hb_face_get_glyph_count(roboto_.get()));
  sc = other.GroupGlyphs(*requested_segmentation_info_, *glyph_conditions_,
                         *closure_cache_, std::nullopt, glyphs_to_group_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  // other does not have the same patch combinations and so should not be equal
  // to glyph_groupings_
  ASSERT_FALSE(glyph_groupings_ == other);

  sc = other.CombinePatches(ToGlyphs({'g'}), ToGlyphs({'b'}));
  ASSERT_TRUE(sc.ok());

  sc = other.GroupGlyphs(*requested_segmentation_info_, *glyph_conditions_,
                         *closure_cache_, std::nullopt, glyphs_to_group_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  // Now that combined patches matches they should be equal.
  ASSERT_TRUE(glyph_groupings_ == other);
}

TEST_F(GlyphGroupingsTest, ExclusiveGlyphsRespectsPatchCombinations) {
  auto sc = glyph_groupings_.CombinePatches(ToGlyphs({'g'}), ToGlyphs({'b'}));
  ASSERT_TRUE(sc.ok()) << sc;

  sc = glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                    *glyph_conditions_, *closure_cache_, std::nullopt,
                                    glyphs_to_group_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  // Condition map:
  // s1 -> {c, d}
  // s3 -> {k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s0 OR s3 OR s4 -> {a, b, g, h}

  ASSERT_EQ(glyph_groupings_.ExclusiveGlyphs(0), (GlyphSet{}));
  ASSERT_EQ(glyph_groupings_.ExclusiveGlyphs(1), ToGlyphs({'c', 'd'}));
  ASSERT_EQ(glyph_groupings_.ExclusiveGlyphs(2), (GlyphSet{}));
  ASSERT_EQ(glyph_groupings_.ExclusiveGlyphs(3), ToGlyphs({'k'}));
  ASSERT_EQ(glyph_groupings_.ExclusiveGlyphs(10), (GlyphSet{}));
}

TEST_F(GlyphGroupingsTest, ComplexConditionFinding_LeaveUnmapped) {
  SubsetDefinition init_font_segment;
  AddInitSubsetDefaults(init_font_segment);
  RequestedSegmentationInformation segmentation_info(
      segments_complex_, init_font_segment, *closure_cache_, PATCH);

  auto sc = glyph_groupings_complex_.GroupGlyphs(
      segmentation_info, *glyph_conditions_complex_, *closure_cache_, std::nullopt,
      glyphs_to_group_complex_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  // Condition map:
  // if (s0) then p0 => {56} [excl]
  // if (s1) then p0 => {80} [excl]
  btree_map<ActivationCondition, common::GlyphSet> expected = {
      {ActivationCondition::exclusive_segment(0, 0), ToGlyphs({0x54})},
      {ActivationCondition::exclusive_segment(1, 0), ToGlyphs({0x6C})},
  };

  ASSERT_EQ(expected, glyph_groupings_complex_.ConditionsAndGlyphs());
  ASSERT_EQ(glyph_groupings_complex_.UnmappedGlyphs(),
            (GlyphSet{442, 748, 782}));
}

TEST_F(GlyphGroupingsTest, ComplexConditionFinding_Basic) {
  auto sc = glyph_groupings_complex_.GroupGlyphs(
      *requested_segmentation_info_complex_, *glyph_conditions_complex_,
      *closure_cache_, std::nullopt, glyphs_to_group_complex_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  // Condition map:
  // if (s0) then p0 => {56} [excl]
  // if (s1) then p0 => {80} [excl]
  // if ((s0 OR s3 OR s4)) then p0 => {782}
  // if ((s1 OR s2 OR s4)) then p0 => {748}
  // if ((s2 OR s3 OR s4)) then p0 => {442}
  btree_map<ActivationCondition, common::GlyphSet> expected = {
      {ActivationCondition::exclusive_segment(0, 0), ToGlyphs({0x54})},
      {ActivationCondition::exclusive_segment(1, 0), ToGlyphs({0x6C})},

      {ActivationCondition::or_segments({0, 3, 4}, 0), {782}},
      {ActivationCondition::or_segments({1, 2, 4}, 0), {748}},
      {ActivationCondition::or_segments({2, 3, 4}, 0), {442}},
  };

  ASSERT_EQ(expected, glyph_groupings_complex_.ConditionsAndGlyphs());
  ASSERT_TRUE(glyph_groupings_complex_.UnmappedGlyphs().empty());
}

TEST_F(GlyphGroupingsTest, ComplexConditionFinding_Basic_WithDepedencyGraph) {
  std::unique_ptr<DependencyClosure> dep_closure = *DependencyClosure::Create(
    requested_segmentation_info_complex_.get(), roboto_.get());

  auto sc = glyph_groupings_complex_.GroupGlyphs(
      *requested_segmentation_info_complex_, *glyph_conditions_complex_,
      *closure_cache_, dep_closure.get(), glyphs_to_group_complex_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  // Condition map:
  // if (s0) then p0 => {56} [excl]
  // if (s1) then p0 => {80} [excl]
  // if ((s0 OR s3 OR s4)) then p0 => {782}
  // if ((s1 OR s2 OR s4)) then p0 => {748}
  // if ((s2 OR s3 OR s4)) then p0 => {442}
  btree_map<ActivationCondition, common::GlyphSet> expected = {
      {ActivationCondition::exclusive_segment(0, 0), ToGlyphs({0x54})},
      {ActivationCondition::exclusive_segment(1, 0), ToGlyphs({0x6C})},

      {ActivationCondition::or_segments({0, 3, 4}, 0), {782}},
      {ActivationCondition::or_segments({1, 2, 4}, 0), {748}},
      {ActivationCondition::or_segments({2, 3, 4}, 0), {442}},
  };

  ASSERT_EQ(expected, glyph_groupings_complex_.ConditionsAndGlyphs());
  ASSERT_TRUE(glyph_groupings_complex_.UnmappedGlyphs().empty());

  // Also try incremental
  sc = glyph_groupings_complex_.GroupGlyphs(
      *requested_segmentation_info_complex_, *glyph_conditions_complex_,
      *closure_cache_, dep_closure.get(), {748}, {});
  ASSERT_TRUE(sc.ok()) << sc;
  ASSERT_EQ(expected, glyph_groupings_complex_.ConditionsAndGlyphs());
  ASSERT_TRUE(glyph_groupings_complex_.UnmappedGlyphs().empty());
}

TEST_F(GlyphGroupingsTest, ComplexConditionFinding_IncrementalUnchanged) {
  auto sc = glyph_groupings_complex_.GroupGlyphs(
      *requested_segmentation_info_complex_, *glyph_conditions_complex_,
      *closure_cache_, std::nullopt, glyphs_to_group_complex_, {});
  ASSERT_TRUE(sc.ok()) << sc;
  btree_map<ActivationCondition, common::GlyphSet> expected =
      glyph_groupings_complex_.ConditionsAndGlyphs();

  // Incremental regrouping, should arrive back at the same mapping.
  sc = glyph_groupings_complex_.GroupGlyphs(
      *requested_segmentation_info_complex_, *glyph_conditions_complex_,
      *closure_cache_, std::nullopt, {748}, {});
  ASSERT_TRUE(sc.ok()) << sc;
  ASSERT_EQ(expected, glyph_groupings_complex_.ConditionsAndGlyphs());
  ASSERT_TRUE(glyph_groupings_complex_.UnmappedGlyphs().empty());

  sc = glyph_groupings_complex_.GroupGlyphs(
      *requested_segmentation_info_complex_, *glyph_conditions_complex_,
      *closure_cache_, std::nullopt, ToGlyphs({0x54}), {2});
  ASSERT_TRUE(sc.ok()) << sc;
  ASSERT_EQ(expected, glyph_groupings_complex_.ConditionsAndGlyphs());
  ASSERT_TRUE(glyph_groupings_complex_.UnmappedGlyphs().empty());
}

TEST_F(GlyphGroupingsTest, ComplexConditionFinding_IncrementalChanged) {
  auto sc = glyph_groupings_complex_.GroupGlyphs(
      *requested_segmentation_info_complex_, *glyph_conditions_complex_,
      *closure_cache_, std::nullopt, glyphs_to_group_complex_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  // Modify a segment definition and then incremental recompute the groupings.
  Segment merged = segments_complex_[0];
  merged.Definition().Union(segments_complex_[3].Definition());
  requested_segmentation_info_complex_->AssignMergedSegment(0, {3}, merged);

  // Incremental regrouping, should arrive back at the same mapping.
  sc = glyph_groupings_complex_.GroupGlyphs(
      *requested_segmentation_info_complex_, *glyph_conditions_complex_,
      *closure_cache_, std::nullopt, ToGlyphs({0x54}), {0, 3});
  ASSERT_TRUE(sc.ok()) << sc;

  btree_map<ActivationCondition, common::GlyphSet> expected = {
      {ActivationCondition::exclusive_segment(0, 0), ToGlyphs({0x54})},
      {ActivationCondition::exclusive_segment(1, 0), ToGlyphs({0x6C})},
      {ActivationCondition::or_segments({1, 2, 4}, 0), {748}},
  };

  auto new_mappings = *FindSupersetDisjunctiveConditionsFor(
      *requested_segmentation_info_complex_, *glyph_conditions_complex_,
      *closure_cache_, {442, 782}, SegmentSet::all());

  for (const auto& [s, g] : new_mappings) {
    if (s.size() == 1) {
      expected[ActivationCondition::exclusive_segment(*s.begin(), 0)].union_set(
          g);
    } else {
      expected[ActivationCondition::or_segments(s, 0)].union_set(g);
    }
  }

  ASSERT_EQ(expected, glyph_groupings_complex_.ConditionsAndGlyphs());
  ASSERT_TRUE(glyph_groupings_complex_.UnmappedGlyphs().empty());
}

TEST_F(GlyphGroupingsTest, ComplexConditionFinding_CombinedPatches) {
  auto sc = glyph_groupings_complex_.CombinePatches({ToGlyph(0x6C)}, {782});
  ASSERT_TRUE(sc.ok()) << sc;

  sc = glyph_groupings_complex_.GroupGlyphs(
      *requested_segmentation_info_complex_, *glyph_conditions_complex_,
      *closure_cache_, std::nullopt, glyphs_to_group_complex_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  // Condition map:
  // if (s0) then p0 => {56} [excl]
  // if ((s0 OR s1 OR s3 OR s4)) then p0 => {80, 782}
  // if ((s1 OR s2 OR s4)) then p0 => {748}
  // if ((s2 OR s3 OR s4)) then p0 => {442}
  btree_map<ActivationCondition, common::GlyphSet> expected = {
      {ActivationCondition::exclusive_segment(0, 0), ToGlyphs({0x54})},

      {ActivationCondition::or_segments({0, 1, 3, 4}, 0), {ToGlyph(0x6C), 782}},
      {ActivationCondition::or_segments({1, 2, 4}, 0), {748}},
      {ActivationCondition::or_segments({2, 3, 4}, 0), {442}},
  };

  ASSERT_EQ(expected, glyph_groupings_complex_.ConditionsAndGlyphs());
  ASSERT_TRUE(glyph_groupings_complex_.UnmappedGlyphs().empty());
}

TEST_F(GlyphGroupingsTest,
       ComplexConditionFinding_IncrementalAndCombinedPatches) {
  auto sc = glyph_groupings_complex_.CombinePatches({ToGlyph(0x6C)}, {782});
  ASSERT_TRUE(sc.ok()) << sc;

  sc = glyph_groupings_complex_.GroupGlyphs(
      *requested_segmentation_info_complex_, *glyph_conditions_complex_,
      *closure_cache_, std::nullopt, glyphs_to_group_complex_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  // Modify a segment definition and then incremental recompute the groupings.
  Segment merged = segments_complex_[0];
  merged.Definition().Union(segments_complex_[3].Definition());
  requested_segmentation_info_complex_->AssignMergedSegment(0, {3}, merged);

  sc = glyph_groupings_complex_.GroupGlyphs(
      *requested_segmentation_info_complex_, *glyph_conditions_complex_,
      *closure_cache_, std::nullopt, ToGlyphs({0x54}), {0, 3});
  ASSERT_TRUE(sc.ok()) << sc;

  // Condition map:
  // if ((s0 OR s1)) then p0 => {56, 80, 782}
  // if ((s1 OR s2 OR s4)) then p0 => {748}
  // if ((s0 OR s2 OR s4)) then p0 => {442}
  btree_map<ActivationCondition, common::GlyphSet> expected = {
      {ActivationCondition::or_segments({0, 1}, 0),
       {ToGlyph(0x54), ToGlyph(0x6C), 782}},
      {ActivationCondition::or_segments({1, 2, 4}, 0), {748}},
      {ActivationCondition::or_segments({0, 2, 4}, 0), {442}},
  };

  ASSERT_EQ(expected, glyph_groupings_complex_.ConditionsAndGlyphs());
  ASSERT_TRUE(glyph_groupings_complex_.UnmappedGlyphs().empty());
}

}  // namespace ift::encoder
