#ifndef IFT_ENCODER_SEGMENT_H_
#define IFT_ENCODER_SEGMENT_H_

#include "ift/encoder/subset_definition.h"

namespace ift::encoder {

// TODO XXXX remove Segment? Use just SubsetDefinition?
struct Segment {
  Segment(SubsetDefinition definition)
      : definition(std::move(definition)) {}

  const SubsetDefinition& Definition() const { return definition; }
  SubsetDefinition& Definition() { return definition; }

  bool MeetsMinimumGroupSize(uint32_t min_group_size) const {
    if (!Definition().feature_tags.empty() ||
        !Definition().design_space.empty()) {
      // TODO(garretrieger): this computation should also include feature tags
      //  and design space into the min group size calculation.
      return true;
    }

    return Definition().codepoints.size() >= min_group_size;
  }

  void Clear() {
    definition.Clear();
  }

 private:
  SubsetDefinition definition;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_SEGMENT_H_