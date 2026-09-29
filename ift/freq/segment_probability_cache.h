#ifndef IFT_FREQ_SEGMENT_PROBABILITY_CACHE_H_
#define IFT_FREQ_SEGMENT_PROBABILITY_CACHE_H_

#include <optional>
#include <vector>

#include "ift/common/int_set.h"
#include "ift/encoder/types.h"
#include "ift/freq/probability_bound.h"

namespace ift::freq {

// Caches per-segment probability entries indexed by segment_index_t.
// Pre-sized via Reset(num_segments); out-of-bounds accesses fail.
template <typename Entry = ProbabilityBound>
class SegmentProbabilityCache {
 public:
  SegmentProbabilityCache() = default;

  std::optional<Entry>& operator[](
      ift::encoder::segment_index_t segment_index) {
    return entries_.at(segment_index);
  }

  void Invalidate(const ift::common::SegmentSet& segments) {
    for (ift::encoder::segment_index_t s : segments) {
      entries_.at(s) = std::nullopt;
    }
  }

  void Reset(size_t num_segments) {
    entries_.assign(num_segments, std::nullopt);
  }

 private:
  std::vector<std::optional<Entry>> entries_;
};

}  // namespace ift::freq

#endif  // IFT_FREQ_SEGMENT_PROBABILITY_CACHE_H_
