#include "absl/log/log.h"

#include "ift/encoder/glyph_condition_set.h"
#include "ift/encoder/types.h"

namespace ift::encoder {

void PrintTo(const GlyphConditionSet& set, std::ostream* os) {
  *os << "Glyph Condition Set {" << std::endl;
  glyph_id_t gid = 0;
  for (const auto& c : set.gid_conditions_) {
    if (!c.and_segments.empty() || !c.or_segments.empty()) {
      *os << "  g" << gid << ": ";
      *os << "OR " << c.or_segments.ToString();
      *os << ", AND " << c.and_segments.ToString();
      *os << std::endl;
    }
    gid++;
  }
  *os << "}" << std::endl;
}

static void PrintCondition(
  glyph_id_t gid, const GlyphConditions& condition, bool added) {
  VLOG(0) << (added ? "++ " : "-- ") << "g" << gid
    << ": OR " << condition.or_segments.ToString() << ", "
    << ": AND " << condition.or_segments.ToString();
}

void GlyphConditionSet::PrintDiff(const GlyphConditionSet& a, const GlyphConditionSet& b) {
  auto it_a = a.gid_conditions_.begin();
  auto it_b = b.gid_conditions_.begin();
  glyph_id_t gid_a = 0;
  glyph_id_t gid_b = 0;

  while (it_a != a.gid_conditions_.end() || it_b != b.gid_conditions_.end()) {
    if (it_a == a.gid_conditions_.end()) {
      PrintCondition(gid_b, *it_b, true);
      it_b++;
      gid_b++;
    } else if (it_b == b.gid_conditions_.end()) {
      PrintCondition(gid_a, *it_a, false);
      it_a++;
      gid_a++;
    } else if (gid_a == gid_b) {
      if (*it_a != *it_b) {
        PrintCondition(gid_a, *it_a, false);
        PrintCondition(gid_b, *it_b, true);
      }
      it_a++;
      it_b++;
      gid_a++;
      gid_b++;
    } else if (gid_a < gid_b) {
      PrintCondition(gid_a, *it_a, false);
      it_a++;
      gid_a++;
    } else {
      PrintCondition(gid_b, *it_b, true);
      it_b++;
      gid_b++;
    }
  }
}

}  // namespace ift::encoder