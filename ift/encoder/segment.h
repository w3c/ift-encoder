#ifndef IFT_ENCODER_SEGMENT_H_
#define IFT_ENCODER_SEGMENT_H_

#include "ift/encoder/subset_definition.h"
#include "ift/freq/probability_bound.h"

namespace ift::encoder {

struct Segment {
  Segment(SubsetDefinition definition, freq::ProbabilityBound probability)
      : definition(std::move(definition)), probability(probability) {}

  double Probability() const { return probability.Average(); }
  const freq::ProbabilityBound& ProbabilityBound() const { return probability; }

  const SubsetDefinition& Definition() const { return definition; }
  SubsetDefinition& Definition() { return definition; }

  bool MeetsMinimumGroupSize(uint32_t min_group_size) const {
    if (!Definition().feature_tags.empty() ||
        !Definition().design_space.empty()) {
      // TODO(garretrieger): this computation should also include feature tags
      // and design space
      //                     into the min group size calculation.
      return true;
    }

    return Definition().codepoints.size() >= min_group_size;
  }

  void SetProbability(freq::ProbabilityBound probability) {
    this->probability = probability;
  }

  void Clear() {
    definition.Clear();
    probability = freq::ProbabilityBound::Zero();
  }

 private:
  SubsetDefinition definition;
  freq::ProbabilityBound probability;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_SEGMENT_H_