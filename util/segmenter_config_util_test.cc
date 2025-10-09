#include "util/segmenter_config_util.h"

#include <vector>

#include "absl/container/btree_map.h"
#include "common/int_set.h"
#include "gtest/gtest.h"
#include "ift/encoder/merge_strategy.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/unicode_frequencies.h"

using absl::btree_map;
using common::CodepointSet;
using common::SegmentSet;
using ift::encoder::MergeStrategy;
using ift::encoder::SubsetDefinition;
using ift::freq::UnicodeFrequencies;
using util::SegmenterConfigUtil;

class SegmenterConfigUtilTest : public ::testing::Test {
 protected:
  SegmenterConfigUtilTest() {}
};

void AddSegment(SegmenterConfig& config, uint32_t id, CodepointSet codepoints) {
  for (unsigned cp : codepoints) {
    (*config.mutable_segments())[id].mutable_codepoints()->add_values(cp);
  }
}

MergeStrategy ExpectedCostStrategy(unsigned net_overhead,
                                   int init_font_threshold) {
  UnicodeFrequencies freq;
  freq.Add(1, 1, 1);

  MergeStrategy s = *MergeStrategy::CostBased(std::move(freq), net_overhead, 1);
  s.SetOptimizationCutoffFraction(0.001);
  s.SetInitFontMergeThreshold(init_font_threshold);

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

  SegmenterConfigUtil util("");

  std::vector<SubsetDefinition> segments_out;
  auto groups = util.ConfigToMergeGroups(config, font_codepoints, segments_out);
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

  SegmenterConfigUtil util("");

  std::vector<SubsetDefinition> segments_out;
  auto groups = util.ConfigToMergeGroups(config, font_codepoints, segments_out);
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
       ConfigToMergeGroups_SegmentsInferred_Heuristic) {
  // Minimal config with one heuristic merge group that covers everything.
  SegmenterConfig config;
  auto* group = config.add_merge_groups();
  group->mutable_heuristic_config()->set_min_patch_size(101);

  CodepointSet font_codepoints{1, 2, 4};

  SegmenterConfigUtil util("");

  std::vector<SubsetDefinition> segments_out;
  auto groups = util.ConfigToMergeGroups(config, font_codepoints, segments_out);
  ASSERT_TRUE(groups.ok()) << groups.status();

  ASSERT_EQ(segments_out, (std::vector<SubsetDefinition>{
                              {1},
                              {2},
                              {4},
                          }));

  ASSERT_EQ(*groups, (btree_map<SegmentSet, MergeStrategy>{
                         {{0, 1, 2}, MergeStrategy::Heuristic(101)}}));
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

  SegmenterConfigUtil util("");

  std::vector<SubsetDefinition> segments_out;
  auto groups = util.ConfigToMergeGroups(config, font_codepoints, segments_out);
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
  group->mutable_cost_config()->set_path_to_frequency_data(
      "test_freq_data.riegeli");
  group->mutable_cost_config()->set_network_overhead_cost(85);

  CodepointSet font_codepoints{0x40, 0x42, 0x43, 0x45, 0x47};

  SegmenterConfigUtil util("util/testdata/config.txtpb");

  std::vector<SubsetDefinition> segments_out;
  auto groups = util.ConfigToMergeGroups(config, font_codepoints, segments_out);
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
  SegmenterConfig config;
  config.mutable_base_cost_config()->set_init_font_merge_threshold(-90);

  auto* group = config.add_merge_groups();
  group->mutable_cost_config()->set_path_to_frequency_data(
      "test_freq_data.riegeli");
  group->mutable_cost_config()->set_network_overhead_cost(85);
  group->mutable_segment_ids()->add_values(0x44);

  CodepointSet font_codepoints{0x40, 0x42, 0x43, 0x44, 0x45, 0x47};

  SegmenterConfigUtil util("util/testdata/config.txtpb");

  std::vector<SubsetDefinition> segments_out;
  auto groups = util.ConfigToMergeGroups(config, font_codepoints, segments_out);

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
  group->mutable_cost_config()->set_path_to_frequency_data(
      "test_freq_data.riegeli");
  group->mutable_cost_config()->set_network_overhead_cost(10);
  group->mutable_segment_ids()->add_values(21);

  group = config.add_merge_groups();
  group->mutable_cost_config()->set_path_to_frequency_data(
      "test_freq_data.riegeli");
  group->mutable_cost_config()->set_network_overhead_cost(20);
  group->mutable_segment_ids()->add_values(31);

  group = config.add_merge_groups();
  group->mutable_cost_config()->set_path_to_frequency_data(
      "test_freq_data.riegeli");
  group->mutable_cost_config()->set_network_overhead_cost(30);
  group->mutable_segment_ids()->add_values(11);
  group->mutable_segment_ids()->add_values(31);

  CodepointSet font_codepoints{0x42, 0x43, 0x44, 0x45};

  SegmenterConfigUtil util("util/testdata/config.txtpb");

  std::vector<SubsetDefinition> segments_out;
  auto groups = util.ConfigToMergeGroups(config, font_codepoints, segments_out);

  ASSERT_TRUE(groups.ok()) << groups.status();

  ASSERT_EQ(segments_out, (std::vector<SubsetDefinition>{
                              {0x42},
                              {0x43, 0x44},
                              {0x45},
                          }));

  ASSERT_EQ(*groups, (btree_map<SegmentSet, MergeStrategy>{
                         {{1}, ExpectedCostStrategy(10, -70)},
                         {{2}, ExpectedCostStrategy(20, -70)},
                         {{0, 2}, ExpectedCostStrategy(30, -70)},
                     }));
}

TEST_F(SegmenterConfigUtilTest, ConfigToMergeGroups_CostRequiresFreqData) {
  SegmenterConfig config;
  auto* group = config.add_merge_groups();
  group->mutable_cost_config()->set_network_overhead_cost(85);

  CodepointSet font_codepoints{0x40, 0x42, 0x43, 0x45, 0x47};

  SegmenterConfigUtil util("util/testdata/config.txtpb");

  std::vector<SubsetDefinition> segments_out;
  auto groups = util.ConfigToMergeGroups(config, font_codepoints, segments_out);
  ASSERT_TRUE(absl::IsInvalidArgument(groups.status())) << groups.status();
}
