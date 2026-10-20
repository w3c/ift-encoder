#include "ift/encoder/candidate_merge.h"

#include <cmath>
#include <memory>
#include <optional>

#include "absl/flags/flag.h"
#include "gtest/gtest.h"
#include "ift/common/bazel_data_file_resolver.h"
#include "ift/common/font_data.h"
#include "ift/common/font_helper.h"
#include "ift/common/int_set.h"
#include "ift/common/test_font_loader.h"
#include "ift/encoder/closure_glyph_segmenter.h"
#include "ift/encoder/merge_strategy.h"
#include "ift/encoder/merger.h"
#include "ift/encoder/mock_patch_size_cache.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"
#include "ift/freq/mock_probability_calculator.h"
#include "ift/freq/probability_bound.h"
#include "ift/freq/unicode_frequencies.h"

using absl::flat_hash_map;

using ift::config::CLOSURE_ONLY;
using ift::config::DEP_GRAPH_ONLY;
using ift::config::DEP_GRAPH_ONLY_WITH_SIMPLIFICATION;
using ift::config::PATCH;

using ift::common::BazelDataFileResolver;
using ift::common::CodepointSet;
using ift::common::DataFileResolver;
using ift::common::FontData;
using ift::common::FontHelper;
using ift::common::GlyphSet;
using ift::common::hb_face_unique_ptr;
using ift::common::IntSet;
using ift::common::make_hb_face;
using ift::common::SegmentSet;
using ift::freq::MockProbabilityCalculator;
using ift::freq::ProbabilityBound;
using ift::freq::UnicodeFrequencies;
using ProbabilityProfile = ift::encoder::MergeStrategy::ProbabilityProfile;

namespace ift::encoder {

class CandidateMergeTest : public ::testing::Test {
 protected:
  CandidateMergeTest()
      : roboto(make_hb_face(nullptr)),
        resolver(*BazelDataFileResolver::CreateForTest()),
        empty_segment({}),
        a(empty_segment),
        b(empty_segment),
        c(empty_segment),
        d(empty_segment) {
    roboto = from_file("ift/common/testdata/Roboto-Regular.ttf");

    a.base_segment_index_ = 0;
    a.segments_to_merge_ = {1};
    a.merged_segment_ = empty_segment;
    a.input_segments_are_inert_ = false;
    a.new_patch_size_ = 0;
    a.cost_delta_ = 100.0;

    b.base_segment_index_ = 1;
    b.segments_to_merge_ = {2};
    b.merged_segment_ = empty_segment;
    b.input_segments_are_inert_ = false;
    b.new_patch_size_ = 0;
    b.cost_delta_ = 100.0;

    c.base_segment_index_ = 1;
    c.segments_to_merge_ = {3};
    c.merged_segment_ = empty_segment;
    c.input_segments_are_inert_ = false;
    c.new_patch_size_ = 0;
    c.cost_delta_ = 100.0;

    d.base_segment_index_ = 0;
    d.segments_to_merge_ = {1};
    d.merged_segment_ = empty_segment;
    d.input_segments_are_inert_ = false;
    d.new_patch_size_ = 0;
    d.cost_delta_ = 200.0;

    all.invert();
  }

  hb_face_unique_ptr from_file(const char* filename) {
    auto loader = ift::common::TestFontLoader::Default().value();
    return loader->LoadFace(filename).value();
  }

  hb_face_unique_ptr roboto;
  std::shared_ptr<DataFileResolver> resolver;
  Segment empty_segment;
  CandidateMerge a;
  CandidateMerge b;
  CandidateMerge c;
  CandidateMerge d;
  SegmentSet all;
};

// This is a simpler test which doesn't verify exact cost calculations but
// ensures that they have the correct sign using a case that is constructed to
// given very positive or very negative costs.
TEST_F(CandidateMergeTest, AssessMerge_CostDeltas) {
  std::vector<Segment> segments = {
      {{'a', 'b', 'c', 'd', 'e', 'f'}},
      {{'g', 'h', 'i', 'j', 'k', 'l'}},
      {{'m', 'n', 'o', 'p', 'q', 'r'}},
      {{'s', 't', 'u', 'v', 'w', 'x'}},
  };
  std::vector<std::pair<Segment, double>> segment_probabilities = {
      {{{'a', 'b', 'c', 'd', 'e', 'f'}}, 0.95},
      {{{'g', 'h', 'i', 'j', 'k', 'l'}}, 0.95},
      {{{'m', 'n', 'o', 'p', 'q', 'r'}}, 0.95},
      {{{'s', 't', 'u', 'v', 'w', 'x'}}, 0.01},

      // 0 + 1
      {{{'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l'}}, 0.98},

      // 0 + 1 + 2
      {{{'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
         'o', 'p', 'q', 'r'}},
       0.99},

      // 0 + 1 + 3
      {{{'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 's', 't',
         'u', 'v', 'w', 'x'}},
       0.98},
  };
  auto probability_calculator =
      std::make_unique<freq::MockProbabilityCalculator>(segment_probabilities);

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  Merger merger = *Merger::New(
      *context,
      MergeStrategy::CostBased(
          ProbabilityProfile(std::move(probability_calculator)), 75, 4),
      all, all);

  // Case 1: merge high frequency segments {0, 1, 2}. The cost of the new
  // segments increased probability is outweighed by the reduction of
  // network overhead, Overall cost should be negative.
  auto r = CandidateMerge::AssessSegmentMerge(merger, 0, {1, 2}, std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());
  CandidateMerge merge = **r;
  ASSERT_LT(merge.CostDelta(), 0);

  // Case 2: merging a high and low frequency segment will signicantly increase
  // the probably of loading the low frequency bytes which will not outweigh the
  // network overhead cost reduction. Overall cost should be positive.
  r = CandidateMerge::AssessSegmentMerge(merger, 0, {1, 3}, std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());
  merge = **r;
  ASSERT_GT(merge.CostDelta(), 0);
  double prev_cost_delta = merge.CostDelta();

  // Case 3: check that ordering (ie. what's 'base' and what's 'merged') does
  // not change the cost delta.
  r = CandidateMerge::AssessSegmentMerge(merger, 3, {0, 1}, std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());
  merge = **r;
  ASSERT_GT(merge.CostDelta(), 0);
  ASSERT_EQ(merge.CostDelta(), prev_cost_delta);
}

TEST_F(CandidateMergeTest, AssessMerge_WithBestCandidate) {
  std::vector<Segment> segments = {
      {{'a', 'b', 'c', 'd', 'e', 'f'}},
      {{'g', 'h', 'i', 'j', 'k', 'l'}},
      {{'m', 'n', 'o', 'p', 'q', 'r'}},
      {{'s', 't', 'u', 'v', 'w', 'x'}},
  };

  double merged_01 = 1.0 - (1.0 - 0.95) * (1.0 - 0.95);
  double merged_03 = 1.0 - (1.0 - 0.95) * (1.0 - 0.01);
  std::vector<std::pair<Segment, double>> segment_probabilities = {
      {{{'a', 'b', 'c', 'd', 'e', 'f'}}, 0.95},
      {{{'g', 'h', 'i', 'j', 'k', 'l'}}, 0.95},
      {{{'m', 'n', 'o', 'p', 'q', 'r'}}, 0.95},
      {{{'s', 't', 'u', 'v', 'w', 'x'}}, 0.01},

      // 0 + 1
      {{{'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l'}},
       merged_01},

      // 0 + 3
      {{{'a', 'b', 'c', 'd', 'e', 'f', 's', 't', 'u', 'v', 'w', 'x'}},
       merged_03},
  };
  auto probability_calculator =
      std::make_unique<freq::MockProbabilityCalculator>(segment_probabilities);

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  Merger merger = *Merger::New(
      *context,
      MergeStrategy::CostBased(
          ProbabilityProfile(std::move(probability_calculator)), 75, 4),
      all, all);

  unsigned base_size =
      *context->patch_size_cache->GetPatchSize({'a', 'b', 'c', 'd', 'e', 'f'});

  // Case 1: merge high frequency segments {0, 1}. Best current merge is set at
  // 0, assess merge should return a better candidate.
  auto r = CandidateMerge::AssessSegmentMerge(
      merger, 0, {1}, CandidateMerge::BaselineCandidate(4, 0.0));
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());
  CandidateMerge merge = **r;
  ASSERT_LT(merge.CostDelta(), 0);

  // Case 2: merge high frequency segments {0, 1}. Best current merge is set at
  // -500, assess merge should not return a better candidate.
  r = CandidateMerge::AssessSegmentMerge(
      merger, 0, {1}, CandidateMerge::BaselineCandidate(4, -500.0));
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_FALSE(r->has_value());

  // Case 3: merging a high and low frequency segment will signicantly increase
  // the probably of loading the low frequency bytes which will not outweigh the
  // network overhead cost reduction. Overall cost should be positive. Baseline
  // is set at 0 so no candidate is expected from the return.
  r = CandidateMerge::AssessSegmentMerge(
      merger, 0, {3}, CandidateMerge::BaselineCandidate(4, 0.0));
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_FALSE(r->has_value());

  // Case 4: same as 4 but with order reversed, result should be the same.
  base_size =
      *context->patch_size_cache->GetPatchSize({'s', 't', 'u', 'v', 'w', 'x'});
  r = CandidateMerge::AssessSegmentMerge(
      merger, 3, {0}, CandidateMerge::BaselineCandidate(4, 0.0));
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_FALSE(r->has_value());
}

// More complex test that checks the actual computed cost value.
TEST_F(CandidateMergeTest, AssessMerge_CostDeltas_Complex) {
  freq::UnicodeFrequencies frequencies{
      {{' ', ' '}, 100}, {{'f', 'f'}, 75}, {{'i', 'i'}, 95}};

  std::vector<Segment> segments = {
      {{'f'}},
      {{'i'}},
  };

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  Merger merger = *Merger::New(
      *context,
      MergeStrategy::CostBased(
          *ProbabilityProfile::Unigram(std::move(frequencies)), 75, 4),
      all, all);

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
  auto r = CandidateMerge::AssessSegmentMerge(merger, 0, {1}, std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());
  CandidateMerge merge = **r;
  EXPECT_NEAR(merge.CostDelta(), expected_cost_delta, 1e-9);
}

// More complex test that checks the actual computed cost value, includes a
// modified condition.
TEST_F(CandidateMergeTest, AssessMerge_CostDeltas_Complex_ModifiedConditions) {
  std::vector<Segment> segments = {
      {{'a'}},
      {{'f'}},
      {{'i'}},
  };
  freq::UnicodeFrequencies frequencies{
      {{' ', ' '}, 100}, {{'a', 'a'}, 50}, {{'f', 'f'}, 75}, {{'i', 'i'}, 95}};

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  Merger merger = *Merger::New(
      *context,
      MergeStrategy::CostBased(
          *ProbabilityProfile::Unigram(std::move(frequencies)), 75, 4),
      all, all);

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
  auto r = CandidateMerge::AssessSegmentMerge(merger, 0, {1}, std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());
  CandidateMerge merge = **r;
  EXPECT_NEAR(merge.CostDelta(), expected_cost_delta, 1e-9);
}

// Checks that when a strategy has multiple probability calculators the cost
// delta of a segment merge is the sum of the deltas computed against each
// calculator.
TEST_F(CandidateMergeTest, AssessMerge_CostDeltas_MultipleCalculators) {
  std::vector<Segment> segments = {
      {{'f'}},
      {{'i'}},
  };

  // Calculator one models a language where 'f' and 'i' are common.
  std::vector<std::pair<Segment, double>> probabilities_1 = {
      {{{'f'}}, 0.75},
      {{{'i'}}, 0.95},
      {{{'f', 'i'}}, 0.98},
  };
  // Calculator two models a language where 'f' and 'i' are uncommon.
  std::vector<std::pair<Segment, double>> probabilities_2 = {
      {{{'f'}}, 0.10},
      {{{'i'}}, 0.20},
      {{{'f', 'i'}}, 0.25},
  };

  MergeStrategy strategy = MergeStrategy::CostBased(
      ProbabilityProfile(
          std::make_shared<MockProbabilityCalculator>(probabilities_1)),
      75, 4);
  strategy.AddProbabilityProfile(ProbabilityProfile(
      std::make_shared<MockProbabilityCalculator>(probabilities_2)));

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  Merger merger = *Merger::New(*context, strategy, all, all);

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

  // Patch sizes are identical for both calculators, only the probabilities
  // differ. So the expected total delta is the sum of the delta computed
  // against each calculator individually.
  double expected_delta_1 = -0.75 * (200 + 75)            // less cost of {f}
                            - 0.95 * (300 + 75)           // less cost of {i}
                            - (0.75 * 0.95) * (150 + 75)  // less cost {f + i}
                            + 0.98 * (600 + 75);          // add merged patch
  double expected_delta_2 = -0.10 * (200 + 75)            // less cost of {f}
                            - 0.20 * (300 + 75)           // less cost of {i}
                            - (0.10 * 0.20) * (150 + 75)  // less cost {f + i}
                            + 0.25 * (600 + 75);          // add merged patch

  context->patch_size_cache.reset(size_cache);
  auto r = CandidateMerge::AssessSegmentMerge(merger, 0, {1}, std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());
  CandidateMerge merge = **r;
  EXPECT_NEAR(merge.CostDelta(), expected_delta_1 + expected_delta_2, 1e-9);
}

// A calculator which has no frequency data for the codepoints involved in a
// merge contributes nothing to the cost delta.
TEST_F(CandidateMergeTest, AssessMerge_CostDeltas_UnrelatedCalculator) {
  std::vector<Segment> segments = {
      {{'f'}},
      {{'i'}},
  };

  std::vector<std::pair<Segment, double>> probabilities_1 = {
      {{{'f'}}, 0.75},
      {{{'i'}}, 0.95},
      {{{'f', 'i'}}, 0.98},
  };
  // This calculator has no data for 'f' or 'i', so all associated
  // probabilities are 0.
  std::vector<std::pair<Segment, double>> probabilities_2 = {
      {{{'z'}}, 0.90},
  };

  MergeStrategy strategy = MergeStrategy::CostBased(
      ProbabilityProfile(
          std::make_shared<MockProbabilityCalculator>(probabilities_1)),
      75, 4);
  strategy.AddProbabilityProfile(ProbabilityProfile(
      std::make_shared<MockProbabilityCalculator>(probabilities_2)));

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  Merger merger = *Merger::New(*context, strategy, all, all);

  MockPatchSizeCache* size_cache = new MockPatchSizeCache();
  size_cache->SetPatchSize({74}, 200);
  size_cache->SetPatchSize({77}, 300);
  size_cache->SetPatchSize({444, 446}, 150);
  size_cache->SetPatchSize({74, 77, 444, 446}, 600);

  double expected_cost_delta = -0.75 * (200 + 75)            // less cost of {f}
                               - 0.95 * (300 + 75)           // less cost of {i}
                               - (0.75 * 0.95) * (150 + 75)  // less {f + i}
                               + 0.98 * (600 + 75);          // add merged patch

  context->patch_size_cache.reset(size_cache);
  auto r = CandidateMerge::AssessSegmentMerge(merger, 0, {1}, std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());
  CandidateMerge merge = **r;
  EXPECT_NEAR(merge.CostDelta(), expected_cost_delta, 1e-9);
}

TEST_F(CandidateMergeTest, OperatorLess) {
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

TEST_F(CandidateMergeTest, AssessPatchMerge) {
  std::vector<Segment> segments = {
      {{'A', 'B'}}, {{'C'}}, {{'e'}}, {{0xe9}},  // eacute
      {{0x106}},                                 // Cacute
  };
  std::vector<std::pair<Segment, double>> segment_probabilities = {
      {{{'A', 'B'}}, 0.95},
      {{{'C'}}, 0.95},
      {{{'e'}}, 0.10},
      {{{0x0e9}}, 0.01},  // eacute
      {{{0x106}}, 0.01},  // Cacute

      // 1 + 4
      {{{'C', 0x106}}, 0.95},

      // 0 + 1 + 4
      {{{'A', 'B', 'C', 0x106}}, 0.98},

      // 2 + 3
      {{{'e', 0xe9}}, 0.10},

      // 0 + 2 + 3
      {{{'A', 'B', 'e', 0xe9}}, 0.95},
  };
  auto probability_calculator =
      std::make_unique<freq::MockProbabilityCalculator>(segment_probabilities);

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  Merger merger = *Merger::New(
      *context,
      MergeStrategy::CostBased(
          ProbabilityProfile(std::move(probability_calculator)), 75, 1),
      all, all);

  // Try merging the patch for {0} and {1 OR 4}.
  auto r = CandidateMerge::AssessPatchMerge(
      merger, ActivationCondition::exclusive_segment(0, 0),
      ActivationCondition::or_segments({1, 4}, 0), std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());
  CandidateMerge merge = **r;

  // This merge should be favourable. The combined probability is the same, so
  // reduction of overhead makes the delta negative
  ASSERT_LT(merge.CostDelta(), 0);

  // Try merging the patch for {0} and {2 OR 3}.
  r = CandidateMerge::AssessPatchMerge(
      merger, ActivationCondition::exclusive_segment(0, 0),
      ActivationCondition::or_segments({2, 3}, 0), std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());
  merge = **r;

  // This merge should be not favourable. The combined probability is higher
  // then the combination.
  ASSERT_GT(merge.CostDelta(), 0);
}

TEST_F(CandidateMergeTest, AssessPatchMerge_RequiresPatches) {
  std::vector<Segment> segments = {
      {{'A', 'B'}},
      {{'C'}},
      {{0x106}},  // Cacute
  };
  std::vector<std::pair<Segment, double>> segment_probabilities = {
      {{{'A', 'B'}}, 0.95},
      {{{'C'}}, 0.95},
      {{{0x106}}, 0.01},  // Cacute

      // 1 + 2
      {{{'C', 0x106}}, 0.95},

      // 0 + 1 + 2
      {{{'A', 'B', 'C', 0x106}}, 0.98},
  };
  auto probability_calculator =
      std::make_unique<freq::MockProbabilityCalculator>(segment_probabilities);

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  Merger merger = *Merger::New(
      *context,
      MergeStrategy::CostBased(
          ProbabilityProfile(std::move(probability_calculator)), 75, 1),
      all, all);

  // Try merging the patch for {0} and {1}.
  auto r = CandidateMerge::AssessPatchMerge(
      merger, ActivationCondition::exclusive_segment(0, 0),
      ActivationCondition::or_segments({1}, 0), std::nullopt);
  // segment 1 has no patch associated with it, so no merge is possible.
  ASSERT_TRUE(absl::IsInternal(r.status())) << r.status();

  // Try merging the patch for {0} and {0 or 1}.
  r = CandidateMerge::AssessPatchMerge(
      merger, ActivationCondition::exclusive_segment(0, 0),
      ActivationCondition::or_segments({0, 1}, 0), std::nullopt);
  // {0 or 1} has no patch associated with it, so no merge is possible.
  ASSERT_TRUE(absl::IsInternal(r.status())) << r.status();
}

TEST_F(CandidateMergeTest, AssessPatchMerge_NonDisjunctive) {
  std::vector<Segment> segments = {
      {{'A'}},
      {{'B'}},
      {{'C'}},
  };

  std::vector<std::pair<Segment, double>> segment_probabilities = {
      {{{'A'}}, 0.95},      {{{'B'}}, 0.85},      {{{'C'}}, 0.75},
      {{{'A', 'B'}}, 0.90}, {{{'A', 'C'}}, 0.92},
  };

  auto probability_calculator =
      std::make_unique<freq::MockProbabilityCalculator>(segment_probabilities);

  MockPatchSizeCache* size_cache = new MockPatchSizeCache();

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  Merger merger = *Merger::New(
      *context,
      MergeStrategy::CostBased(
          ProbabilityProfile(std::move(probability_calculator)), 75, 1),
      all, all);

  glyph_id_t gid_B = *FontHelper::GetNominalGlyph(roboto.get(), 'B');
  size_cache->SetPatchSize({gid_B}, 200);

  // Manually set up a conjunctive condition for gid_B: s1 AND s2.
  context->glyph_condition_set.AddAndCondition(gid_B, 1);
  context->glyph_condition_set.AddAndCondition(gid_B, 2);

  ActivationCondition conj_cond = ActivationCondition::and_segments({1, 2}, 0);

  // We also need a base patch to exist.
  glyph_id_t gid_A = *FontHelper::GetNominalGlyph(roboto.get(), 'A');
  size_cache->SetPatchSize({gid_A}, 100);
  size_cache->SetPatchSize({gid_A, gid_B}, 250);
  context->glyph_condition_set.AddAndCondition(gid_A, 0);

  // Update glyph groupings.
  auto sc = context->glyph_groupings.GroupGlyphs(
      context->SegmentationInfo(), context->glyph_condition_set,
      *context->glyph_closure_cache, std::nullopt, {gid_A, gid_B}, {0, 1, 2},
      false);
  ASSERT_TRUE(sc.ok()) << sc;

  context->patch_size_cache.reset(size_cache);

  auto r = CandidateMerge::AssessPatchMerge(
      merger, ActivationCondition::exclusive_segment(0, 0), conj_cond,
      std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());

  // Delta =
  // - (s0) * (size(gid_A) + 75)
  // - (s1 AND s2) * (size(gid_B) + 75)
  // + (s0 OR s1) and (s0 OR s2)) * (size(gid_A, gid_b) + 75)
  ASSERT_EQ((*r)->CostDelta(), -0.95 * (100 + 75) - (0.85 * 0.75) * (200 + 75) +
                                   (0.90 * 0.92) * (250 + 75));

  CandidateMerge merge = **r;
  ASSERT_EQ(merge.SegmentsToMerge(), SegmentSet({0, 1, 2}));
}

// Checks that patch merge cost deltas are the sum of the deltas computed
// against each of the strategies probability calculators.
TEST_F(CandidateMergeTest, AssessPatchMerge_MultipleCalculators) {
  std::vector<Segment> segments = {
      {{'A'}},
      {{'B'}},
      {{'C'}},
  };

  std::vector<std::pair<Segment, double>> probabilities_1 = {
      {{{'A'}}, 0.95},      {{{'B'}}, 0.85},      {{{'C'}}, 0.75},
      {{{'A', 'B'}}, 0.90}, {{{'A', 'C'}}, 0.92},
  };
  std::vector<std::pair<Segment, double>> probabilities_2 = {
      {{{'A'}}, 0.05},      {{{'B'}}, 0.15},      {{{'C'}}, 0.25},
      {{{'A', 'B'}}, 0.30}, {{{'A', 'C'}}, 0.40},
  };

  MergeStrategy strategy = MergeStrategy::CostBased(
      ProbabilityProfile(
          std::make_shared<MockProbabilityCalculator>(probabilities_1)),
      75, 1);
  strategy.AddProbabilityProfile(ProbabilityProfile(
      std::make_shared<MockProbabilityCalculator>(probabilities_2)));

  MockPatchSizeCache* size_cache = new MockPatchSizeCache();

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  Merger merger = *Merger::New(*context, strategy, all, all);

  glyph_id_t gid_B = *FontHelper::GetNominalGlyph(roboto.get(), 'B');
  size_cache->SetPatchSize({gid_B}, 200);

  // Manually set up a conjunctive condition for gid_B: s1 AND s2.
  context->glyph_condition_set.AddAndCondition(gid_B, 1);
  context->glyph_condition_set.AddAndCondition(gid_B, 2);

  ActivationCondition conj_cond = ActivationCondition::and_segments({1, 2}, 0);

  // We also need a base patch to exist.
  glyph_id_t gid_A = *FontHelper::GetNominalGlyph(roboto.get(), 'A');
  size_cache->SetPatchSize({gid_A}, 100);
  size_cache->SetPatchSize({gid_A, gid_B}, 250);
  context->glyph_condition_set.AddAndCondition(gid_A, 0);

  // Update glyph groupings.
  auto sc = context->glyph_groupings.GroupGlyphs(
      context->SegmentationInfo(), context->glyph_condition_set,
      *context->glyph_closure_cache, std::nullopt, {gid_A, gid_B}, {0, 1, 2},
      false);
  ASSERT_TRUE(sc.ok()) << sc;

  context->patch_size_cache.reset(size_cache);

  auto r = CandidateMerge::AssessPatchMerge(
      merger, ActivationCondition::exclusive_segment(0, 0), conj_cond,
      std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());

  // Delta for a single calculator m =
  // - P_m(s0) * (size(gid_A) + 75)
  // - P_m(s1 AND s2) * (size(gid_B) + 75)
  // + P_m((s0 OR s1) AND (s0 OR s2)) * (size(gid_A, gid_b) + 75)
  //
  // The total delta is the sum of the deltas for each calculator.
  double expected_delta_1 = -0.95 * (100 + 75) - (0.85 * 0.75) * (200 + 75) +
                            (0.90 * 0.92) * (250 + 75);
  double expected_delta_2 = -0.05 * (100 + 75) - (0.15 * 0.25) * (200 + 75) +
                            (0.30 * 0.40) * (250 + 75);

  EXPECT_NEAR((*r)->CostDelta(), expected_delta_1 + expected_delta_2, 1e-9);
}

TEST_F(CandidateMergeTest, AssessPatchMerge_NonDisjunctive_WithSimplification) {
  std::vector<Segment> segments = {
      {{'A'}},
      {{'B'}},
      {{'C'}},
  };
  std::vector<Segment> segments_with_merges = segments;
  std::vector<std::pair<Segment, double>> segment_probabilities = {
      {{{'A'}}, 0.95},      {{{'B'}}, 0.85},      {{{'C'}}, 0.75},
      {{{'A', 'B'}}, 0.90}, {{{'A', 'C'}}, 0.92},
  };

  auto probability_calculator =
      std::make_unique<freq::MockProbabilityCalculator>(segment_probabilities);

  MockPatchSizeCache* size_cache = new MockPatchSizeCache();

  ClosureGlyphSegmenter segmenter(8, 8, PATCH,
                                  DEP_GRAPH_ONLY_WITH_SIMPLIFICATION, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  Merger merger = *Merger::New(
      *context,
      MergeStrategy::CostBased(
          ProbabilityProfile(std::move(probability_calculator)), 75, 1),
      all, all);

  glyph_id_t gid_B = *FontHelper::GetNominalGlyph(roboto.get(), 'B');
  size_cache->SetPatchSize({gid_B}, 200);

  // Manually set up a conjunctive condition for gid_B: s1 AND s2.
  context->glyph_condition_set.AddAndCondition(gid_B, 1);
  context->glyph_condition_set.AddAndCondition(gid_B, 2);

  ActivationCondition conj_cond = ActivationCondition::and_segments({1, 2}, 0);

  // We also need a base patch to exist.
  glyph_id_t gid_A = *FontHelper::GetNominalGlyph(roboto.get(), 'A');
  size_cache->SetPatchSize({gid_A}, 100);
  size_cache->SetPatchSize({gid_A, gid_B}, 250);
  context->glyph_condition_set.AddAndCondition(gid_A, 0);

  // Update glyph groupings.
  auto sc = context->glyph_groupings.GroupGlyphs(
      context->SegmentationInfo(), context->glyph_condition_set,
      *context->glyph_closure_cache, std::nullopt, {gid_A, gid_B}, {0, 1, 2},
      false);
  ASSERT_TRUE(sc.ok()) << sc;

  context->patch_size_cache.reset(size_cache);

  // s0 merge with (s1 AND s2) => (s0 OR s1) AND (s0 OR s2) =simplifies=> (s0 or
  // s1)
  auto r = CandidateMerge::AssessPatchMerge(
      merger, ActivationCondition::exclusive_segment(0, 0), conj_cond,
      std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());

  // Delta =
  // - (s0) * (size(gid_A) + 75)
  // - (s1 AND s2) * (size(gid_B) + 75)
  // + (s0 OR s1)) * (size(gid_A, gid_b) + 75)
  ASSERT_EQ((*r)->CostDelta(), -0.95 * (100 + 75) - (0.85 * 0.75) * (200 + 75) +
                                   (0.90) * (250 + 75));

  CandidateMerge merge = **r;
  ASSERT_EQ(merge.SegmentsToMerge(), SegmentSet({0, 1, 2}));

  auto s = merge.Apply(merger);
  ASSERT_TRUE(s.ok()) << s.status();

  // gid A and gid B should now be in the same partition.
  ASSERT_EQ(*context->glyph_groupings.CombinedPatches().Find(gid_A),
            *context->glyph_groupings.CombinedPatches().Find(gid_B));
}

TEST_F(CandidateMergeTest, ComputeInitFontCostDelta) {
  std::vector<Segment> segments = {
      {{'b'}},
      {{'f'}},
      {{'i'}},
  };

  std::vector<std::pair<Segment, double>> segment_probabilities = {
      {{{'b'}}, 0.95},
      {{{'f'}}, 0.85},
      {{{'i'}}, 0.75},
  };

  auto probability_calculator =
      std::make_unique<freq::MockProbabilityCalculator>(segment_probabilities);

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {'a'}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  Merger merger = *Merger::New(
      *context,
      MergeStrategy::CostBased(
          ProbabilityProfile(std::move(probability_calculator)), 75, 4),
      all, all);

  glyph_id_t g_a = 69;
  glyph_id_t g_b = 70;
  glyph_id_t g_f = 74;
  glyph_id_t g_i = 77;
  glyph_id_t g_fi = 444;
  glyph_id_t g_ffi = 446;

  uint32_t init_size =
      *context->patch_size_cache_for_init_font->GetPatchSize({0, g_a});
  uint32_t plus_b_size =
      *context->patch_size_cache_for_init_font->GetPatchSize({0, g_a, g_b});
  uint32_t plus_f_size =
      *context->patch_size_cache_for_init_font->GetPatchSize({0, g_a, g_f});
  uint32_t plus_f_i_fi_ffi_size =
      *context->patch_size_cache_for_init_font->GetPatchSize(
          {0, g_a, g_f, g_i, g_fi, g_ffi});
  uint32_t plus_fi_ffi_size =
      *context->patch_size_cache_for_init_font->GetPatchSize(
          {0, g_a, g_fi, g_ffi});
  uint32_t plus_fi_size =
      *context->patch_size_cache_for_init_font->GetPatchSize({0, g_a, g_fi});

  uint32_t b_size =
      *context->patch_size_cache_for_init_font->GetPatchSize({g_b});
  uint32_t f_size =
      *context->patch_size_cache_for_init_font->GetPatchSize({g_f});
  uint32_t i_size =
      *context->patch_size_cache_for_init_font->GetPatchSize({g_i});
  uint32_t fi_ffi_size =
      *context->patch_size_cache_for_init_font->GetPatchSize({g_fi, g_ffi});
  uint32_t ffi_size =
      *context->patch_size_cache_for_init_font->GetPatchSize({g_ffi});
  uint32_t i_fi_ffi_size =
      *context->patch_size_cache_for_init_font->GetPatchSize(
          {g_i, g_fi, g_ffi});

  /* Case 1: simple move */
  flat_hash_map<GlyphSet, uint32_t> smallest;
  auto r = CandidateMerge::ComputeInitFontCostDelta(merger, init_size, {g_b},
                                                    smallest);
  ASSERT_TRUE(r.ok()) << r.status();
  double delta = r->first;
  GlyphSet moved_glyphs = r->second;

  EXPECT_EQ(delta,
            // patch with b removed and moved to the init font.
            (plus_b_size - init_size) - 0.95 * (b_size + 75));
  EXPECT_EQ(moved_glyphs, (GlyphSet{g_b}));

  /* Case 2: move + modified conditions (one half of (f AND i) moved) */
  smallest.clear();
  r = CandidateMerge::ComputeInitFontCostDelta(merger, init_size, {g_f},
                                               smallest);
  ASSERT_TRUE(r.ok()) << r.status();
  delta = r->first;
  moved_glyphs = r->second;

  EXPECT_NEAR(delta,
              (plus_f_size - init_size)  /* init font increase */
                  - 0.85 * (f_size + 75) /* if (f) removed */
                  - 0.75 * (i_size + 75) /* if (i) removed */
                  -
                  (0.75 * 0.85) * (fi_ffi_size + 75) /* if (f and i) removed */
                  + 0.75 * (i_fi_ffi_size + 75) /* if (i) with ligatures */,
              1e-9);
  EXPECT_EQ(moved_glyphs, (GlyphSet{g_f}));

  /* Case 3: move non exclusive */
  smallest.clear();
  r = CandidateMerge::ComputeInitFontCostDelta(merger, init_size, {g_fi, g_ffi},
                                               smallest);
  ASSERT_TRUE(r.ok()) << r.status();
  delta = r->first;
  moved_glyphs = r->second;

  EXPECT_NEAR(delta,
              (plus_fi_ffi_size - init_size) /* init font increase */
                  -
                  (0.85 * 0.75) * (fi_ffi_size + 75), /* if (f and i) removed */
              1e-9);
  EXPECT_EQ(moved_glyphs, (GlyphSet{g_fi, g_ffi}));

  /* Case 4: move part of a patch */
  smallest.clear();
  r = CandidateMerge::ComputeInitFontCostDelta(merger, init_size, {g_fi},
                                               smallest);
  ASSERT_TRUE(r.ok()) << r.status();
  delta = r->first;
  moved_glyphs = r->second;

  EXPECT_NEAR(delta,
              (plus_fi_size - init_size) /* init font increase */
                  -
                  (0.85 * 0.75) * (fi_ffi_size + 75) /* if (f and i) removed */
                  + (0.85 * 0.75) * (ffi_size + 75), /* if (f and i) added */
              1e-9);
  EXPECT_EQ(moved_glyphs, (GlyphSet{g_fi}));

  /* Case 5: move multiple */
  smallest.clear();
  r = CandidateMerge::ComputeInitFontCostDelta(merger, init_size, {g_f, g_i},
                                               smallest);
  ASSERT_TRUE(r.ok()) << r.status();
  delta = r->first;
  moved_glyphs = r->second;

  EXPECT_NEAR(delta,
              (plus_f_i_fi_ffi_size - init_size) /* init font increase */
                  - 0.85 * (f_size + 75)         /* if (f) removed */
                  - 0.75 * (i_size + 75)         /* if (i) removed */
                  -
                  (0.75 * 0.85) * (fi_ffi_size + 75), /* if (f and i) removed */
              1e-9);
  EXPECT_EQ(moved_glyphs, (GlyphSet{g_f, g_i, g_fi, g_ffi}));
}

TEST_F(CandidateMergeTest, ComputeInitFontCostDelta_TracksSmallestDelta) {
  std::vector<Segment> segments = {
      {{'b'}},
      {{'f'}},
      {{'i'}},
  };
  std::vector<std::pair<Segment, double>> segment_probabilities = {
      {{{'b'}}, 0.95},
      {{{'f'}}, 0.85},
      {{{'i'}}, 0.75},
  };

  auto probability_calculator =
      std::make_unique<freq::MockProbabilityCalculator>(segment_probabilities);

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {'a'}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  Merger merger = *Merger::New(
      *context,
      MergeStrategy::CostBased(
          ProbabilityProfile(std::move(probability_calculator)), 75, 4),
      all, all);

  glyph_id_t g_a = 69;
  glyph_id_t g_b = 70;

  MockPatchSizeCache* mock_cache = new MockPatchSizeCache();
  context->patch_size_cache_for_init_font.reset(mock_cache);

  uint32_t init_size = 1000;
  mock_cache->SetPatchSize({0, g_a}, init_size);
  mock_cache->SetPatchSize({0, g_a, g_b}, 1100);

  flat_hash_map<GlyphSet, uint32_t> smallest_size_increases;

  // First call: size increase is 100.
  auto r = CandidateMerge::ComputeInitFontCostDelta(merger, init_size, {g_b},
                                                    smallest_size_increases);
  ASSERT_TRUE(r.ok()) << r.status();
  double first_delta = r->first;
  EXPECT_EQ(smallest_size_increases.at({g_b}), 100);

  // Second call: simulated size increase is 200 (1200 - 1000).
  // But it should use the stored smallest increase (100).
  mock_cache->SetPatchSize({0, g_a, g_b}, 1200);
  r = CandidateMerge::ComputeInitFontCostDelta(merger, init_size, {g_b},
                                               smallest_size_increases);
  ASSERT_TRUE(r.ok()) << r.status();
  double second_delta = r->first;

  EXPECT_EQ(smallest_size_increases.at({g_b}), 100);
  EXPECT_EQ(first_delta, second_delta);

  // Third call: simulated size increase is 50 (1050 - 1000).
  // It should update the stored smallest increase to 50 and return a smaller
  // delta.
  mock_cache->SetPatchSize({0, g_a, g_b}, 1050);
  r = CandidateMerge::ComputeInitFontCostDelta(merger, init_size, {g_b},
                                               smallest_size_increases);
  ASSERT_TRUE(r.ok()) << r.status();
  double third_delta = r->first;

  EXPECT_EQ(smallest_size_increases.at({g_b}), 50);

  EXPECT_LT(third_delta, first_delta);
}

TEST_F(CandidateMergeTest, ComputeCostDelta_EarlyExitAndBoundary) {
  std::vector<Segment> segments = {
      {{'a'}},
      {{'f'}},
      {{'i'}},
  };
  freq::UnicodeFrequencies frequencies{
      {{' ', ' '}, 100}, {{'a', 'a'}, 50}, {{'f', 'f'}, 75}, {{'i', 'i'}, 95}};

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  Merger merger = *Merger::New(
      *context,
      MergeStrategy::CostBased(
          *ProbabilityProfile::Unigram(std::move(frequencies)), 75, 4),
      all, all);

  MockPatchSizeCache* size_cache = new MockPatchSizeCache();
  size_cache->SetPatchSize({69}, 200);
  size_cache->SetPatchSize({74}, 300);
  size_cache->SetPatchSize({444, 446}, 150);
  size_cache->SetPatchSize({69, 74}, 450);
  context->patch_size_cache.reset(size_cache);

  GlyphSet exclusive_gids = {69, 74};
  auto exact_false = CandidateMerge::ComputeCostDelta<false>(
      merger, {0, 1}, exclusive_gids, std::nullopt);
  ASSERT_TRUE(exact_false.ok()) << exact_false.status();
  auto exact_true = CandidateMerge::ComputeCostDelta<true>(
      merger, {0, 1}, std::nullopt, std::nullopt);
  ASSERT_TRUE(exact_true.ok()) << exact_true.status();

  // Well below the actual delta: should cancel early.
  auto cancelled_false = CandidateMerge::ComputeCostDelta<false>(
      merger, {0, 1}, exclusive_gids, *exact_false - 10.0);
  EXPECT_TRUE(absl::IsCancelled(cancelled_false.status()));
  auto cancelled_true = CandidateMerge::ComputeCostDelta<true>(
      merger, {0, 1}, std::nullopt, *exact_true - 10.0);
  EXPECT_TRUE(absl::IsCancelled(cancelled_true.status()));

  // Within the FP rounding margin of the actual delta (e.g. equal to or 1 ULP
  // below): should NOT cancel early, and should return the exact sorted sum.
  auto boundary_false = CandidateMerge::ComputeCostDelta<false>(
      merger, {0, 1}, exclusive_gids,
      std::nextafter(*exact_false, -INFINITY));
  ASSERT_TRUE(boundary_false.ok()) << boundary_false.status();
  EXPECT_EQ(*boundary_false, *exact_false);

  auto boundary_true = CandidateMerge::ComputeCostDelta<true>(
      merger, {0, 1}, std::nullopt, std::nextafter(*exact_true, -INFINITY));
  ASSERT_TRUE(boundary_true.ok()) << boundary_true.status();
  EXPECT_EQ(*boundary_true, *exact_true);

  // AssessSegmentMerge should reject at exact tie and accept 1 ULP above.
  auto tied = CandidateMerge::AssessSegmentMerge(
      merger, 0, {1}, CandidateMerge::BaselineCandidate(0, *exact_false));
  ASSERT_TRUE(tied.ok()) << tied.status();
  EXPECT_FALSE(tied->has_value());

  auto better_by_1_ulp = CandidateMerge::AssessSegmentMerge(
      merger, 0, {1},
      CandidateMerge::BaselineCandidate(
          0, std::nextafter(*exact_false, INFINITY)));
  ASSERT_TRUE(better_by_1_ulp.ok()) << better_by_1_ulp.status();
  ASSERT_TRUE(better_by_1_ulp->has_value());
  EXPECT_EQ((*better_by_1_ulp)->CostDelta(), *exact_false);
}

TEST_F(CandidateMergeTest, ComputeCostDelta_FallbackChangedBeforeEarlyExit) {
  std::vector<Segment> segments = {
      {{0x54}},    // s0 -> gid 56
      {{0x6C}},    // s1 -> gid 80
      {{0x13C}},   // s2
      {{0x21A}},   // s3
      {{0xF6C3}},  // s4
  };
  freq::UnicodeFrequencies frequencies{
      {{' ', ' '}, 100},   {{0x54, 0x54}, 50},   {{0x6C, 0x6C}, 75},
      {{0x13C, 0x13C}, 10}, {{0x21A, 0x21A}, 10}, {{0xF6C3, 0xF6C3}, 10}};

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  Merger merger = *Merger::New(
      *context,
      MergeStrategy::CostBased(
          *ProbabilityProfile::Unigram(std::move(frequencies)), 75, 4),
      all, all);

  // Include an unmapped glyph in exclusive_gids so fallback_changed is
  // triggered and reduces the fallback patch size by 1000 bytes.
  GlyphSet old_fallback = context->glyph_groupings.UnmappedGlyphs();
  ASSERT_FALSE(old_fallback.empty());
  glyph_id_t unmapped_gid = *old_fallback.min();
  GlyphSet exclusive_gids = {56, 80, unmapped_gid};
  GlyphSet new_fallback = old_fallback;
  new_fallback.subtract(exclusive_gids);

  MockPatchSizeCache* size_cache = new MockPatchSizeCache();
  size_cache->SetPatchSize({56}, 200);
  size_cache->SetPatchSize({80}, 300);
  size_cache->SetPatchSize(exclusive_gids, 450);
  size_cache->SetPatchSize(old_fallback, 2000);
  size_cache->SetPatchSize(new_fallback, 1000);
  context->patch_size_cache.reset(size_cache);

  auto exact_with_fallback = CandidateMerge::ComputeCostDelta<false>(
      merger, {0, 1}, exclusive_gids, std::nullopt);
  ASSERT_TRUE(exact_with_fallback.ok()) << exact_with_fallback.status();

  // Without the -1000 fallback delta applied prior to the second loop, the
  // running delta during the second loop would be +1000 higher and would
  // falsely exceed delta_to_beat = *exact_with_fallback + 100.
  auto with_threshold = CandidateMerge::ComputeCostDelta<false>(
      merger, {0, 1}, exclusive_gids, *exact_with_fallback + 100.0);
  ASSERT_TRUE(with_threshold.ok()) << with_threshold.status();
  EXPECT_EQ(*with_threshold, *exact_with_fallback);
}

TEST_F(CandidateMergeTest, AssessMerge_Heuristic_NonExclusiveMaxPatchSize) {
  // s0: 'a' -> {69}
  // s1: 'i' -> {77}, s1 AND s3 -> {444, 446}
  // s2: 'l' -> {80}, s2 AND s3 -> {445, 447}
  // s3: 'f' -> {74}
  std::vector<Segment> segments = {
      {{'a'}},
      {{'i'}},
      {{'l'}},
      {{'f'}},
  };

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  Merger merger =
      *Merger::New(*context, MergeStrategy::Heuristic(100, 500), all, all);

  MockPatchSizeCache* size_cache = new MockPatchSizeCache();
  size_cache->SetPatchSize({69, 77}, 300);
  size_cache->SetPatchSize({77, 80}, 300);
  size_cache->SetPatchSize({444, 446}, 600);
  size_cache->SetPatchSize({445, 447}, 200);
  size_cache->SetPatchSize({444, 445, 446, 447}, 600);
  context->patch_size_cache.reset(size_cache);

  // Case 1: Merging s1 ('i') and s2 ('l') keeps the exclusive patch {77, 80}
  // under max_patch_size (300 <= 500), but collapses (s1 AND s3) and
  // (s2 AND s3) into {444, 445, 446, 447} which exceeds max_patch_size
  // (600 > 500). Should be rejected.
  auto r = CandidateMerge::AssessSegmentMerge(merger, 1, {2}, std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  EXPECT_FALSE(r->has_value());

  // Case 2: Merging s0 ('a') and s1 ('i') updates (s1 AND s3) to (s0 AND s3)
  // without growing its glyph set {444, 446}. Even though {444, 446} is
  // > 500 bytes, it did not grow as a result of the merge, so the merge should
  // be allowed.
  r = CandidateMerge::AssessSegmentMerge(merger, 0, {1}, std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  EXPECT_TRUE(r->has_value());

  // Case 3: When the collapsed non-exclusive patch {444, 445, 446, 447} fits
  // within max_patch_size (450 <= 500), merging s1 and s2 succeeds.
  size_cache->SetPatchSize({444, 445, 446, 447}, 450);
  r = CandidateMerge::AssessSegmentMerge(merger, 1, {2}, std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  EXPECT_TRUE(r->has_value());
}

TEST_F(CandidateMergeTest,
       AssessMerge_Heuristic_PureDepGraphModeSkipsClosure) {
  // s0: 'i' -> {77}, s0 AND s2 -> {444, 446}
  // s1: 'l' -> {80}, s1 AND s2 -> {445, 447}
  // s2: 'f' -> {74}
  std::vector<Segment> segments = {
      {{'i'}},
      {{'l'}},
      {{'f'}},
  };

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, DEP_GRAPH_ONLY, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  Merger merger =
      *Merger::New(*context, MergeStrategy::Heuristic(100, 500), all, all);

  MockPatchSizeCache* size_cache = new MockPatchSizeCache();
  size_cache->SetPatchSize({77, 80}, 600);
  size_cache->SetPatchSize({444, 445, 446, 447}, 450);
  context->patch_size_cache.reset(size_cache);

  uint64_t hits_before = context->glyph_closure_cache->CacheHits();
  uint64_t misses_before = context->glyph_closure_cache->CacheMisses();

  // Case 1: Merged unitary patch {77, 80} exceeds max_patch_size (600 > 500).
  auto r = CandidateMerge::AssessSegmentMerge(merger, 0, {1}, std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  EXPECT_FALSE(r->has_value());

  // Case 2: Merged unitary patch fits (300 <= 500), but collapsed non-exclusive
  // patch {444, 445, 446, 447} exceeds max_patch_size (600 > 500).
  size_cache->SetPatchSize({77, 80}, 300);
  size_cache->SetPatchSize({444, 445, 446, 447}, 600);
  r = CandidateMerge::AssessSegmentMerge(merger, 0, {1}, std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  EXPECT_FALSE(r->has_value());

  // Case 3: Both fit within max_patch_size.
  size_cache->SetPatchSize({444, 445, 446, 447}, 450);
  r = CandidateMerge::AssessSegmentMerge(merger, 0, {1}, std::nullopt);
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_TRUE(r->has_value());

  // In pure dep-graph mode, AssessSegmentMerge should never invoke
  // AnalyzeSegment / GlyphClosureCache.
  EXPECT_EQ(context->glyph_closure_cache->CacheHits(), hits_before);
  EXPECT_EQ(context->glyph_closure_cache->CacheMisses(), misses_before);
}

TEST_F(CandidateMergeTest, AssessMerge_CostDeltas_UnicodeRangeIgnoresNonExclusivePatches) {
  // s0: 'a' -> {69}
  // s1: 'f' -> {74}
  // s2: 'i' -> {77}
  // Non-exclusive patch (s1 AND s2): 'f' + 'i' -> {444, 446}
  std::vector<Segment> segments = {
      {{'a'}},
      {{'f'}},
      {{'i'}},
  };
  freq::UnicodeFrequencies frequencies{
      {{' ', ' '}, 100}, {{'a', 'a'}, 50}, {{'f', 'f'}, 75}, {{'i', 'i'}, 95}};

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver,
                                  ift::config::UNICODE_RANGE);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver,
      ift::config::UNICODE_RANGE);
  ASSERT_TRUE(context.ok()) << context.status();

  Merger merger = *Merger::New(
      *context,
      MergeStrategy::CostBased(
          *ProbabilityProfile::Unigram(std::move(frequencies)), 75, 4),
      all, all);

  MockPatchSizeCache* size_cache = new MockPatchSizeCache();
  size_cache->SetPatchSize({}, 100);
  size_cache->SetPatchSize({69}, 200);
  size_cache->SetPatchSize({74}, 300);
  size_cache->SetPatchSize({77}, 250);
  size_cache->SetPatchSize({444, 446}, 150);
  size_cache->SetPatchSize({69, 74}, 450);
  size_cache->SetPatchSize({74, 77, 444, 446}, 600);
  context->patch_size_cache.reset(size_cache);

  // Case 1: Merging s0 ('a') and s1 ('f') modifies (s1 AND s2) -> (s0 AND s2),
  // which remains non-exclusive. In UNICODE_RANGE mode non-exclusive patches
  // are dropped, so the cost delta only reflects removing {a} and {f} and
  // adding {a, f}.
  double p_a_or_f = 0.50 + 0.75 - 0.50 * 0.75;
  double expected_delta_a_f = -0.50 * (200 + 75) - 0.75 * (300 + 75) +
                              p_a_or_f * (450 + 75);
  auto r_a_f = CandidateMerge::AssessSegmentMerge(merger, 0, {1}, std::nullopt);
  ASSERT_TRUE(r_a_f.ok()) << r_a_f.status();
  ASSERT_TRUE(r_a_f->has_value());
  EXPECT_NEAR((*r_a_f)->CostDelta(), expected_delta_a_f, 1e-9);

  // Case 2: Merging s1 ('f') and s2 ('i') turns the previously non-exclusive
  // patch {444, 446} into part of the new exclusive patch {74, 77, 444, 446}.
  // Since {444, 446} was non-exclusive prior to the merge, its pre-merge cost
  // is not subtracted, while the post-merge exclusive patch includes it.
  double p_f_or_i = 0.75 + 0.95 - 0.75 * 0.95;
  double expected_delta_f_i = -0.75 * (300 + 75) - 0.95 * (250 + 75) +
                              p_f_or_i * (600 + 75);
  auto r_f_i = CandidateMerge::AssessSegmentMerge(merger, 1, {2}, std::nullopt);
  ASSERT_TRUE(r_f_i.ok()) << r_f_i.status();
  ASSERT_TRUE(r_f_i->has_value());
  EXPECT_NEAR((*r_f_i)->CostDelta(), expected_delta_f_i, 1e-9);
}

namespace {

class HighDefaultPatchSizeCache : public PatchSizeCache {
 public:
  explicit HighDefaultPatchSizeCache(uint32_t default_size)
      : default_size_(default_size) {}

  absl::StatusOr<uint32_t> GetPatchSize(
      const ift::common::GlyphSet& gids) override {
    auto it = patch_sizes_.find(gids);
    if (it != patch_sizes_.end()) {
      return it->second;
    }
    return default_size_;
  }

  void LogBrotliCallCount() const override {}

  void SetPatchSize(const ift::common::GlyphSet& gids, uint32_t size) {
    patch_sizes_[gids] = size;
  }

 private:
  uint32_t default_size_;
  absl::flat_hash_map<ift::common::GlyphSet, uint32_t> patch_sizes_;
};

}  // namespace

TEST_F(CandidateMergeTest,
       TryNextPatchMerge_ExcludesGlyphsThatBecameExclusiveDuringSegmentMerging) {

  // s0 (aacute) and s1 (eacute) share acute accent glyphs via (s0 OR s1).
  // s2 ('f'), s3 ('i'), s4 ('l') form composite ligature patches (s2 AND s3)
  // and (s2 AND s4).
  std::vector<Segment> segments = {
      {{0x00E1}},  // aacute
      {{0x00E9}},  // eacute
      {{'f'}},
      {{'i'}},
      {{'l'}},
  };

  freq::UnicodeFrequencies frequencies{
      {{' ', ' '}, 100},
      {{0x00E1, 0x00E1}, 90},
      {{0x00E9, 0x00E9}, 90},
      {{'f', 'f'}, 40},
      {{'i', 'i'}, 40},
      {{'l', 'l'}, 40},
  };

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  GlyphSet g0 = context->glyph_groupings.ExclusiveGlyphs(0);
  GlyphSet g1 = context->glyph_groupings.ExclusiveGlyphs(1);
  ASSERT_FALSE(g0.empty());
  ASSERT_FALSE(g1.empty());
  ASSERT_TRUE(context->glyph_groupings.UnmappedGlyphs().empty());

  ASSERT_TRUE(context->glyph_groupings.ConditionsAndGlyphs().contains(
      ActivationCondition::or_segments({0, 1}, 0)));
  GlyphSet g_acute = context->glyph_groupings.ConditionsAndGlyphs().at(
      ActivationCondition::or_segments({0, 1}, 0));
  ASSERT_FALSE(g_acute.empty());

  ASSERT_TRUE(context->glyph_groupings.ConditionsAndGlyphs().contains(
      ActivationCondition::and_segments({2, 3}, 0)));
  GlyphSet g_fi = context->glyph_groupings.ConditionsAndGlyphs().at(
      ActivationCondition::and_segments({2, 3}, 0));
  ASSERT_FALSE(g_fi.empty());

  ASSERT_TRUE(context->glyph_groupings.ConditionsAndGlyphs().contains(
      ActivationCondition::and_segments({2, 4}, 0)));
  GlyphSet g_fl = context->glyph_groupings.ConditionsAndGlyphs().at(
      ActivationCondition::and_segments({2, 4}, 0));
  ASSERT_FALSE(g_fl.empty());

  auto* size_cache = new HighDefaultPatchSizeCache(/*default_size=*/5000);
  size_cache->SetPatchSize({}, 10);
  for (const auto& [cond, glyphs] :
       context->glyph_groupings.ConditionsAndGlyphs()) {
    size_cache->SetPatchSize(glyphs, 30);
  }

  GlyphSet g01 = g0;
  g01.union_set(g1);
  g01.union_set(g_acute);
  size_cache->SetPatchSize(g01, 40);

  GlyphSet g_fi_fl = g_fi;
  g_fi_fl.union_set(g_fl);
  size_cache->SetPatchSize(g_fi_fl, 30);

  // Make merging s0's post-merge exclusive patch with the composite patches
  // strongly cost-reducing if s0 were ever leaked into TryNextPatchMerge().
  GlyphSet g01_fi = g01;
  g01_fi.union_set(g_fi);
  size_cache->SetPatchSize(g01_fi, 40);
  GlyphSet g01_fl = g01;
  g01_fl.union_set(g_fl);
  size_cache->SetPatchSize(g01_fl, 40);
  GlyphSet g01_fi_fl = g01;
  g01_fi_fl.union_set(g_fi_fl);
  size_cache->SetPatchSize(g01_fi_fl, 40);

  context->patch_size_cache.reset(size_cache);

  MergeStrategy strategy = MergeStrategy::CostBased(
      *ProbabilityProfile::Unigram(std::move(frequencies)),
      /*network_overhead_cost=*/75,
      /*min_group_size=*/1);
  strategy.SetUsePatchMerges(true);
  strategy.SetOptimizationCutoffFraction(0.0);

  Merger merger = *Merger::New(*context, strategy, all, all);

  // Step 1: Merges segments {0, 1} into segment 0.
  auto step1 = merger.TryNextMerge();
  ASSERT_TRUE(step1.ok()) << step1.status();
  ASSERT_TRUE(step1->has_value());
  EXPECT_EQ((*step1)->base_segment, 0u);
  EXPECT_EQ((*step1)->segments, (SegmentSet{0, 1}));
  ASSERT_TRUE(context->ReprocessChanged(**step1).ok());

  // g_acute has now collapsed into the exclusive patch for segment 0.
  EXPECT_EQ(context->glyph_groupings.ExclusiveGlyphs(0), g01);

  // Step 2 & 3: Patch merging combines composite ligature patches.
  auto step2 = merger.TryNextMerge();
  ASSERT_TRUE(step2.ok()) << step2.status();
  ASSERT_TRUE(step2->has_value());
  ASSERT_TRUE(context->ReprocessChanged(**step2).ok());

  auto step3 = merger.TryNextMerge();
  ASSERT_TRUE(step3.ok()) << step3.status();
  ASSERT_TRUE(step3->has_value());
  ASSERT_TRUE(context->ReprocessChanged(**step3).ok());

  // Step 4: No further merges remain. The exclusive patch for segment 0 must
  // NOT be merged into any composite patch.
  auto step4 = merger.TryNextMerge();
  ASSERT_TRUE(step4.ok()) << step4.status();
  EXPECT_FALSE(step4->has_value());

  EXPECT_EQ(context->glyph_groupings.ExclusiveGlyphs(0), g01);
  EXPECT_EQ(context->glyph_groupings.ConditionsAndGlyphs().at(
                ActivationCondition::exclusive_segment(0, 0)),
            g01);
}

TEST_F(CandidateMergeTest,
       TryNextPatchMerge_ExcludesConditionSimplifiedToExclusiveDuringPatchMerging) {
  std::vector<Segment> segments = {
      {{'f', 'b'}},
      {{'i', 'd'}},
      {{'l', 'g'}},
      {{0x00F2, 'h'}},  // ograve, 'h'
      {{0x00F9, 'm'}},  // ugrave, 'm'
  };

  freq::UnicodeFrequencies frequencies{
      {{' ', ' '}, 100},
      {{'f', 'f'}, 90},
      {{'i', 'i'}, 80},
      {{'l', 'l'}, 80},
      {{0x00F2, 0x00F2}, 40},
      {{0x00F9, 0x00F9}, 40},
  };

  ClosureGlyphSegmenter segmenter(
      8, 8, PATCH, DEP_GRAPH_ONLY_WITH_SIMPLIFICATION, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  GlyphSet g0 = context->glyph_groupings.ExclusiveGlyphs(0);
  ASSERT_FALSE(g0.empty());

  ASSERT_TRUE(context->glyph_groupings.ConditionsAndGlyphs().contains(
      ActivationCondition::and_segments({0, 1}, 0)));
  GlyphSet g_fi = context->glyph_groupings.ConditionsAndGlyphs().at(
      ActivationCondition::and_segments({0, 1}, 0));
  ASSERT_FALSE(g_fi.empty());

  ASSERT_TRUE(context->glyph_groupings.ConditionsAndGlyphs().contains(
      ActivationCondition::and_segments({0, 2}, 0)));
  GlyphSet g_fl = context->glyph_groupings.ConditionsAndGlyphs().at(
      ActivationCondition::and_segments({0, 2}, 0));
  ASSERT_FALSE(g_fl.empty());

  ASSERT_TRUE(context->glyph_groupings.ConditionsAndGlyphs().contains(
      ActivationCondition::or_segments({3, 4}, 0)));
  GlyphSet g_grave = context->glyph_groupings.ConditionsAndGlyphs().at(
      ActivationCondition::or_segments({3, 4}, 0));
  ASSERT_FALSE(g_grave.empty());

  auto* size_cache = new HighDefaultPatchSizeCache(/*default_size=*/5000);
  size_cache->SetPatchSize({}, 10);
  for (const auto& [cond, glyphs] :
       context->glyph_groupings.ConditionsAndGlyphs()) {
    size_cache->SetPatchSize(glyphs, 30);
  }

  GlyphSet g_fi_fl = g_fi;
  g_fi_fl.union_set(g_fl);
  size_cache->SetPatchSize(g_fi_fl, 40);

  GlyphSet g0_with_fi_fl = g0;
  g0_with_fi_fl.union_set(g_fi_fl);
  size_cache->SetPatchSize(g0_with_fi_fl, 40);

  // Make merging the simplified exclusive condition if (s0) with (s3 OR s4)
  // strongly cost-reducing if if (s0) were not excluded on subsequent calls.
  GlyphSet g0_with_fi_fl_and_grave = g0_with_fi_fl;
  g0_with_fi_fl_and_grave.union_set(g_grave);
  size_cache->SetPatchSize(g0_with_fi_fl_and_grave, 40);

  context->patch_size_cache.reset(size_cache);

  MergeStrategy strategy = MergeStrategy::CostBased(
      *ProbabilityProfile::Unigram(std::move(frequencies)),
      /*network_overhead_cost=*/75,
      /*min_group_size=*/1);
  strategy.SetUsePatchMerges(true);
  strategy.SetOptimizationCutoffFraction(0.0);

  Merger merger = *Merger::New(*context, strategy, all, all);

  // Step 1: Merges (s0 AND s1) with (s0 AND s2). Under
  // DEP_GRAPH_ONLY_WITH_SIMPLIFICATION, NonCompositeSuperset() simplifies
  // s0 AND (s1 OR s2) to exclusive condition if (s0) then p0.
  auto step1 = merger.TryNextMerge();
  ASSERT_TRUE(step1.ok()) << step1.status();
  ASSERT_TRUE(step1->has_value());
  ASSERT_TRUE(context->ReprocessChanged(**step1).ok());

  EXPECT_EQ(context->glyph_groupings.ConditionsAndGlyphs().at(
                ActivationCondition::exclusive_segment(0, 0)),
            g0_with_fi_fl);

  // Step 2: The simplified condition if (s0) is exclusive and must NOT be
  // merged with if ((s3 OR s4)). No further patch merges remain.
  auto step2 = merger.TryNextMerge();
  ASSERT_TRUE(step2.ok()) << step2.status();
  EXPECT_FALSE(step2->has_value());

  EXPECT_EQ(context->glyph_groupings.ConditionsAndGlyphs().at(
                ActivationCondition::exclusive_segment(0, 0)),
            g0_with_fi_fl);
  EXPECT_EQ(context->glyph_groupings.ConditionsAndGlyphs().at(
                ActivationCondition::or_segments({3, 4}, 0)),
            g_grave);
}

TEST_F(CandidateMergeTest, ComputeCostDelta_InertPairFastPathAndPreCheck) {
  // s0: 'a' -> {69}, s1: 'b' -> {70}, s2: 'c' -> {71} (all inert in Roboto).
  std::vector<Segment> segments = {
      {{'a'}},
      {{'b'}},
      {{'c'}},
  };
  freq::UnicodeFrequencies frequencies{
      {{' ', ' '}, 100}, {{'a', 'a'}, 80}, {{'b', 'b'}, 5}, {{'c', 'c'}, 75}};

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();
  ASSERT_TRUE(context->InertSegments().contains(0));
  ASSERT_TRUE(context->InertSegments().contains(1));
  ASSERT_TRUE(context->InertSegments().contains(2));

  Merger merger = *Merger::New(
      *context,
      MergeStrategy::CostBased(
          *ProbabilityProfile::Unigram(std::move(frequencies)), 75, 4),
      all, all);

  MockPatchSizeCache* size_cache = new MockPatchSizeCache();
  size_cache->SetPatchSize({69}, 200);
  size_cache->SetPatchSize({70}, 300);
  size_cache->SetPatchSize({71}, 240);
  size_cache->SetPatchSize({69, 71}, 320);
  context->patch_size_cache.reset(size_cache);

  // First evaluation warms the per-segment exclusive patch size cache for s0
  // and s2.
  size_cache->ResetCallCount();
  auto best_case_0_2 = CandidateMerge::ComputeCostDelta<true>(
      merger, {0, 2}, std::nullopt, std::nullopt);
  ASSERT_TRUE(best_case_0_2.ok()) << best_case_0_2.status();
  EXPECT_EQ(size_cache->GetPatchSizeCallCount(), 2);

  // Expected best-case merged size for {0, 2}: max(200, 240) + min(200, 240)/2
  // = 240 + 100 = 340.
  double p_0_or_2 = 0.80 + 0.75 - 0.80 * 0.75;
  double expected_best_0_2 =
      -0.80 * (200 + 75) - 0.75 * (240 + 75) + p_0_or_2 * (340 + 75);
  EXPECT_NEAR(*best_case_0_2, expected_best_0_2, 1e-9);

  // Subsequent best-case evaluation of {0, 2} hits the per-segment size cache
  // with zero calls to PatchSizeCache::GetPatchSize.
  size_cache->ResetCallCount();
  auto best_case_0_2_cached = CandidateMerge::ComputeCostDelta<true>(
      merger, {0, 2}, std::nullopt, std::nullopt);
  ASSERT_TRUE(best_case_0_2_cached.ok()) << best_case_0_2_cached.status();
  EXPECT_EQ(*best_case_0_2_cached, *best_case_0_2);
  EXPECT_EQ(size_cache->GetPatchSizeCallCount(), 0);

  // Evaluating {0, 1} with delta_to_beat = 0.0: s0 (p=0.80, size=200) and
  // s1 (p=0.05, size=300) has best-case merged size 300 + 100 = 400. Even at
  // the lower-bound probability max(0.80, 0.05) = 0.80, the delta is
  // 0.80*(400+75) - 0.80*(200+75) - 0.05*(300+75) = 160 - 18.75 = +141.25 > 0,
  // so the O(1) pre-check cancels immediately.
  auto cancelled_precheck =
      CandidateMerge::ComputeCostDelta<true>(merger, {0, 1}, std::nullopt, 0.0);
  EXPECT_TRUE(absl::IsCancelled(cancelled_precheck.status()));

  // Exact evaluation of {0, 2} via the inert fast-path.
  GlyphSet exclusive_0_2 = {69, 71};
  auto exact_0_2 = CandidateMerge::ComputeCostDelta<false>(
      merger, {0, 2}, exclusive_0_2, std::nullopt);
  ASSERT_TRUE(exact_0_2.ok()) << exact_0_2.status();
  double expected_exact_0_2 =
      -0.80 * (200 + 75) - 0.75 * (240 + 75) + p_0_or_2 * (320 + 75);
  EXPECT_NEAR(*exact_0_2, expected_exact_0_2, 1e-9);

  // Boundary check: 1 ULP below exact_0_2 does not cancel early.
  auto boundary_0_2 = CandidateMerge::ComputeCostDelta<false>(
      merger, {0, 2}, exclusive_0_2, std::nextafter(*exact_0_2, -INFINITY));
  ASSERT_TRUE(boundary_0_2.ok()) << boundary_0_2.status();
  EXPECT_EQ(*boundary_0_2, *exact_0_2);

  // Applying the inert merge of {0, 2} invalidates s0's cached metadata while
  // keeping s1's cached exclusive patch size intact.
  auto candidate =
      CandidateMerge::AssessSegmentMerge(merger, 0, {2}, std::nullopt);
  ASSERT_TRUE(candidate.ok()) << candidate.status();
  ASSERT_TRUE(candidate->has_value());
  ASSERT_TRUE((*candidate)->Apply(merger).ok());

  size_cache->ResetCallCount();
  EXPECT_EQ(*merger.ExclusivePatchSize(1), 300u);
  EXPECT_EQ(size_cache->GetPatchSizeCallCount(), 0);
  EXPECT_EQ(*merger.ExclusivePatchSize(0), 320u);
  EXPECT_EQ(size_cache->GetPatchSizeCallCount(), 1);
  EXPECT_NEAR(*merger.AggregateProbability(0), p_0_or_2, 1e-9);
}

TEST_F(CandidateMergeTest,
       ComputeCostDelta_InertPairFastPath_UnicodeRangeMode) {
  std::vector<Segment> segments = {
      {{'a'}},  // gid 69
      {{'c'}},  // gid 71
  };
  freq::UnicodeFrequencies frequencies{
      {{' ', ' '}, 100}, {{'a', 'a'}, 80}, {{'c', 'c'}, 75}};

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver,
                                  ift::config::UNICODE_RANGE);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver,
      ift::config::UNICODE_RANGE);
  ASSERT_TRUE(context.ok()) << context.status();

  Merger merger = *Merger::New(
      *context,
      MergeStrategy::CostBased(
          *ProbabilityProfile::Unigram(std::move(frequencies)), 75, 4),
      all, all);

  MockPatchSizeCache* size_cache = new MockPatchSizeCache();
  size_cache->SetPatchSize({}, 100);
  size_cache->SetPatchSize({69}, 200);
  size_cache->SetPatchSize({71}, 240);
  size_cache->SetPatchSize({69, 71}, 320);
  context->patch_size_cache.reset(size_cache);

  // In UNICODE_RANGE mode, base_overhead = 100 is subtracted before computing
  // incremental sizes: inc_0 = 200 - 100 = 100, inc_1 = 240 - 100 = 140,
  // extra = min(100, 140) * 0.5 = 50, so best-case merged size = 240 + 50 = 290.
  double p_0_or_1 = 0.80 + 0.75 - 0.80 * 0.75;
  double expected_best =
      -0.80 * (200 + 75) - 0.75 * (240 + 75) + p_0_or_1 * (290 + 75);
  auto best_delta = CandidateMerge::ComputeCostDelta<true>(
      merger, {0, 1}, std::nullopt, std::nullopt);
  ASSERT_TRUE(best_delta.ok()) << best_delta.status();
  EXPECT_NEAR(*best_delta, expected_best, 1e-9);

  GlyphSet exclusive_gids = {69, 71};
  double expected_exact =
      -0.80 * (200 + 75) - 0.75 * (240 + 75) + p_0_or_1 * (320 + 75);
  auto exact_delta = CandidateMerge::ComputeCostDelta<false>(
      merger, {0, 1}, exclusive_gids, std::nullopt);
  ASSERT_TRUE(exact_delta.ok()) << exact_delta.status();
  EXPECT_NEAR(*exact_delta, expected_exact, 1e-9);
}

namespace {
struct ScopedAllPairsMergingFlag {
  explicit ScopedAllPairsMergingFlag(bool enabled)
      : prev(absl::GetFlag(FLAGS_experimental_all_pairs_merging)) {
    absl::SetFlag(&FLAGS_experimental_all_pairs_merging, enabled);
  }
  ~ScopedAllPairsMergingFlag() {
    absl::SetFlag(&FLAGS_experimental_all_pairs_merging, prev);
  }
  bool prev;
};
}  // namespace

TEST_F(CandidateMergeTest, AllPairsMerging_AvoidsSingleBaseGreedyTrap) {
  // s0 ('a', gid 69) has a marginally negative merge with s1 ('b', gid 70)
  // (delta = -4.0), whereas s1 ('b', gid 70) and s2 ('c', gid 71) have a much
  // stronger negative merge (delta = -201.25).
  std::vector<Segment> segments = {
      {{'a'}},
      {{'b'}},
      {{'c'}},
  };
  std::vector<std::pair<Segment, double>> probabilities = {
      {{{'a'}}, 0.90},
      {{{'b'}}, 0.80},
      {{{'c'}}, 0.79},
      {{{'a', 'b'}}, 0.90},
      {{{'b', 'c'}}, 0.80},
      {{{'a', 'c'}}, 0.99},
      {{{'a', 'b', 'c'}}, 0.99},
  };

  auto run_merger = [&](bool all_pairs) {
    ScopedAllPairsMergingFlag flag_override(all_pairs);
    ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver);
    auto context = SegmentationContext::InitializeSegmentationContext(
        roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
        segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
        segmenter.init_font_merging_brotli_quality(), resolver);
    EXPECT_TRUE(context.ok()) << context.status();

    auto* size_cache = new MockPatchSizeCache();
    size_cache->SetPatchSize({69}, 200);
    size_cache->SetPatchSize({70}, 200);
    size_cache->SetPatchSize({71}, 200);
    size_cache->SetPatchSize({69, 70}, 440);
    size_cache->SetPatchSize({70, 71}, 220);
    size_cache->SetPatchSize({69, 71}, 450);
    size_cache->SetPatchSize({69, 70, 71}, 650);
    context->patch_size_cache.reset(size_cache);

    auto calc = std::make_shared<MockProbabilityCalculator>(probabilities);
    Merger merger = *Merger::New(
        *context, MergeStrategy::CostBased(ProbabilityProfile(calc), 75, 1),
        all, all);

    while (true) {
      auto modified = merger.TryNextMerge();
      EXPECT_TRUE(modified.ok()) << modified.status();
      if (!modified->has_value()) {
        break;
      }
      EXPECT_TRUE(context->ReprocessChanged(**modified).ok());
    }

    std::vector<SubsetDefinition> result;
    for (const auto& seg : context->SegmentationInfo().Segments()) {
      result.push_back(seg.Definition());
    }
    return result;
  };

  // Single-base greedy mode merges {0, 1} first, trapping s2 unmerged.
  std::vector<SubsetDefinition> single_base_result = run_merger(false);
  std::vector<SubsetDefinition> expected_single_base = {
      {'a', 'b'},
      {},
      {'c'},
  };
  EXPECT_EQ(single_base_result, expected_single_base);

  // All-pairs mode merges {1, 2} first and leaves s0 separate.
  std::vector<SubsetDefinition> all_pairs_result = run_merger(true);
  std::vector<SubsetDefinition> expected_all_pairs = {
      {'a'},
      {'b', 'c'},
      {},
  };
  EXPECT_EQ(all_pairs_result, expected_all_pairs);
}

TEST_F(CandidateMergeTest,
       AllPairsMerging_Phase2ResumesPhase1AfterForcedMerge) {
  // With min_group_size = 2:
  // - s0 ('a', gid 69) and s1 ('b', gid 70) each have 1 codepoint; s2
  //   ({'c', 'd'}, gids {71, 72}) already has 2 codepoints.
  // - All initial 2-segment merges have positive cost deltas, so Phase 1 makes
  //   no merges and Phase 2 forces s0 to merge with s1 (delta = +6.5).
  // - Once s0 becomes {'a', 'b'}, merging s0 with s2 has a negative cost delta
  //   (delta = -220.25). Even though both s0 and s2 now meet min_group_size = 2,
  //   AllPairsMerger refreshes dirty segment s0 and resumes Phase 1 to apply
  //   the cost-reducing merge {0, 2}.
  ScopedAllPairsMergingFlag flag_override(true);
  std::vector<Segment> segments = {
      {{'a'}},
      {{'b'}},
      {{'c', 'd'}},
  };
  std::vector<std::pair<Segment, double>> probabilities = {
      {{{'a'}}, 0.80},
      {{{'b'}}, 0.50},
      {{{'c', 'd'}}, 0.75},
      {{{'a', 'b'}}, 0.80},
      {{{'a', 'c', 'd'}}, 0.95},
      {{{'b', 'c', 'd'}}, 0.95},
      {{{'a', 'b', 'c', 'd'}}, 0.80},
  };

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  auto* size_cache = new MockPatchSizeCache();
  size_cache->SetPatchSize({69}, 200);
  size_cache->SetPatchSize({70}, 200);
  size_cache->SetPatchSize({71, 72}, 240);
  size_cache->SetPatchSize({69, 70}, 380);
  size_cache->SetPatchSize({69, 71, 72}, 450);
  size_cache->SetPatchSize({70, 71, 72}, 450);
  size_cache->SetPatchSize({69, 70, 71, 72}, 400);
  context->patch_size_cache.reset(size_cache);

  auto calc = std::make_shared<MockProbabilityCalculator>(probabilities);
  Merger merger = *Merger::New(
      *context, MergeStrategy::CostBased(ProbabilityProfile(calc), 75, 2), all,
      all);

  // Step 1: Phase 1 finds no negative merges; Phase 2 forces {0, 1}.
  auto step1 = merger.TryNextMerge();
  ASSERT_TRUE(step1.ok()) << step1.status();
  ASSERT_TRUE(step1->has_value());
  EXPECT_EQ((*step1)->base_segment, 0u);
  EXPECT_EQ((*step1)->segments, (SegmentSet{0, 1}));
  ASSERT_TRUE(context->ReprocessChanged(**step1).ok());

  // Step 2: Phase 1 resumes on dirty segment 0 and applies {0, 2}.
  auto step2 = merger.TryNextMerge();
  ASSERT_TRUE(step2.ok()) << step2.status();
  ASSERT_TRUE(step2->has_value());
  EXPECT_EQ((*step2)->base_segment, 0u);
  EXPECT_EQ((*step2)->segments, (SegmentSet{0, 2}));
  ASSERT_TRUE(context->ReprocessChanged(**step2).ok());

  auto step3 = merger.TryNextMerge();
  ASSERT_TRUE(step3.ok()) << step3.status();
  EXPECT_FALSE(step3->has_value());
}

TEST_F(CandidateMergeTest,
       AllPairsMerging_Phase2UsesAllPairsProbabilityOrdering) {
  // Initial probabilities are descending:
  // s0 ('a', 0.88), s1 ('b', 0.50), s2 ('c', 0.001), s3 ('d', 0.48),
  // s4 ('e', 0.48).
  // In Phase 1, {3, 4} merges into s3 with P({'d', 'e'}) = 0.66 > P(s1) = 0.50.
  // In Phase 2 (min_group_size = 2), CollectExclusiveCandidateMerges iterates
  // using AllPairsMerger's probability order (s3 at 0.66, then s1 at 0.50,
  // then s2 at 0.001) rather than segment-index order:
  // - s3 (0.66) is evaluated before s1 (0.50), so on an equal positive cost
  //   delta (+1.54), s3 is selected over s1.
  // - s2 (0.001) is visited after s3 and s1 and breaks early on
  //   inert_threshold (0.0065).
  ScopedAllPairsMergingFlag flag_override(true);
  std::vector<Segment> segments = {
      {{'a'}},
      {{'b'}},
      {{'c'}},
      {{'d'}},
      {{'e'}},
  };
  std::vector<std::pair<Segment, double>> probabilities = {
      {{{'a'}}, 0.88},
      {{{'b'}}, 0.50},
      {{{'c'}}, 0.001},
      {{{'d'}}, 0.48},
      {{{'e'}}, 0.48},
      {{{'d', 'e'}}, 0.66},
      {{{'a', 'b'}}, 0.88},
      {{{'a', 'c'}}, 0.88},
      {{{'a', 'd'}}, 0.88},
      {{{'a', 'e'}}, 0.88},
      {{{'a', 'd', 'e'}}, 0.88},
      {{{'b', 'c'}}, 0.501},
      {{{'b', 'd'}}, 0.80},
      {{{'b', 'e'}}, 0.80},
      {{{'b', 'd', 'e'}}, 0.85},
      {{{'c', 'd'}}, 0.481},
      {{{'c', 'e'}}, 0.481},
      {{{'c', 'd', 'e'}}, 0.661},
  };

  ClosureGlyphSegmenter segmenter(8, 8, PATCH, CLOSURE_ONLY, resolver);
  auto context = SegmentationContext::InitializeSegmentationContext(
      roboto.get(), {}, segments, segmenter.unmapped_glyph_handling(),
      segmenter.condition_analysis_mode(), segmenter.brotli_quality(),
      segmenter.init_font_merging_brotli_quality(), resolver);
  ASSERT_TRUE(context.ok()) << context.status();

  // Glyphs: 'a'=69, 'b'=70, 'c'=71, 'd'=72, 'e'=73.
  auto* size_cache = new MockPatchSizeCache();
  size_cache->SetPatchSize({69}, 500);
  size_cache->SetPatchSize({70}, 200);
  size_cache->SetPatchSize({71}, 200);
  size_cache->SetPatchSize({72}, 200);
  size_cache->SetPatchSize({73}, 200);
  // {3, 4} has negative best-case and exact cost delta in Phase 1:
  // best-case: 0.66 * (300 + 75) - 2 * 0.48 * (200 + 75) = 247.5 - 264.0 = -16.5.
  // exact: 0.66 * (200 + 75) - 264.0 = 181.5 - 264.0 = -82.5.
  size_cache->SetPatchSize({72, 73}, 200);
  // In Phase 2 for s0 (cost = 0.88 * 575 = 506.0):
  // Both {0, 1} and {0, 3} have the exact same positive cost delta (+1.54):
  // - With s3={'d', 'e'} (cost = 0.66 * 275 = 181.5):
  //   size({69, 72, 73}) = 708 -> 0.88 * 783 - 506.0 - 181.5 = 689.04 - 687.5 = +1.54.
  // - With s1={'b'} (cost = 0.50 * 275 = 137.5):
  //   size({69, 70}) = 658 -> 0.88 * 733 - 506.0 - 137.5 = 645.04 - 643.5 = +1.54.
  // Because s3 (0.66) precedes s1 (0.50) in AllPairsMerger's probability order,
  // s3 is evaluated first and wins the tie.
  size_cache->SetPatchSize({69, 72, 73}, 708);
  size_cache->SetPatchSize({69, 70}, 658);
  size_cache->SetPatchSize({69, 72}, 700);
  size_cache->SetPatchSize({69, 73}, 700);
  size_cache->SetPatchSize({70, 72}, 400);
  size_cache->SetPatchSize({70, 73}, 400);
  size_cache->SetPatchSize({70, 72, 73}, 400);
  context->patch_size_cache.reset(size_cache);

  auto calc = std::make_shared<MockProbabilityCalculator>(probabilities);
  Merger merger = *Merger::New(
      *context, MergeStrategy::CostBased(ProbabilityProfile(calc), 75, 2), all,
      all);

  // Step 1 (Phase 1): {3, 4} merges into s3.
  auto step1 = merger.TryNextMerge();
  ASSERT_TRUE(step1.ok()) << step1.status();
  ASSERT_TRUE(step1->has_value());
  EXPECT_EQ((*step1)->base_segment, 3u);
  EXPECT_EQ((*step1)->segments, (SegmentSet{3, 4}));
  ASSERT_TRUE(context->ReprocessChanged(**step1).ok());

  // Step 2 (Phase 2): s0 merges with s3 (evaluated before s1 due to higher
  // post-merge probability, and breaking before s2 on inert_threshold).
  auto step2 = merger.TryNextMerge();
  ASSERT_TRUE(step2.ok()) << step2.status();
  ASSERT_TRUE(step2->has_value());
  EXPECT_EQ((*step2)->base_segment, 0u);
  EXPECT_EQ((*step2)->segments, (SegmentSet{0, 3}));
}

}  // namespace ift::encoder
