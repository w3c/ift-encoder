#ifndef IFT_FREQ_MOCK_PROBABILITY_CALCULATOR_H_
#define IFT_FREQ_MOCK_PROBABILITY_CALCULATOR_H_

#include <vector>

#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/probability_calculator.h"

namespace ift::freq {

class MockProbabilityCalculator : public ProbabilityCalculator {
 public:
  MockProbabilityCalculator(std::vector<std::pair<ift::encoder::Segment, double>> segments)
      : segments_(segments) {}

  absl::string_view Name() const override {
    return "MockProbabilityCalculator";
  }

  ProbabilityBound ComputeProbability(uint32_t codepoint) const override {
    return ComputeProbability(ift::encoder::SubsetDefinition {codepoint});
  }

  ProbabilityBound ComputeProbability(
      const ift::encoder::SubsetDefinition& definition) const {
    for (const auto& [segment, prob] : segments_) {
      if (segment.Definition() == definition) {
        return {prob, prob};
      }
    }
    return {0.0, 0.0};
  }

  ProbabilityBound ComputeProbability(
      absl::Span<const ift::encoder::Segment> segments,
      ift::encoder::segment_index_t segment_index) const override {
    return ComputeProbability(segments.at(segment_index).Definition());
  }

  ProbabilityBound ComputeMergedProbability(
       absl::Span<const ift::encoder::Segment> segments,
      const ift::common::SegmentSet& segment_indices) const override {
    std::vector<const ift::encoder::Segment*> segment_ptrs;
    segment_ptrs.reserve(segment_indices.size());
    for (ift::encoder::segment_index_t s : segment_indices) {
      segment_ptrs.push_back(&segments[s]);
    }
    return ComputeMergedProbability(segment_ptrs);
  }

  ProbabilityBound ComputeMergedProbability(
      const std::vector<const ift::encoder::Segment*>& segments)
      const {
    ift::encoder::SubsetDefinition merged;
    for (const auto* s : segments) {
      merged.Union(s->Definition());
    }
    return ComputeProbability(merged);
  }

  ProbabilityBound ComputeConjunctiveProbability(
      const std::vector<ProbabilityBound>& bounds) const override {
    double probability = 1.0;
    for (const auto& bound : bounds) {
      probability *= bound.Value();
    }
    return {probability, probability};
  }

 private:
  std::vector<std::pair<ift::encoder::Segment, double>> segments_;
};

}  // namespace ift::freq

#endif  // IFT_FREQ_MOCK_PROBABILITY_CALCULATOR_H_