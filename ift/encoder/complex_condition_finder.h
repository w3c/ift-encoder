#ifndef IFT_ENCODER_COMPLEX_CONDITION_FINDER_H_
#define IFT_ENCODER_COMPLEX_CONDITION_FINDER_H_

#include "absl/status/statusor.h"
#include "common/int_set.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/glyph_condition_set.h"
#include "ift/encoder/requested_segmentation_information.h"

namespace ift::encoder {

// Finds the minimal purely disjunctive conditions that activate each
// provided glyph. Returns a map from each condition to the activated
// glyphs.
//
// Takes a glyph condition set which will be used as a starting point.
//
// A minimal purely disjunctive condition is the complete set of segments
// which appear in the true activation condition for a glyph. The
// purely disjunctive version is a super set of the true condition and
// will activate at least whenever the true condition would.
//
// For example if a glyph has the true condition (a and b) or (b and c)
// this will find the condition (a or b or c).
absl::StatusOr<absl::btree_map<common::SegmentSet, common::GlyphSet>>
FindMinimalDisjunctiveConditionsFor(
    const RequestedSegmentationInformation& segmentation_info,
    const GlyphConditionSet& glyph_condition_set,
    GlyphClosureCache& closure_cache, common::GlyphSet glyphs);

}  // namespace ift::encoder

#endif  // IFT_ENCODER_COMPLEX_CONDITION_FINDER_H_