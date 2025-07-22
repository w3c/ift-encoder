#include "ift/encoder/candidate_merge.h"

#include "common/font_data.h"
#include "common/int_set.h"
#include "gtest/gtest.h"
#include "ift/encoder/closure_glyph_segmenter.h"
#include "ift/encoder/subset_definition.h"

using common::CodepointSet;
using common::FontData;
using common::hb_face_unique_ptr;
using common::IntSet;
using common::make_hb_face;

namespace ift::encoder {

class CandidateMergeTest : public ::testing::Test {
 protected:
  CandidateMergeTest() : roboto(make_hb_face(nullptr)) {
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

  hb_face_unique_ptr roboto;
};

// This is a simpler test which doesn't verify exact cost calculations but
// ensures that they have the correct sign using a case that is constructed to
// given very positive or very negative costs.
TEST_F(CandidateMergeTest, AssessMerge_CostDeltas) {
  std::vector<Segment> segments = {
      {{'a', 'b', 'c', 'd', 'e', 'f'}, 0.95},
      {{'g', 'h', 'i', 'j', 'k', 'l'}, 0.95},
      {{'m', 'n', 'o', 'p', 'q', 'r'}, 0.95},
      {{'s', 't', 'u', 'v', 'w', 'x'}, 0.01},
  };

  ClosureGlyphSegmenter segmenter;
  auto context =
      segmenter.InitializeSegmentationContext(roboto.get(), {}, segments);
  ASSERT_TRUE(context.ok()) << context.status();

  // Case 1: merge high frequency segments {0, 1, 2}. The cost of the new
  // segments increased probability is outweighed by the reduction of
  // network overhead, Overall cost should be negative.
  auto r = CandidateMerge::AssessMerge(*context, 0, {1, 2});
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());
  CandidateMerge merge = **r;
  ASSERT_LT(merge.cost_delta, 0);

  // Case 2: merging a high and low frequency segment will signicantly increase
  // the probably of loading the low frequency bytes which will not outweigh the
  // network overhead cost reduction. Overall cost should be positive.
  r = CandidateMerge::AssessMerge(*context, 0, {1, 3});
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());
  merge = **r;
  ASSERT_GT(merge.cost_delta, 0);
}

// More complex test that checks the actual computed cost value for a case
// involving both removed and modified conditions.
TEST_F(CandidateMergeTest, AssessMerge_CostDeltas_Complex) {
  std::vector<Segment> segments = {
      {{'f'}, 0.75},
      {{'i'}, 0.95},
  };

  ClosureGlyphSegmenter segmenter;
  auto context =
      segmenter.InitializeSegmentationContext(roboto.get(), {}, segments);
  ASSERT_TRUE(context.ok()) << context.status();

  // TODO XXXXX use a mock estimate patch size cache so we can control the
  // relevant patch sizes
  //      and predict the outcome of the cost calculation.

  // There should be three overall conditions 'f' -> p0, 'i' -> p1, 'f' and 'i'
  // -> p3

  // TODO XXXXX test for results of cost calculation.
}

}  // namespace ift::encoder
