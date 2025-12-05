#ifndef IFT_ENCODER_COMPLEX_CONDITION_FINDER_H_
#define IFT_ENCODER_COMPLEX_CONDITION_FINDER_H_

#include "absl/status/statusor.h"
#include "common/int_set.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/glyph_condition_set.h"
#include "ift/encoder/requested_segmentation_information.h"

namespace ift::encoder {

absl::StatusOr<absl::btree_map<common::SegmentSet, common::GlyphSet>>
FindComplexConditionsFor(
    const RequestedSegmentationInformation& segmentation_info,
    const GlyphConditionSet& glyph_condition_set,
    GlyphClosureCache& closure_cache, common::GlyphSet glyphs);

}  // namespace ift::encoder

#endif  // IFT_ENCODER_COMPLEX_CONDITION_FINDER_H_