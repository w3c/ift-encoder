#include "ift/encoder/candidate_merge.h"

#include <memory>
#include <optional>

#include "common/font_data.h"
#include "common/int_set.h"
#include "gtest/gtest.h"
#include "ift/encoder/closure_glyph_segmenter.h"
#include "ift/encoder/merge_strategy.h"
#include "ift/encoder/mock_patch_size_cache.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/mock_probability_calculator.h"
#include "ift/freq/probability_bound.h"
#include "ift/freq/unicode_frequencies.h"

using common::CodepointSet;
using common::FontData;
using common::hb_face_unique_ptr;
using common::IntSet;
using common::make_hb_face;
using ift::freq::MockProbabilityCalculator;
using ift::freq::ProbabilityBound;
using ift::freq::UnicodeFrequencies;

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
      {{'a', 'b', 'c', 'd', 'e', 'f'}, ProbabilityBound{0.95, 0.95}},
      {{'g', 'h', 'i', 'j', 'k', 'l'}, ProbabilityBound{0.95, 0.95}},
      {{'m', 'n', 'o', 'p', 'q', 'r'}, ProbabilityBound{0.95, 0.95}},
      {{'s', 't', 'u', 'v', 'w', 'x'}, ProbabilityBound{0.01, 0.01}},
  };
  std::vector<Segment> segments_with_merges = {
      {{'a', 'b', 'c', 'd', 'e', 'f'}, ProbabilityBound{0.95, 0.95}},
      {{'g', 'h', 'i', 'j', 'k', 'l'}, ProbabilityBound{0.95, 0.95}},
      {{'m', 'n', 'o', 'p', 'q', 'r'}, ProbabilityBound{0.95, 0.95}},
      {{'s', 't', 'u', 'v', 'w', 'x'}, ProbabilityBound{0.01, 0.01}},

      // 0 + 1
      {{'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l'},
       ProbabilityBound{0.98, 0.98}},

      // 0 + 1 + 2
      {{'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
        'o', 'p', 'q', 'r'},
       ProbabilityBound{0.99, 0.99}},

      // 0 + 1 + 3
      {{'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 's', 't',
        'u', 'v', 'w', 'x'},
       ProbabilityBound{0.98, 0.98}},
  };
  auto probability_calculator =
      std::make_unique<freq::MockProbabilityCalculator>(segments_with_merges);

  ClosureGlyphSegmenter segmenter;
  auto context =
      segmenter.InitializeSegmentationContext(roboto.get(), {}, segments);
  ASSERT_TRUE(context.ok()) << context.status();
  context->merge_strategy =
      MergeStrategy::CostBased(std::move(probability_calculator), 75, 4);

  // Case 1: merge high frequency segments {0, 1, 2}. The cost of the new
  // segments increased probability is outweighed by the reduction of
  // network overhead, Overall cost should be negative.
  auto r = CandidateMerge::AssessMerge(*context, 0, {1, 2}, std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());
  CandidateMerge merge = **r;
  ASSERT_LT(merge.cost_delta, 0);

  // Case 2: merging a high and low frequency segment will signicantly increase
  // the probably of loading the low frequency bytes which will not outweigh the
  // network overhead cost reduction. Overall cost should be positive.
  r = CandidateMerge::AssessMerge(*context, 0, {1, 3}, std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());
  merge = **r;
  ASSERT_GT(merge.cost_delta, 0);
  double prev_cost_delta = merge.cost_delta;

  // Case 3: check that ordering (ie. what's 'base' and what's 'merged') does
  // not change the cost delta.
  r = CandidateMerge::AssessMerge(*context, 3, {0, 1}, std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());
  merge = **r;
  ASSERT_GT(merge.cost_delta, 0);
  ASSERT_EQ(merge.cost_delta, prev_cost_delta);
}

TEST_F(CandidateMergeTest, AssessMerge_WithBestCandidate) {
  std::vector<Segment> segments = {
      {{'a', 'b', 'c', 'd', 'e', 'f'}, ProbabilityBound{0.95, 0.95}},
      {{'g', 'h', 'i', 'j', 'k', 'l'}, ProbabilityBound{0.95, 0.95}},
      {{'m', 'n', 'o', 'p', 'q', 'r'}, ProbabilityBound{0.95, 0.95}},
      {{'s', 't', 'u', 'v', 'w', 'x'}, ProbabilityBound{0.01, 0.01}},
  };

  double merged_01 = 1.0 - (1.0 - 0.95) * (1.0 - 0.95);
  double merged_03 = 1.0 - (1.0 - 0.95) * (1.0 - 0.01);
  std::vector<Segment> segments_with_merges = {
      {{'a', 'b', 'c', 'd', 'e', 'f'}, ProbabilityBound{0.95, 0.95}},
      {{'g', 'h', 'i', 'j', 'k', 'l'}, ProbabilityBound{0.95, 0.95}},
      {{'m', 'n', 'o', 'p', 'q', 'r'}, ProbabilityBound{0.95, 0.95}},
      {{'s', 't', 'u', 'v', 'w', 'x'}, ProbabilityBound{0.01, 0.01}},

      // 0 + 1
      {{'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l'},
       ProbabilityBound{merged_01, merged_01}},

      // 0 + 3
      {{'a', 'b', 'c', 'd', 'e', 'f', 's', 't', 'u', 'v', 'w', 'x'},
       ProbabilityBound{merged_03, merged_03}},
  };
  auto probability_calculator =
      std::make_unique<freq::MockProbabilityCalculator>(segments_with_merges);

  ClosureGlyphSegmenter segmenter;
  auto context =
      segmenter.InitializeSegmentationContext(roboto.get(), {}, segments);
  ASSERT_TRUE(context.ok()) << context.status();
  context->merge_strategy =
      MergeStrategy::CostBased(std::move(probability_calculator), 75, 4);

  unsigned base_size =
      *context->patch_size_cache->GetPatchSize({'a', 'b', 'c', 'd', 'e', 'f'});

  // Case 1: merge high frequency segments {0, 1}. Best current merge is set at
  // 0, assess merge should return a better candidate.
  auto r = CandidateMerge::AssessMerge(
      *context, 0, {1},
      CandidateMerge::BaselineCandidate(
          4, 0.0, base_size, 0.95,
          context->merge_strategy.NetworkOverheadCost()));
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());
  CandidateMerge merge = **r;
  ASSERT_LT(merge.cost_delta, 0);

  // Case 2: merge high frequency segments {0, 1}. Best current merge is set at
  // -500, assess merge should not return a better candidate.
  r = CandidateMerge::AssessMerge(
      *context, 0, {1},
      CandidateMerge::BaselineCandidate(
          4, -500.0, base_size, 0.95,
          context->merge_strategy.NetworkOverheadCost()));
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_FALSE(r->has_value());

  // Case 3: merging a high and low frequency segment will signicantly increase
  // the probably of loading the low frequency bytes which will not outweigh the
  // network overhead cost reduction. Overall cost should be positive. Baseline
  // is set at 0 so no candidate is expected from the return.
  r = CandidateMerge::AssessMerge(
      *context, 0, {3},
      CandidateMerge::BaselineCandidate(
          4, 0.0, base_size, 0.95,
          context->merge_strategy.NetworkOverheadCost()));
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_FALSE(r->has_value());

  // Case 4: same as 4 but with order reversed, result should be the same.
  base_size =
      *context->patch_size_cache->GetPatchSize({'s', 't', 'u', 'v', 'w', 'x'});
  r = CandidateMerge::AssessMerge(
      *context, 3, {0},
      CandidateMerge::BaselineCandidate(
          4, 0.0, base_size, 0.01,
          context->merge_strategy.NetworkOverheadCost()));
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_FALSE(r->has_value());
}

// More complex test that checks the actual computed cost value.
TEST_F(CandidateMergeTest, AssessMerge_CostDeltas_Complex) {
  freq::UnicodeFrequencies frequencies{
      {{' ', ' '}, 100}, {{'f', 'f'}, 75}, {{'i', 'i'}, 95}};

  std::vector<Segment> segments = {
      {{'f'}, {0.75, 0.75}},
      {{'i'}, {0.95, 0.95}},
  };

  ClosureGlyphSegmenter segmenter;
  auto context =
      segmenter.InitializeSegmentationContext(roboto.get(), {}, segments);
  ASSERT_TRUE(context.ok()) << context.status();
  context->merge_strategy =
      *MergeStrategy::CostBased(std::move(frequencies), 75, 4);

  MockPatchSizeCache* size_cache = new MockPatchSizeCache();

  // There are four glyph sets in use here with this segmentation:
  // 1. f -> {74}
  // 2. i -> {77}
  // 3. f+i -> {444, 446}
  // 4. merged -> {74, 77, 444, 446}
  size_cache->SetPatchSize({74}, 200);
  size_cache->SetPatchSize({77}, 300);
  size_cache->SetPatchSize({444, 446}, 150);
  size_cache->SetPatchSize({74, 77, 444, 446}, 600);

  // Expected cost delta:
  // After the merge there's only one single patch containing everything,
  // so subtract costs of the patches prior to the merge and add the cost
  // of the newly merged patch
  double p_f_or_i = 0.75 + 0.95 - 0.75 * 0.95;  // probability of (f or i)
  double p_f_and_i = 0.75 * 0.95;               // probability of (f and i)
  double expected_cost_delta =
      -0.75 * (200 + 75)        // less cost of {f}
      - 0.95 * (300 + 75)       // less cost of {i}
      - p_f_and_i * (150 + 75)  // less cost of {f + i}
      + p_f_or_i * (600 + 75);  // add cost of the new merged patch

  context->patch_size_cache.reset(size_cache);
  auto r = CandidateMerge::AssessMerge(*context, 0, {1}, std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());
  CandidateMerge merge = **r;
  EXPECT_NEAR(merge.cost_delta, expected_cost_delta, 1e-9);
}

// More complex test that checks the actual computed cost value, includes a
// modified condition.
TEST_F(CandidateMergeTest, AssessMerge_CostDeltas_Complex_ModifiedConditions) {
  std::vector<Segment> segments = {
      {{'a'}, {0.50, 0.50}},
      {{'f'}, {0.75, 0.75}},
      {{'i'}, {0.95, 0.95}},
  };
  freq::UnicodeFrequencies frequencies{
      {{' ', ' '}, 100}, {{'a', 'a'}, 50}, {{'f', 'f'}, 75}, {{'i', 'i'}, 95}};

  ClosureGlyphSegmenter segmenter;
  auto context =
      segmenter.InitializeSegmentationContext(roboto.get(), {}, segments);
  ASSERT_TRUE(context.ok()) << context.status();
  context->merge_strategy =
      *MergeStrategy::CostBased(std::move(frequencies), 75, 4);

  MockPatchSizeCache* size_cache = new MockPatchSizeCache();

  // There are four glyph sets in use here with this segmentation:
  // 1. a -> {69}
  // 1. f -> {74}
  // 3. f+i -> {444, 446}
  // 4. merged -> {69, 74}
  size_cache->SetPatchSize({69}, 200);
  size_cache->SetPatchSize({74}, 300);
  size_cache->SetPatchSize({444, 446}, 150);
  size_cache->SetPatchSize({69, 74}, 450);

  // Expected cost delta:
  // After the merge there's only one single patch containing everything,
  // so subtract costs of the patches prior to the merge and add the cost
  // of the newly merged patch
  double p_a_or_f = 0.50 + 0.75 - 0.50 * 0.75;  // probability of (a or f)
  double p_f_and_i = 0.75 * 0.95;               // probability of (f and i)
  double expected_cost_delta =
      -0.50 * (200 + 75)                // less cost of {a}
      - 0.75 * (300 + 75)               // less cost of {f}
      - p_f_and_i * (150 + 75)          // less cost of {f + i}
      + (p_a_or_f * 0.95) * (150 + 75)  // add cost of modified segment
      + p_a_or_f * (450 + 75);          // add cost of the new merged patch

  context->patch_size_cache.reset(size_cache);
  auto r = CandidateMerge::AssessMerge(*context, 0, {1}, std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());
  CandidateMerge merge = **r;
  EXPECT_NEAR(merge.cost_delta, expected_cost_delta, 1e-9);
}

TEST_F(CandidateMergeTest, OperatorLess) {
  Segment empty_segment({}, ProbabilityBound::Zero());
  CandidateMerge a{.base_segment_index = 0,
                   .segments_to_merge = {1},
                   .merged_segment = empty_segment,
                   .new_segment_is_inert = false,
                   .new_patch_size = 0,
                   .cost_delta = 100.0};
  CandidateMerge b{.base_segment_index = 1,
                   .segments_to_merge = {2},
                   .merged_segment = empty_segment,
                   .new_segment_is_inert = false,
                   .new_patch_size = 0,
                   .cost_delta = 100.0};
  CandidateMerge c{.base_segment_index = 1,
                   .segments_to_merge = {3},
                   .merged_segment = empty_segment,
                   .new_segment_is_inert = false,
                   .new_patch_size = 0,
                   .cost_delta = 100.0};
  CandidateMerge d{.base_segment_index = 0,
                   .segments_to_merge = {1},
                   .merged_segment = empty_segment,
                   .new_segment_is_inert = false,
                   .new_patch_size = 0,
                   .cost_delta = 200.0};

  EXPECT_TRUE(a < b);
  EXPECT_TRUE(b < c);
  EXPECT_TRUE(a < c);
  EXPECT_TRUE(a < d);
  EXPECT_TRUE(b < d);
  EXPECT_TRUE(c < d);

  EXPECT_FALSE(b < a);
  EXPECT_FALSE(c < b);
  EXPECT_FALSE(c < a);
  EXPECT_FALSE(d < a);
  EXPECT_FALSE(d < b);
  EXPECT_FALSE(d < c);
}

}  // namespace ift::encoder
