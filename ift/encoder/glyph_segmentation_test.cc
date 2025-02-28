#include "ift/encoder/glyph_segmentation.h"

#include <optional>

#include "gtest/gtest.h"
#include "ift/encoder/condition.h"

using absl::btree_set;

namespace ift::encoder {

class GlyphSegmentationTest : public ::testing::Test {
 protected:
  GlyphSegmentationTest() {}
};

TEST_F(GlyphSegmentationTest, ActivationConditionsToEncoderConditions) {
  absl::flat_hash_map<segment_index_t, absl::flat_hash_set<hb_codepoint_t>>
      segments = {
          {1, {'a', 'b'}},
          {2, {'c'}},
          {3, {'d', 'e', 'f'}},
          {4, {'g'}},
      };

  std::vector<GlyphSegmentation::ActivationCondition> activation_conditions = {
      GlyphSegmentation::ActivationCondition::exclusive_segment(2, 2),
      GlyphSegmentation::ActivationCondition::exclusive_segment(3, 4),
      GlyphSegmentation::ActivationCondition::or_segments({1, 3}, 5),
      GlyphSegmentation::ActivationCondition::composite_condition(
          {{1, 3}, {2, 4}}, 6),
  };

  std::vector<Condition> expected;

  // entry[0] {{2}} -> 2,
  {
    Condition condition;
    condition.subset_definition.codepoints.insert('c');
    condition.activated_patch_id = 2;
    expected.push_back(condition);
  }

  // entry[1] {{3}} -> 4
  {
    Condition condition;
    condition.subset_definition.codepoints.insert('d');
    condition.subset_definition.codepoints.insert('e');
    condition.subset_definition.codepoints.insert('f');
    condition.activated_patch_id = 4;
    expected.push_back(condition);
  }

  // entry[2] {{1}} ignored
  {
    Condition condition;
    condition.subset_definition.codepoints.insert('a');
    condition.subset_definition.codepoints.insert('b');
    condition.activated_patch_id = std::nullopt;
    expected.push_back(condition);
  }

  // entry[3] {{4}} ignored
  {
    Condition condition;
    condition.subset_definition.codepoints.insert('g');
    condition.activated_patch_id = std::nullopt;
    expected.push_back(condition);
  }

  // entry[4] {{1 OR 3}} -> 5
  {
    Condition condition;
    condition.child_conditions = {2, 1};  // entry[1], entry[2]
    condition.activated_patch_id = 5;
    expected.push_back(condition);
  }

  // entry[5] {{2 OR 4}} ignored
  {
    Condition condition;
    condition.child_conditions = {0, 3};  // entry[0], entry[3]
    condition.activated_patch_id = std::nullopt;
    expected.push_back(condition);
  }

  // entry[6] {{1 OR 3} AND {2 OR 4}} -> 6
  {
    Condition condition;
    condition.child_conditions = {4, 5};  // entry[4], entry[5]
    condition.activated_patch_id = 6;
    condition.conjunctive = true;
    expected.push_back(condition);
  }

  auto entries = GlyphSegmentation::ActivationConditionsToConditionEntries(
      activation_conditions, segments);
  ASSERT_TRUE(entries.ok()) << entries.status();
  ASSERT_EQ(*entries, expected);
}

// TODO(garretrieger): add test where or_set glyphs are moved back to unmapped
// due to found "additional conditions".

}  // namespace ift::encoder