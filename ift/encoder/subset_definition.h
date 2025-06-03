#ifndef IFT_ENCODER_SUBSET_DEFINITION_H_
#define IFT_ENCODER_SUBSET_DEFINITION_H_

#include <cstdint>
#include <initializer_list>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "common/axis_range.h"
#include "common/int_set.h"
#include "hb-subset.h"
#include "ift/proto/patch_map.h"

namespace ift::encoder {

typedef absl::flat_hash_map<hb_tag_t, common::AxisRange> design_space_t;

// Describes a subset of a font in terms of the codepoints, feature tags, and
// design space that the subset will contain data for.
//
// Mirrors: https://w3c.github.io/IFT/Overview.html#font-subset-definition
//
// When used to describe the condition to activate a patch/subset the target
// subset definition will intersect the condition when
//
// intersects(target.codepoints, condition.codepoints) OR
// intersects(target.features, condition.features) OR
// intersects(target.design_space, condition.design_space)
//
// Each of the sets (codepoints, features, design space) are optional, if empty
// they do not influence intersection or configuration of a subsetting
// operation.
struct SubsetDefinition {
  SubsetDefinition() {}
  SubsetDefinition(std::initializer_list<uint32_t> codepoints_in) {
    for (uint32_t cp : codepoints_in) {
      codepoints.insert(cp);
    }
  }

  static SubsetDefinition Codepoints(const common::CodepointSet& codepoints) {
    SubsetDefinition def;
    def.codepoints = codepoints;
    return def;
  }

  friend void PrintTo(const SubsetDefinition& point, std::ostream* os);

  common::CodepointSet codepoints;
  common::GlyphSet gids;
  absl::btree_set<hb_tag_t> feature_tags;
  design_space_t design_space;

  bool IsVariable() const {
    for (const auto& [tag, range] : design_space) {
      if (range.IsRange()) {
        return true;
      }
    }
    return false;
  }

  bool Empty() const {
    return codepoints.empty() && gids.empty() && feature_tags.empty() &&
           design_space.empty();
  }

  void Clear() {
    codepoints.clear();
    gids.clear();
    feature_tags.clear();
    design_space.clear();
  }

  bool operator==(const SubsetDefinition& other) const {
    return codepoints == other.codepoints && gids == other.gids &&
           feature_tags == other.feature_tags &&
           design_space == other.design_space;
  }

  template <typename H>
  friend H AbslHashValue(H h, const SubsetDefinition& s) {
    return H::combine(std::move(h), s.codepoints, s.gids, s.feature_tags,
                      s.design_space);
  }

  void Union(const SubsetDefinition& other);

  void Subtract(const SubsetDefinition& other);

  void ConfigureInput(hb_subset_input_t* input, hb_face_t* face) const;

  // TODO XXXXX in some cases a composite entry (using child indices) will be
  // needed.
  //            we can't always convert to just a single coverage object.
  ift::proto::PatchMap::Coverage ToCoverage() const;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_SUBSET_DEFINITION_H_