#ifndef IFT_ENCODER_COMPLEX_CONDITION_FINDER_H_
#define IFT_ENCODER_COMPLEX_CONDITION_FINDER_H_

#include "absl/status/statusor.h"
#include "common/int_set.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/glyph_condition_set.h"
#include "ift/encoder/requested_segmentation_information.h"

namespace ift::encoder {

// Finds superset purely disjunctive conditions that activate each
// provided glyph. Returns a map from each condition to the activated
// glyphs.
//
// Takes a glyph condition set which will be used as a starting point.
//
// A superset purely disjunctive condition  will activate at least
// whenever the true condition would. It will only ever include segments
// that appear in the true condition. There are typically multiple
// possible superset conditions. This will find one of them.
//
// For example if a glyph has the true condition (a and b) or (b and c)
// this could find the condition (a or c).
absl::StatusOr<absl::btree_map<common::SegmentSet, common::GlyphSet>>
FindSupersetDisjunctiveConditionsFor(
    const RequestedSegmentationInformation& segmentation_info,
    const GlyphConditionSet& glyph_condition_set,
    GlyphClosureCache& closure_cache, common::GlyphSet glyphs);

}  // namespace ift::encoder

#endif  // IFT_ENCODER_COMPLEX_CONDITION_FINDER_H_