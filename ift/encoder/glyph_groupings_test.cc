#include "ift/encoder/glyph_groupings.h"

#include "absl/container/flat_hash_map.h"
#include "common/font_data.h"
#include "common/int_set.h"
#include "gtest/gtest.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/glyph_condition_set.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"
#include "ift/freq/probability_bound.h"

namespace ift::encoder {

using absl::flat_hash_map;
using common::CodepointSet;
using common::FontData;
using common::GlyphSet;
using common::hb_face_unique_ptr;
using common::make_hb_face;
using freq::ProbabilityBound;

void PrintTo(
    const absl::btree_map<ActivationCondition, common::GlyphSet>& conditions,
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
        glyph_groupings_(segments_, hb_face_get_glyph_count(roboto_.get())) {
    uint32_t num_glyphs = hb_face_get_glyph_count(roboto_.get());

    SubsetDefinition init_font_segment;
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

  glyph_id_t ToGlyph(hb_codepoint_t cp) { return cp_to_gid_[cp]; }

  hb_face_unique_ptr roboto_;
  std::vector<Segment> segments_;
  std::unique_ptr<GlyphClosureCache> closure_cache_;
  std::unique_ptr<RequestedSegmentationInformation>
      requested_segmentation_info_;
  std::unique_ptr<GlyphConditionSet> glyph_conditions_;
  GlyphGroupings glyph_groupings_;
  GlyphSet glyphs_to_group_;
  flat_hash_map<hb_codepoint_t, glyph_id_t> cp_to_gid_;
};

TEST_F(GlyphGroupingsTest, SimpleGrouping) {
  auto sc = glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                         *glyph_conditions_, *closure_cache_,
                                         glyphs_to_group_);
  ASSERT_TRUE(sc.ok()) << sc;

  // Condition map:
  // s0 -> {a, b}
  // s1 -> {c, d}
  // s3 -> {k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s3 OR s4 -> {g, h}
  absl::btree_map<ActivationCondition, common::GlyphSet> expected = {
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
                                         *glyph_conditions_, *closure_cache_,
                                         glyphs_to_group_);
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
                                    new_conditions, *closure_cache_,
                                    glyphs_to_group_);
  ASSERT_TRUE(sc.ok()) << sc;

  // Condition map:
  // s0 -> {a, b, c, d}
  // s3 -> {k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s3 OR s4 -> {g, h}
  absl::btree_map<ActivationCondition, common::GlyphSet> expected = {
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
                                    *glyph_conditions_, *closure_cache_,
                                    glyphs_to_group_);
  ASSERT_TRUE(sc.ok()) << sc;

  // Condition map:
  // s1 -> {c, d}
  // s3 -> {k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s0 OR s3 OR s4 -> {a, b, g, h}
  absl::btree_map<ActivationCondition, common::GlyphSet> expected = {
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
                                    *glyph_conditions_, *closure_cache_,
                                    glyphs_to_group_);
  ASSERT_TRUE(sc.ok()) << sc;

  // Combined Condition map:
  // s0 OR s1 -> {a, b, c, d}
  // s3 -> {k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s3 OR s4 -> {g, h}

  // Create a new exclusive patch without using GroupGlyphs(), merges s1 and s3
  // into s3
  glyph_conditions_->InvalidateGlyphInformation(ToGlyphs({'c', 'd', 'k'}),
                                                {1, 3});
  glyph_conditions_->AddAndCondition(ToGlyph('c'), 3);
  glyph_conditions_->AddAndCondition(ToGlyph('d'), 3);
  glyph_conditions_->AddAndCondition(ToGlyph('k'), 3);

  sc = glyph_groupings_.AddGlyphsToExclusiveGroup(*glyph_conditions_, 3,
                                                  ToGlyphs({'c', 'd', 'k'}));
  ASSERT_TRUE(sc.ok()) << sc;

  // s3 should get pulled into the combined patch of s0 and s1:
  // s0 OR s1 OR s3 -> {a, b, c, d, k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s3 OR s4 -> {g, h}
  absl::btree_map<ActivationCondition, common::GlyphSet> expected = {
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
                                         *glyph_conditions_, *closure_cache_,
                                         glyphs_to_group_);
  ASSERT_TRUE(sc.ok()) << sc;

  sc = glyph_groupings_.CombinePatches(ToGlyphs({'g'}), ToGlyphs({'b'}));
  ASSERT_TRUE(sc.ok()) << sc;

  sc = glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                    *glyph_conditions_, *closure_cache_, {});
  ASSERT_TRUE(sc.ok()) << sc;

  // UnionPatches + GroupGlyphs() will automatically invalidate and then fix
  // the groupings as needed, so condition map should now be:
  // s1 -> {c, d}
  // s3 -> {k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s0 OR s3 OR s4 -> {a, b, g, h}
  absl::btree_map<ActivationCondition, common::GlyphSet> expected = {
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
                                         *glyph_conditions_, *closure_cache_,
                                         glyphs_to_group_);
  ASSERT_TRUE(sc.ok()) << sc;

  sc = glyph_groupings_.CombinePatches(ToGlyphs({'g'}), ToGlyphs({'b'}));
  ASSERT_TRUE(sc.ok()) << sc;

  // Now simulate a partial invalidation that intersects the merged patch
  // and see if grouping correctly reforms the full mapping. Invalidates:
  // s0 -> {a, b}
  sc = glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                    *glyph_conditions_, *closure_cache_,
                                    ToGlyphs({'a', 'b'}));
  ASSERT_TRUE(sc.ok()) << sc;

  // Expected condition map:
  // s1 -> {c, d}
  // s3 -> {k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s0 OR s3 OR s4 -> {a, b, g, h}
  absl::btree_map<ActivationCondition, common::GlyphSet> expected = {
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
                                    *glyph_conditions_, *closure_cache_,
                                    ToGlyphs({'g', 'h'}));
  ASSERT_TRUE(sc.ok()) << sc;

  // Groupings should still be the same.
  ASSERT_EQ(expected, glyph_groupings_.ConditionsAndGlyphs());
}

TEST_F(GlyphGroupingsTest, CombinePatches_Noop) {
  auto sc = glyph_groupings_.CombinePatches(ToGlyphs({'a'}), ToGlyphs({'b'}));
  ASSERT_TRUE(sc.ok()) << sc;
  sc = glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                    *glyph_conditions_, *closure_cache_,
                                    glyphs_to_group_);
  ASSERT_TRUE(sc.ok()) << sc;

  // The combination is a noop since it only combines things already in the
  // same patch. Expected condition map:
  // s0 -> {a, b}
  // s1 -> {c, d}
  // s3 -> {k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s3 OR s4 -> {g, h}
  absl::btree_map<ActivationCondition, common::GlyphSet> expected = {
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
                                    *glyph_conditions_, *closure_cache_,
                                    glyphs_to_group_);
  ASSERT_TRUE(sc.ok()) << sc;

  // Condition map:
  // s0 -> {a, b}
  // s1 -> {c, d}
  // s3 -> {k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s3 OR s4 -> {g, h}
  absl::btree_map<ActivationCondition, common::GlyphSet> expected = {
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
                                    *glyph_conditions_, *closure_cache_,
                                    glyphs_to_group_);
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
                                    new_conditions, *closure_cache_,
                                    ToGlyphs({'a', 'b', 'c', 'd'}));
  ASSERT_TRUE(sc.ok()) << sc;

  // Condition map:
  // s3 -> {k}
  // s2 AND s3 -> {e, f}
  // s2 OR s3 -> {j}
  // s0 OR s3 OR s4 -> {a, b, c, , d, g, h}
  absl::btree_map<ActivationCondition, common::GlyphSet> expected = {
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
                                    *glyph_conditions_, *closure_cache_,
                                    glyphs_to_group_);
  ASSERT_TRUE(sc.ok()) << sc;

  GlyphGroupings other(segments_, hb_face_get_glyph_count(roboto_.get()));
  sc = other.GroupGlyphs(*requested_segmentation_info_, *glyph_conditions_,
                         *closure_cache_, glyphs_to_group_);
  ASSERT_TRUE(sc.ok()) << sc;

  // other does not have the same patch combinations and so should not be equal
  // to glyph_groupings_
  ASSERT_FALSE(glyph_groupings_ == other);

  sc = other.CombinePatches(ToGlyphs({'g'}), ToGlyphs({'b'}));
  ASSERT_TRUE(sc.ok());

  sc = other.GroupGlyphs(*requested_segmentation_info_, *glyph_conditions_,
                         *closure_cache_, glyphs_to_group_);
  ASSERT_TRUE(sc.ok()) << sc;

  // Now that combined patches matches they should be equal.
  ASSERT_TRUE(glyph_groupings_ == other);
}

TEST_F(GlyphGroupingsTest, ExclusiveGlyphsRespectsPatchCombinations) {
  auto sc = glyph_groupings_.CombinePatches(ToGlyphs({'g'}), ToGlyphs({'b'}));
  ASSERT_TRUE(sc.ok()) << sc;

  sc = glyph_groupings_.GroupGlyphs(*requested_segmentation_info_,
                                    *glyph_conditions_, *closure_cache_,
                                    glyphs_to_group_);
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

}  // namespace ift::encoder
