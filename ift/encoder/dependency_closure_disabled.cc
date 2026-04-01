#include "ift/common/int_set.h"
#include "ift/encoder/dependency_closure.h"

using absl::Status;
using absl::StatusOr;
using ift::common::GlyphSet;
using ift::common::IntSet;
using ift::common::SegmentSet;

namespace ift::encoder {

Status DependencyClosure::SegmentsChanged(bool init_font_change,
                                          const SegmentSet& segments) {
  return absl::UnimplementedError(
      "Depdency graph functionality was disabled during compilation and is "
      "unvailable");
}

StatusOr<DependencyClosure::AnalysisAccuracy> DependencyClosure::AnalyzeSegment(
    const SegmentSet& segments, GlyphSet& and_gids, GlyphSet& or_gids,
    GlyphSet& exclusive_gids) {
  return absl::UnimplementedError(
      "Depdency graph functionality was disabled during compilation and is "
      "unvailable");
}

StatusOr<SegmentSet> DependencyClosure::SegmentsThatInteractWith(
    const GlyphSet& glyphs) const {
  return absl::UnimplementedError(
      "Depdency graph functionality was disabled during compilation and is "
      "unvailable");
}

StatusOr<SegmentSet> DependencyClosure::SegmentsThatInteractWith(
      const SubsetDefinition& def) const {
  return absl::UnimplementedError(
      "Depdency graph functionality was disabled during compilation and is "
      "unvailable");
}

}  // namespace ift::encoder