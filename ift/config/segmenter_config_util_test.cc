#include "ift/config/segmenter_config_util.h"

#include <optional>
#include <vector>

#include "absl/container/btree_map.h"
#include "gtest/gtest.h"
#include "ift/common/bazel_data_file_resolver.h"
#include "ift/common/data_file_resolver.h"
#include "ift/common/int_set.h"
#include "ift/encoder/merge_strategy.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/bigram_probability_calculator.h"
#include "ift/freq/unicode_frequencies.h"
#include "ift/freq/unigram_probability_calculator.h"

using ift::config::CostConfiguration;
using ift::config::Features;
using ift::config::MergeGroup;
using ift::config::SegmenterConfig;

using absl::btree_map;
using absl::btree_set;
using ift::common::BazelDataFileResolver;
using ift::common::CodepointSet;
using ift::common::DataFileResolver;
using ift::common::SegmentSet;
using ift::config::SegmenterConfigUtil;
using ift::encoder::MergeStrategy;
using ProbabilityProfile = ift::encoder::MergeStrategy::ProbabilityProfile;
using ift::encoder::SubsetDefinition;
using ift::freq::BigramProbabilityCalculator;
using ift::freq::UnicodeFrequencies;
using ift::freq::UnicodeFrequenciesBuilder;
using ift::freq::UnigramProbabilityCalculator;

class SegmenterConfigUtilTest : public ::testing::Test {
 protected:
  SegmenterConfigUtilTest()
      : resolver(*BazelDataFileResolver::CreateForTest()) {}

  std::shared_ptr<DataFileResolver> resolver;
};

void AddSegment(SegmenterConfig& config, uint32_t id, CodepointSet codepoints) {
  for (unsigned cp : codepoints) {
    (*config.mutable_segments())[id].mutable_codepoints()->add_values(cp);
  }
}

void AddFreqData(MergeGroup& group, const std::string& path) {
  group.mutable_cost_config()->add_frequency_data()->set_path_to_frequency_data(
      path);
}

// Helpers used by the tests which check that the deprecated single frequency
// data set fields still work.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

void AddLegacyFreqData(CostConfiguration& config, const std::string& path) {
  config.set_path_to_frequency_data(path);
}

void SetLegacyUseBigrams(CostConfiguration& config, bool value) {
  config.set_use_bigrams(value);
}

void SetLegacyInitFontThreshold(CostConfiguration& config, double value) {
  config.set_initial_font_merge_threshold(value);
}

void SetLegacyInitFontProbabilityThreshold(CostConfiguration& config,
                                           double value) {
  config.set_initial_font_merge_probability_threshold(value);
}

#pragma GCC diagnostic pop

MergeStrategy ExpectedCostStrategy(
    unsigned net_overhead,
    std::optional<int> init_font_threshold = std::nullopt) {
  UnicodeFrequenciesBuilder freq_builder;
  freq_builder.Add(1, 1, 1);

  auto profile =
      *MergeStrategy::ProbabilityProfile::Unigram(freq_builder.Build());
  profile.init_font_merge_threshold = init_font_threshold;

  MergeStrategy s =
      MergeStrategy::CostBased(std::move(profile), net_overhead, 1);
  s.SetOptimizationCutoffFraction(0.001);

  return s;
}

TEST_F(SegmenterConfigUtilTest, ConfigToMergeGroups_NoMergeGroups) {
  SegmenterConfig config;
  AddSegment(config, 5, {14, 32});
  AddSegment(config, 2, {10, 15});

  CodepointSet font_codepoints{
      10,
      15,
      32,
  };

  SegmenterConfigUtil util("", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups =
      util.ConfigToMergeGroups(config, font_codepoints, {}, segments_out);
  ASSERT_TRUE(groups.ok()) << groups.status();

  ASSERT_TRUE(groups->empty());
  ASSERT_EQ(segments_out, (std::vector<SubsetDefinition>{{10, 15}, {32}}));
}

TEST_F(SegmenterConfigUtilTest,
       ConfigToMergeGroups_InitFontCodepointsExcluded) {
  SegmenterConfig config;
  config.mutable_initial_segment()->mutable_codepoints()->add_values(2);
  config.mutable_initial_segment()->mutable_codepoints()->add_values(8);

  auto* group = config.add_merge_groups();
  group->mutable_heuristic_config()->set_min_patch_size(101);

  CodepointSet font_codepoints{1, 2, 4, 8, 9};

  SegmenterConfigUtil util("", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups =
      util.ConfigToMergeGroups(config, font_codepoints, {}, segments_out);
  ASSERT_TRUE(groups.ok()) << groups.status();

  ASSERT_EQ(segments_out, (std::vector<SubsetDefinition>{
                              {1},
                              {4},
                              {9},
                          }));

  ASSERT_EQ(*groups, (btree_map<SegmentSet, MergeStrategy>{
                         {{0, 1, 2}, MergeStrategy::Heuristic(101)}}));
}

TEST_F(SegmenterConfigUtilTest,
       ConfigToMergeGroups_SegmentIdsWithoutSegmentsIgnored) {
  SegmenterConfig config;
  config.mutable_initial_segment()->mutable_codepoints()->add_values(2);

  auto* group = config.add_merge_groups();
  group->mutable_segment_ids()->add_values(1);
  // There's no segment for these two: 2 is in the initial font and 77 isn't
  // in the font at all.
  group->mutable_segment_ids()->add_values(2);
  group->mutable_segment_ids()->add_values(77);
  group->mutable_heuristic_config()->set_min_patch_size(101);

  CodepointSet font_codepoints{1, 2, 4};

  SegmenterConfigUtil util("", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups =
      util.ConfigToMergeGroups(config, font_codepoints, {}, segments_out);
  ASSERT_TRUE(groups.ok()) << groups.status();

  ASSERT_EQ(segments_out, (std::vector<SubsetDefinition>{
                              {1},
                              {4},
                          }));

  ASSERT_EQ(*groups, (btree_map<SegmentSet, MergeStrategy>{
                         {{0}, MergeStrategy::Heuristic(101)}}));
}

TEST_F(SegmenterConfigUtilTest, ConfigToMergeGroups_NoSegments_Heuristic) {
  // Minimal config with one heuristic merge group that covers everything.
  SegmenterConfig config;
  auto* group = config.add_merge_groups();
  group->mutable_segment_ids();
  group->mutable_heuristic_config()->set_min_patch_size(101);

  CodepointSet font_codepoints{1, 2, 4};

  SegmenterConfigUtil util("", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups =
      util.ConfigToMergeGroups(config, font_codepoints, {}, segments_out);
  ASSERT_TRUE(groups.ok()) << groups.status();

  ASSERT_EQ(segments_out, (std::vector<SubsetDefinition>{
                              {1},
                              {2},
                              {4},
                          }));

  ASSERT_EQ(*groups, (btree_map<SegmentSet, MergeStrategy>{
                         {{}, MergeStrategy::Heuristic(101)}}));
}

TEST_F(SegmenterConfigUtilTest,
       ConfigToMergeGroups_SegmentsInferred_Heuristic) {
  // Minimal config with one heuristic merge group that covers everything.
  SegmenterConfig config;
  auto* group = config.add_merge_groups();
  group->mutable_heuristic_config()->set_min_patch_size(101);

  CodepointSet font_codepoints{1, 2, 4};

  SegmenterConfigUtil util("", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups =
      util.ConfigToMergeGroups(config, font_codepoints, {}, segments_out);
  ASSERT_TRUE(groups.ok()) << groups.status();

  ASSERT_EQ(segments_out, (std::vector<SubsetDefinition>{
                              {1},
                              {2},
                              {4},
                          }));

  ASSERT_EQ(*groups, (btree_map<SegmentSet, MergeStrategy>{
                         {{0, 1, 2}, MergeStrategy::Heuristic(101)}}));
}

TEST_F(SegmenterConfigUtilTest, ConfigToMergeGroups_FeatureSegments) {
  // Utilizes the additional feature segments mechanism.
  SegmenterConfig config;

  Features features;
  features.add_values("foo ");
  features.add_values("bar ");
  config.mutable_feature_segments()->insert(std::make_pair(2, features));

  auto* group = config.add_merge_groups();
  group->mutable_heuristic_config()->set_min_patch_size(101);
  group->mutable_feature_segment_ids()->add_values(2);

  CodepointSet font_codepoints{1, 2, 4};

  SegmenterConfigUtil util("", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups =
      util.ConfigToMergeGroups(config, font_codepoints, {}, segments_out);
  ASSERT_TRUE(groups.ok()) << groups.status();

  SubsetDefinition def_with_features;
  def_with_features.feature_tags = {HB_TAG('f', 'o', 'o', ' '),
                                    HB_TAG('b', 'a', 'r', ' ')};

  ASSERT_EQ(segments_out, (std::vector<SubsetDefinition>{
                              def_with_features,
                              {1},
                              {2},
                              {4},
                          }));

  ASSERT_EQ(*groups, (btree_map<SegmentSet, MergeStrategy>{
                         {{0, 1, 2, 3}, MergeStrategy::Heuristic(101)}}));
}

TEST_F(SegmenterConfigUtilTest, ConfigToMergeGroups_FeatureSegmentsInferred) {
  // Minimal config with one heuristic merge group that covers everything.
  SegmenterConfig config;
  config.set_generate_feature_segments(true);

  CodepointSet font_codepoints{1, 2, 4};
  btree_set<hb_tag_t> font_features{HB_TAG('f', 'o', 'o', ' '),
                                    HB_TAG('b', 'a', 'r', ' '),
                                    HB_TAG('c', 'u', 'r', 's')};

  SegmenterConfigUtil util("", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups = util.ConfigToMergeGroups(config, font_codepoints, font_features,
                                         segments_out);
  ASSERT_TRUE(groups.ok()) << groups.status();

  SubsetDefinition foo;
  foo.feature_tags = {
      HB_TAG('f', 'o', 'o', ' '),
  };

  SubsetDefinition bar;
  bar.feature_tags = {
      HB_TAG('b', 'a', 'r', ' '),
  };

  ASSERT_EQ(segments_out, (std::vector<SubsetDefinition>{
                              bar,
                              foo,
                              {1},
                              {2},
                              {4},
                          }));

  ASSERT_EQ(*groups, (btree_map<SegmentSet, MergeStrategy>{}));
}

TEST_F(SegmenterConfigUtilTest,
       ConfigToMergeGroups_SegmentsProvided_Heuristic) {
  // Minimal config with one heuristic merge group that covers everything.
  SegmenterConfig config;

  AddSegment(config, 20, {1, 2});
  AddSegment(config, 10, {3, 4});
  AddSegment(config, 30, {5, 6});

  auto* group = config.add_merge_groups();
  group->mutable_segment_ids()->add_values(10);
  group->mutable_segment_ids()->add_values(30);
  group->mutable_heuristic_config()->set_min_patch_size(101);

  group = config.add_merge_groups();
  group->mutable_segment_ids()->add_values(20);
  group->mutable_segment_ids()->add_values(30);
  group->mutable_heuristic_config()->set_min_patch_size(102);

  CodepointSet font_codepoints{1, 2, 4, 6};

  SegmenterConfigUtil util("", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups =
      util.ConfigToMergeGroups(config, font_codepoints, {}, segments_out);
  ASSERT_TRUE(groups.ok()) << groups.status();

  ASSERT_EQ(segments_out, (std::vector<SubsetDefinition>{
                              {4},
                              {1, 2},
                              {6},
                          }));

  ASSERT_EQ(*groups, (btree_map<SegmentSet, MergeStrategy>{
                         {{0, 2}, MergeStrategy::Heuristic(101)},
                         {{1, 2}, MergeStrategy::Heuristic(102)}}));
}

TEST_F(SegmenterConfigUtilTest, ConfigToMergeGroups_SegmentsInferred_Cost) {
  SegmenterConfig config;
  auto* group = config.add_merge_groups();
  AddFreqData(*group, "test_freq_data.riegeli");
  group->mutable_cost_config()->set_network_overhead_cost(85);

  CodepointSet font_codepoints{0x40, 0x42, 0x43, 0x45, 0x47};

  SegmenterConfigUtil util("util/testdata/config.txtpb", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups =
      util.ConfigToMergeGroups(config, font_codepoints, {}, segments_out);
  ASSERT_TRUE(groups.ok()) << groups.status();

  ASSERT_EQ(segments_out, (std::vector<SubsetDefinition>{
                              {0x40},
                              {0x42},
                              {0x43},
                              {0x45},
                              {0x47},
                          }));

  ASSERT_EQ(
      *groups,
      (btree_map<SegmentSet, MergeStrategy>{{{2}, ExpectedCostStrategy(85)}}));
}

TEST_F(SegmenterConfigUtilTest,
       ConfigToMergeGroups_Cost_SetsInitFontThreshold) {
  SegmenterConfig config;
  auto* group = config.add_merge_groups();
  AddFreqData(*group, "test_freq_data.riegeli");
  group->mutable_cost_config()->set_network_overhead_cost(85);
  group->mutable_cost_config()
      ->mutable_frequency_data(0)
      ->set_initial_font_merge_threshold(-70);

  CodepointSet font_codepoints{0x40, 0x42, 0x43, 0x45, 0x47};

  SegmenterConfigUtil util("util/testdata/config.txtpb", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups =
      util.ConfigToMergeGroups(config, font_codepoints, {}, segments_out);
  ASSERT_TRUE(groups.ok()) << groups.status();

  ASSERT_EQ(segments_out, (std::vector<SubsetDefinition>{
                              {0x40},
                              {0x42},
                              {0x43},
                              {0x45},
                              {0x47},
                          }));

  ASSERT_EQ(*groups, (btree_map<SegmentSet, MergeStrategy>{
                         {{2}, ExpectedCostStrategy(85, -70)}}));
}

TEST_F(SegmenterConfigUtilTest,
       ConfigToMergeGroups_SegmentsInferred_MergeGroupsSpecified_Cost) {
  // Uses the deprecated single frequency data set fields, including one
  // supplied by the base cost config.
  SegmenterConfig config;
  SetLegacyInitFontThreshold(*config.mutable_base_cost_config(), -90);

  auto* group = config.add_merge_groups();
  AddLegacyFreqData(*group->mutable_cost_config(), "test_freq_data.riegeli");
  group->mutable_cost_config()->set_network_overhead_cost(85);
  group->mutable_segment_ids()->add_values(0x44);

  CodepointSet font_codepoints{0x40, 0x42, 0x43, 0x44, 0x45, 0x47};

  SegmenterConfigUtil util("util/testdata/config.txtpb", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups =
      util.ConfigToMergeGroups(config, font_codepoints, {}, segments_out);

  ASSERT_TRUE(groups.ok()) << groups.status();

  ASSERT_EQ(segments_out, (std::vector<SubsetDefinition>{
                              {0x40},
                              {0x42},
                              {0x43},
                              {0x44},
                              {0x45},
                              {0x47},
                          }));

  ASSERT_EQ(*groups, (btree_map<SegmentSet, MergeStrategy>{
                         {{3}, ExpectedCostStrategy(85, -90)},
                     }));
}

TEST_F(SegmenterConfigUtilTest, ConfigToMergeGroups_SegmentsProvided_Cost) {
  SegmenterConfig config;
  AddSegment(config, 11, {0x41, 0x42});
  AddSegment(config, 21, {0x43, 0x44});
  AddSegment(config, 31, {0x45, 0x46});

  auto* group = config.add_merge_groups();
  AddFreqData(*group, "test_freq_data.riegeli");
  group->mutable_cost_config()->set_network_overhead_cost(10);
  group->mutable_segment_ids()->add_values(21);

  group = config.add_merge_groups();
  AddFreqData(*group, "test_freq_data.riegeli");
  group->mutable_cost_config()->set_network_overhead_cost(20);
  group->mutable_segment_ids()->add_values(31);

  group = config.add_merge_groups();
  AddFreqData(*group, "test_freq_data.riegeli");
  group->mutable_cost_config()->set_network_overhead_cost(30);
  group->mutable_segment_ids()->add_values(11);
  group->mutable_segment_ids()->add_values(31);

  CodepointSet font_codepoints{0x42, 0x43, 0x44, 0x45};

  SegmenterConfigUtil util("util/testdata/config.txtpb", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups =
      util.ConfigToMergeGroups(config, font_codepoints, {}, segments_out);

  ASSERT_TRUE(groups.ok()) << groups.status();

  ASSERT_EQ(segments_out, (std::vector<SubsetDefinition>{
                              {0x42},
                              {0x43, 0x44},
                              {0x45},
                          }));

  ASSERT_EQ(*groups, (btree_map<SegmentSet, MergeStrategy>{
                         {{1}, ExpectedCostStrategy(10)},
                         {{2}, ExpectedCostStrategy(20)},
                         {{0, 2}, ExpectedCostStrategy(30)},
                     }));
}

TEST_F(SegmenterConfigUtilTest, ConfigToMergeGroups_CostRequiresFreqData) {
  SegmenterConfig config;
  auto* group = config.add_merge_groups();
  group->mutable_cost_config()->set_network_overhead_cost(85);

  CodepointSet font_codepoints{0x40, 0x42, 0x43, 0x45, 0x47};

  SegmenterConfigUtil util("util/testdata/config.txtpb", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups =
      util.ConfigToMergeGroups(config, font_codepoints, {}, segments_out);
  ASSERT_TRUE(absl::IsInvalidArgument(groups.status())) << groups.status();
}

TEST_F(SegmenterConfigUtilTest, ConfigToMergeGroups_MultipleFrequencyData) {
  SegmenterConfig config;
  auto* cost = config.add_merge_groups()->mutable_cost_config();
  cost->set_network_overhead_cost(85);

  // Covers 0x43 (and 0x44, which is not in the font).
  auto* freq = cost->add_frequency_data();
  freq->set_path_to_frequency_data("test_freq_data.riegeli");
  freq->set_initial_font_merge_threshold(-70);
  freq->set_initial_font_merge_probability_threshold(0.25);

  // Covers 0x45 and 0x47.
  freq = cost->add_frequency_data();
  freq->set_path_to_frequency_data("test_freq_data_2.riegeli");
  freq->set_use_bigrams(true);

  CodepointSet font_codepoints{0x40, 0x42, 0x43, 0x45, 0x47};

  SegmenterConfigUtil util("util/testdata/config.txtpb", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups =
      util.ConfigToMergeGroups(config, font_codepoints, {}, segments_out);
  ASSERT_TRUE(groups.ok()) << groups.status();

  ASSERT_EQ(segments_out, (std::vector<SubsetDefinition>{
                              {0x40},
                              {0x42},
                              {0x43},
                              {0x45},
                              {0x47},
                          }));

  // Segments are inferred from the union of the codepoints covered by both
  // data sets: 0x43, 0x45 and 0x47.
  ASSERT_EQ(groups->size(), 1);
  const auto& [group_segments, strategy] = *groups->begin();
  ASSERT_EQ(group_segments, (SegmentSet{2, 3, 4}));

  ASSERT_EQ(strategy.ProbabilityProfiles().size(), 2);
  const auto& profile_1 = strategy.ProbabilityProfiles()[0];
  const auto& profile_2 = strategy.ProbabilityProfiles()[1];

  ASSERT_EQ(profile_1.init_font_merge_threshold, -70);
  ASSERT_EQ(profile_1.init_font_merge_probability_threshold, 0.25);
  ASSERT_EQ(profile_2.init_font_merge_threshold, std::nullopt);
  ASSERT_EQ(profile_2.init_font_merge_probability_threshold, std::nullopt);

  // use_bigrams is per data set.
  ASSERT_NE(dynamic_cast<const UnigramProbabilityCalculator*>(
                profile_1.calculator.get()),
            nullptr);
  ASSERT_NE(dynamic_cast<const BigramProbabilityCalculator*>(
                profile_2.calculator.get()),
            nullptr);
}

TEST_F(SegmenterConfigUtilTest,
       ConfigToMergeGroups_FrequencyDataOverridesBaseConfig) {
  SegmenterConfig config;
  // Covers 0x45 and 0x47.
  config.mutable_base_cost_config()
      ->add_frequency_data()
      ->set_path_to_frequency_data("test_freq_data_2.riegeli");
  config.mutable_base_cost_config()->set_network_overhead_cost(85);

  // Covers 0x43 only, and replaces the base config's data set.
  auto* cost = config.add_merge_groups()->mutable_cost_config();
  cost->add_frequency_data()->set_path_to_frequency_data(
      "test_freq_data.riegeli");

  CodepointSet font_codepoints{0x40, 0x42, 0x43, 0x45, 0x47};

  SegmenterConfigUtil util("util/testdata/config.txtpb", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups =
      util.ConfigToMergeGroups(config, font_codepoints, {}, segments_out);
  ASSERT_TRUE(groups.ok()) << groups.status();

  ASSERT_EQ(*groups, (btree_map<SegmentSet, MergeStrategy>{
                         {{2}, ExpectedCostStrategy(85)}}));
}

TEST_F(SegmenterConfigUtilTest,
       ConfigToMergeGroups_FrequencyDataFromBaseConfig) {
  SegmenterConfig config;
  // Covers 0x45 and 0x47.
  config.mutable_base_cost_config()
      ->add_frequency_data()
      ->set_path_to_frequency_data("test_freq_data_2.riegeli");

  config.add_merge_groups()->mutable_cost_config()->set_network_overhead_cost(
      85);

  CodepointSet font_codepoints{0x40, 0x42, 0x43, 0x45, 0x47};

  SegmenterConfigUtil util("util/testdata/config.txtpb", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups =
      util.ConfigToMergeGroups(config, font_codepoints, {}, segments_out);
  ASSERT_TRUE(groups.ok()) << groups.status();

  ASSERT_EQ(*groups, (btree_map<SegmentSet, MergeStrategy>{
                         {{3, 4}, ExpectedCostStrategy(85)}}));
}

TEST_F(SegmenterConfigUtilTest,
       ConfigToMergeGroups_LegacyAndFrequencyDataIsInvalid) {
  SegmenterConfig config;
  auto* cost = config.add_merge_groups()->mutable_cost_config();
  AddLegacyFreqData(*cost, "test_freq_data.riegeli");
  cost->add_frequency_data()->set_path_to_frequency_data(
      "test_freq_data_2.riegeli");

  CodepointSet font_codepoints{0x40, 0x42, 0x43, 0x45, 0x47};

  SegmenterConfigUtil util("util/testdata/config.txtpb", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups =
      util.ConfigToMergeGroups(config, font_codepoints, {}, segments_out);
  ASSERT_TRUE(absl::IsInvalidArgument(groups.status())) << groups.status();
}

TEST_F(SegmenterConfigUtilTest, ConfigToMergeGroups_LegacyFreqDataFields) {
  // The deprecated single data set fields are converted into an equivalent
  // single probability profile.
  SegmenterConfig config;
  auto* cost = config.add_merge_groups()->mutable_cost_config();
  AddLegacyFreqData(*cost, "test_freq_data.riegeli");
  SetLegacyUseBigrams(*cost, true);
  SetLegacyInitFontThreshold(*cost, -70);
  SetLegacyInitFontProbabilityThreshold(*cost, 0.25);

  CodepointSet font_codepoints{0x40, 0x42, 0x43, 0x45, 0x47};

  SegmenterConfigUtil util("util/testdata/config.txtpb", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups =
      util.ConfigToMergeGroups(config, font_codepoints, {}, segments_out);
  ASSERT_TRUE(groups.ok()) << groups.status();

  ASSERT_EQ(groups->size(), 1);
  const auto& [group_segments, strategy] = *groups->begin();
  ASSERT_EQ(group_segments, (SegmentSet{2}));

  ASSERT_EQ(strategy.ProbabilityProfiles().size(), 1);
  const auto& profile = strategy.ProbabilityProfiles()[0];
  ASSERT_EQ(profile.init_font_merge_threshold, -70);
  ASSERT_EQ(profile.init_font_merge_probability_threshold, 0.25);
  ASSERT_NE(
      dynamic_cast<const BigramProbabilityCalculator*>(profile.calculator.get()),
      nullptr);
}

TEST_F(SegmenterConfigUtilTest, ConfigToMergeGroups_FallbackMergeGroup) {
  // This tests the optional addition of a catch all merge group.
  SegmenterConfig config;
  AddSegment(config, 1, {0x41, 0x42});
  AddSegment(config, 2, {0x43, 0x44});
  AddSegment(config, 3, {0x45, 0x46});
  AddSegment(config, 4, {0x47, 0x48});

  auto* group = config.add_merge_groups();
  AddFreqData(*group, "test_freq_data.riegeli");
  group->mutable_segment_ids()->add_values(1);

  group = config.add_merge_groups();
  AddFreqData(*group, "test_freq_data.riegeli");
  group->mutable_segment_ids()->add_values(2);

  config.mutable_ungrouped_config()->set_min_patch_size(100);

  CodepointSet font_codepoints{0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47};

  SegmenterConfigUtil util("util/testdata/config.txtpb", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups =
      util.ConfigToMergeGroups(config, font_codepoints, {}, segments_out);

  ASSERT_TRUE(groups.ok()) << groups.status();

  ASSERT_EQ(segments_out, (std::vector<SubsetDefinition>{
                              {0x41, 0x42},
                              {0x43, 0x44},
                              {0x45, 0x46},
                              {0x47},
                          }));

  ASSERT_EQ(*groups, (btree_map<SegmentSet, MergeStrategy>{
                         {{0}, ExpectedCostStrategy(75)},
                         {{1}, ExpectedCostStrategy(75)},
                         {{2, 3}, MergeStrategy::Heuristic(100)},
                     }));
}

TEST_F(SegmenterConfigUtilTest,
       ConfigToMergeGroups_FallbackMergeGroupNotNeeded) {
  // This tests the optional addition of a catch all merge group.
  SegmenterConfig config;
  AddSegment(config, 1, {0x41, 0x42});
  AddSegment(config, 2, {0x43, 0x44});

  auto* group = config.add_merge_groups();
  AddFreqData(*group, "test_freq_data.riegeli");
  group->mutable_segment_ids()->add_values(1);

  group = config.add_merge_groups();
  AddFreqData(*group, "test_freq_data.riegeli");
  group->mutable_segment_ids()->add_values(2);

  config.mutable_ungrouped_config()->set_min_patch_size(100);

  CodepointSet font_codepoints{0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47};

  SegmenterConfigUtil util("util/testdata/config.txtpb", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups =
      util.ConfigToMergeGroups(config, font_codepoints, {}, segments_out);

  ASSERT_TRUE(groups.ok()) << groups.status();

  ASSERT_EQ(segments_out, (std::vector<SubsetDefinition>{
                              {0x41, 0x42},
                              {0x43, 0x44},
                          }));

  ASSERT_EQ(*groups, (btree_map<SegmentSet, MergeStrategy>{
                         {{0}, ExpectedCostStrategy(75)},
                         {{1}, ExpectedCostStrategy(75)},
                     }));
}

TEST_F(SegmenterConfigUtilTest, ConfigToMergeGroups_OptimizationSettings) {
  SegmenterConfig config;
  auto* group = config.add_merge_groups();
  AddFreqData(*group, "test_freq_data.riegeli");

  group->mutable_cost_config()->set_optimization_cutoff_fraction(0.12);
  group->mutable_cost_config()->set_best_case_size_reduction_fraction(0.34);

  CodepointSet font_codepoints{0x40, 0x42, 0x43, 0x45, 0x47};

  SegmenterConfigUtil util("util/testdata/config.txtpb", resolver);

  std::vector<SubsetDefinition> segments_out;
  auto groups =
      util.ConfigToMergeGroups(config, font_codepoints, {}, segments_out);
  ASSERT_TRUE(groups.ok()) << groups.status();

  MergeStrategy expected = ExpectedCostStrategy(75);
  expected.SetOptimizationCutoffFraction(0.12);
  expected.SetBestCaseSizeReductionFraction(0.34);

  ASSERT_EQ(*groups, (btree_map<SegmentSet, MergeStrategy>{{{2}, expected}}));
}

// TODO test for feature segment auto generation.