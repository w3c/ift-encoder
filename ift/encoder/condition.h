#ifndef IFT_ENCODER_CONDITION_H_
#define IFT_ENCODER_CONDITION_H_

#include <cstdint>
#include <optional>

#include "ift/encoder/subset_definition.h"

namespace ift::encoder {

/*
 * This conditions is satisfied if the input subset definition matches at
 * least one segment in each required group and every feature in
 * required_features.
 */
struct Condition {
  SubsetDefinition subset_definition;
  absl::btree_set<uint32_t> child_conditions;
  bool conjunctive = false;
  std::optional<uint32_t> activated_patch_id = std::nullopt;

  Condition() : subset_definition(), child_conditions() {}

  static Condition SimpleCondition(SubsetDefinition subset_definition,
                                   uint32_t patch_id) {
    Condition condition;
    condition.subset_definition = subset_definition;
    condition.activated_patch_id = patch_id;
    return condition;
  }

  friend void PrintTo(const Condition& condition, std::ostream* os);
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_CONDITION_H_