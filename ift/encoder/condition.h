#ifndef IFT_ENCODER_CONDITION_H_
#define IFT_ENCODER_CONDITION_H_

#include <cstdint>
#include <optional>

#include "common/int_set.h"
#include "ift/encoder/subset_definition.h"

namespace ift::encoder {

/*
 * This conditions is satisfied if the input subset definition
 * matches the conditions subset_definition and all child conditions
 * are matched.
 *
 * Child conditions refer to the indices of previous condition entries
 * See: https://w3c.github.io/IFT/Overview.html#mapping-entry-childentryindices
 */
struct Condition {
  SubsetDefinition subset_definition;
  common::IntSet child_conditions;
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

  bool operator==(const Condition& other) const {
    return subset_definition == other.subset_definition &&
           child_conditions == other.child_conditions &&
           conjunctive == other.conjunctive &&
           activated_patch_id == other.activated_patch_id;
  }
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_CONDITION_H_