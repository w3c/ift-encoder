#ifndef IFT_ENCODER_SEGMENT_H_
#define IFT_ENCODER_SEGMENT_H_

#include "ift/encoder/subset_definition.h"

namespace ift::encoder {

struct Segment {
  Segment(SubsetDefinition definition, double probability)
      : definition(std::move(definition)),
        probability(std::max(std::min(1.0, probability), 0.0)) {}

  double Probability() const { return probability; }
  const SubsetDefinition& Definition() const { return definition; }
  SubsetDefinition& Definition() { return definition; }

  void SetProbability(double probability) {
    this->probability = std::max(std::min(1.0, probability), 0.0);
  }

  void Clear() {
    definition.Clear();
    probability = 0.0;
  }

 private:
  SubsetDefinition definition;
  double probability;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_SEGMENT_H_