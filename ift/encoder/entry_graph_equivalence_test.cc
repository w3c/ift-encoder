#include <fstream>
#include <string>
#include <vector>
#include <set>

#include "gtest/gtest.h"
#include "google/protobuf/text_format.h"
#include "ift/common/int_set.h"
#include "ift/config/segmentation_plan.pb.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/entry_graph.h"
#include "ift/encoder/subset_definition.h"
#include "ift/proto/patch_encoding.h"
#include "ift/proto/format_2_patch_map.h"

using ift::config::SegmentationPlan;
using ift::config::ActivationConditionProto;
using ift::common::SegmentSet;
using ift::common::CodepointSet;
using absl::flat_hash_map;
using absl::flat_hash_set;
using ift::proto::Format2PatchMap;
using ift::proto::PatchMap;

// This tests the entry graph generation and optimization to ensure
// that it's maintaining equivalence to input conditions on a realistic
// example.
//
// Conditions from a real segmentation of Roboto VF are converted to
// entries via the entry graph and then those are checked for equivalence
// against the original conditions.
//
// Both the optimized and unoptimized versions are checked as both cases
// should preserve the original conditions.

namespace ift::encoder {

static ActivationCondition FromProto(const ActivationConditionProto& condition) {
  std::vector<SegmentSet> groups;
  for (const auto& group : condition.required_segments()) {
    SegmentSet set;
    set.insert(group.values().begin(), group.values().end());
    groups.push_back(set);
  }

  return ActivationCondition::composite_condition(groups,
                                                  condition.activated_patch());
}

static ActivationCondition Normalize(const ActivationCondition& condition) {
  return ActivationCondition::composite_condition(condition.conditions(), 0);
}

static ActivationCondition RealToVirtual(
    const ActivationCondition& real_cond,
    const flat_hash_map<segment_index_t, ActivationCondition>& segment_to_virtual) {
  std::vector<SegmentSet> virtual_groups;
  for (const auto& real_group : real_cond.conditions()) {
    SegmentSet virt_group;
    for (segment_index_t s_id : real_group) {
      virt_group.union_set(segment_to_virtual.at(s_id).conditions()[0]);
    }
    virtual_groups.push_back(virt_group);
  }
  return ActivationCondition::composite_condition(virtual_groups, 0);
}

static int64_t TotalEncodingCost(const std::vector<proto::PatchMap::Entry>& entries) {
  int64_t total = 0;
  for (const auto& entry : entries) {
    auto cost = Format2PatchMap::EstimateEncodingCost(entry);
    if (cost.ok()) {
      total += *cost;
    }
  }
  return total;
}


struct VirtualMaps {
  flat_hash_map<hb_codepoint_t, uint32_t> cp_to_virtual;
  flat_hash_map<hb_tag_t, uint32_t> feature_to_virtual;
};

static ActivationCondition ResolveVirtualCondition(
    const PatchMap::Entry& entry,
    const std::vector<PatchMap::Entry>& all_entries,
    const VirtualMaps& maps) {

  std::vector<ActivationCondition> direct_conditions;
  if (!entry.coverage.codepoints.empty()) {
    SegmentSet s;
    for (hb_codepoint_t cp : entry.coverage.codepoints) {
      s.insert(maps.cp_to_virtual.at(cp));
    }
    if (!s.empty()) {
      direct_conditions.push_back(ActivationCondition::or_segments(s, 0));
    }
  }

  if (!entry.coverage.features.empty()) {
    SegmentSet s;
    for (hb_tag_t tag : entry.coverage.features) {
      s.insert(maps.feature_to_virtual.at(tag));
    }
    if (!s.empty()) {
      direct_conditions.push_back(ActivationCondition::or_segments(s, 0));
    }
  }

  std::vector<ActivationCondition> children;
  for (uint32_t child_idx : entry.coverage.child_indices) {
    children.push_back(ResolveVirtualCondition(all_entries[child_idx], all_entries,
                                               maps));
  }

  if (entry.coverage.conjunctive) {
    ActivationCondition result = ActivationCondition::True(0);
    for (const auto& c : direct_conditions) result = ActivationCondition::And(result, c);
    for (const auto& c : children) result = ActivationCondition::And(result, c);
    return result;
  } else {
    ActivationCondition result = ActivationCondition::True(0);
    for (const auto& c : direct_conditions) {
      result = ActivationCondition::And(result, c);
    }
    if (!children.empty()) {
      ActivationCondition children_condition = children[0];
      for (size_t i = 1; i < children.size(); ++i) {
        children_condition = ActivationCondition::Or(children_condition, children[i]);
      }
      result = ActivationCondition::And(result, children_condition);
    }
    return result;
  }
}

class EntryGraphEquivalenceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string path = "ift/common/testdata/roboto_vf_seg_plan.txtpb";
    std::ifstream file(path);
    ASSERT_TRUE(file.is_open()) << "Could not open " << path;

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(content, &plan_));
  }

  SegmentationPlan plan_;
};

static VirtualMaps CreateVirtualMaps(const SegmentationPlan& plan) {
  std::set<hb_codepoint_t> all_cps;
  std::set<hb_tag_t> all_tags;
  for (const auto& [id, set] : plan.segments()) {
    for (hb_codepoint_t cp : set.codepoints().values()) {
      all_cps.insert(cp);
    }
    for (const auto& tag_str : set.features().values()) {
      all_tags.insert(hb_tag_from_string(tag_str.c_str(), -1));
    }
  }

  VirtualMaps maps;
  uint32_t next_virt = 0;
  for (hb_codepoint_t cp : all_cps) {
    maps.cp_to_virtual[cp] = next_virt++;
  }
  for (hb_tag_t tag : all_tags) {
    maps.feature_to_virtual[tag] = next_virt++;
  }
  return maps;
}

struct SegmentData {
  flat_hash_map<segment_index_t, SubsetDefinition> segments;
  flat_hash_map<segment_index_t, ActivationCondition> segment_to_virtual;
};

static SegmentData CreateSegments(const SegmentationPlan& plan, const VirtualMaps& maps) {
  SegmentData data;
  for (const auto& [id, set] : plan.segments()) {
    auto& segment = data.segments[id];
    SegmentSet virtual_indices;
    for (hb_codepoint_t cp : set.codepoints().values()) {
      segment.codepoints.insert(cp);
      virtual_indices.insert(maps.cp_to_virtual.at(cp));
    }
    for (const auto& tag_str : set.features().values()) {
      hb_tag_t tag = hb_tag_from_string(tag_str.c_str(), -1);
      segment.feature_tags.insert(tag);
      virtual_indices.insert(maps.feature_to_virtual.at(tag));
    }
    data.segment_to_virtual.emplace(id, ActivationCondition::or_segments(virtual_indices, 0));
  }
  return data;
}

TEST_F(EntryGraphEquivalenceTest, OptimizePreservesConditions) {
  VirtualMaps virtual_maps = CreateVirtualMaps(plan_);
  SegmentData segment_data = CreateSegments(plan_, virtual_maps);

  std::vector<ActivationCondition> conditions;
  flat_hash_map<std::vector<patch_id_t>, ActivationCondition> expected_virtual_conditions;
  for (const auto& c_proto : plan_.glyph_patch_conditions()) {
    ActivationCondition c = FromProto(c_proto);
    conditions.push_back(c);

    std::vector<patch_id_t> patches = {c.activated()};
    patches.insert(patches.end(), c.prefetches().begin(), c.prefetches().end());
    expected_virtual_conditions.emplace(std::move(patches), RealToVirtual(Normalize(c), segment_data.segment_to_virtual));
  }

  auto graph = EntryGraph::Create(conditions, segment_data.segments);
  ASSERT_TRUE(graph.ok()) << graph.status();

  auto unoptimized_entries = graph->ToPatchMapEntries(proto::GLYPH_KEYED);
  ASSERT_TRUE(unoptimized_entries.ok()) << unoptimized_entries.status();

  auto check_equivalence = [&](const std::vector<PatchMap::Entry>& entries) {
    for (const auto& entry : entries) {
      if (entry.ignored) {
        continue;
      }
      ActivationCondition resolved = ResolveVirtualCondition(
          entry, entries, virtual_maps);

      auto it = expected_virtual_conditions.find(entry.patch_indices);
      ASSERT_NE(it, expected_virtual_conditions.end())
          << "No expected condition for patches starting with " << entry.patch_indices[0];
      EXPECT_EQ(resolved, it->second)
          << "Condition mismatch for patches starting with " << entry.patch_indices[0]
          << "\nResolved: " << resolved.ToString()
          << "\nExpected: " << it->second.ToString();
    }
  };

  check_equivalence(*unoptimized_entries);

  ASSERT_TRUE(graph->Optimize().ok());
  auto optimized_entries = graph->ToPatchMapEntries(proto::GLYPH_KEYED);
  ASSERT_TRUE(optimized_entries.ok()) << optimized_entries.status();

  check_equivalence(*optimized_entries);

  // Check cost
  int64_t unoptimized_cost = TotalEncodingCost(*unoptimized_entries);
  int64_t optimized_cost = TotalEncodingCost(*optimized_entries);
  std::cout << "Unoptimized cost: " << unoptimized_cost << " bytes" << std::endl;
  std::cout << "Optimized cost:   " << optimized_cost << " bytes" << std::endl;
  std::cout << "Savings:          " << unoptimized_cost - optimized_cost << " bytes ("
            << (100.0 * (unoptimized_cost - optimized_cost) / unoptimized_cost) << "%)" << std::endl;

  EXPECT_LT(optimized_cost, unoptimized_cost);
}

}  // namespace ift::encoder
