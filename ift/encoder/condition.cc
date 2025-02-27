#include "ift/encoder/condition.h"

namespace ift::encoder {

void PrintTo(const Condition& c, std::ostream* os) {
  *os << "{ subset_def: ";
  PrintTo(c.subset_definition, os);

  if (!c.child_conditions.empty()) {
    *os << ", children: {";
    for (auto index : c.child_conditions) {
      *os << "c" << index << ", ";
    }
    *os << "}, ";
    *os << (c.conjunctive ? "conjunctive" : "disjunctive");
  }
  if (c.activated_patch_id.has_value()) {
    *os << " } => p" << *c.activated_patch_id;
  } else {
    *os << " }";
  }
}

}  // namespace ift::encoder