#include "ift/encoder/glyph_closure_cache.h"

#include "common/hb_set_unique_ptr.h"
#include "common/int_set.h"
#include "common/try.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"

using absl::Status;
using absl::StatusOr;
using common::GlyphSet;
using common::hb_set_unique_ptr;
using common::make_hb_set;
using common::SegmentSet;

namespace ift::encoder {

StatusOr<common::GlyphSet> GlyphClosureCache::GlyphClosure(
    const SubsetDefinition& segment) {
  auto it = glyph_closure_cache_.find(segment);
  if (it != glyph_closure_cache_.end()) {
    glyph_closure_cache_hit_++;
    return it->second;
  }

  glyph_closure_cache_miss_++;

  hb_subset_input_t* input = hb_subset_input_create_or_fail();
  if (!input) {
    return absl::InternalError("Closure subset configuration failed.");
  }

  segment.ConfigureInput(input, preprocessed_face_.get());

  hb_subset_plan_t* plan =
      hb_subset_plan_create_or_fail(preprocessed_face_.get(), input);
  hb_subset_input_destroy(input);
  if (!plan) {
    return absl::InternalError("Closure calculation failed.");
  }

  hb_map_t* new_to_old = hb_subset_plan_new_to_old_glyph_mapping(plan);
  hb_set_unique_ptr gids = make_hb_set();
  hb_map_values(new_to_old, gids.get());
  hb_subset_plan_destroy(plan);

  glyph_closure_cache_.insert(std::pair(segment, GlyphSet(gids)));

  return GlyphSet(gids);
}

StatusOr<GlyphSet> GlyphClosureCache::CodepointsToOrGids(
    const RequestedSegmentationInformation& segmentation_info,
    const SegmentSet& segment_ids) {
  GlyphSet and_gids;
  GlyphSet or_gids;
  GlyphSet exclusive_gids;
  TRYV(AnalyzeSegment(segmentation_info, segment_ids, and_gids, or_gids,
                      exclusive_gids));

  return or_gids;
}

// This generates the subset definition that contains all segments except for
// those listed in segment_ids.
SubsetDefinition ComputeExceptSegment(
    const RequestedSegmentationInformation& segmentation_info,
    const SegmentSet& segment_ids, const SubsetDefinition& combined) {
  if (segmentation_info.SegmentsAreDisjoint() &&
      (segment_ids.size() == 1 ||
       segment_ids.size() < (segmentation_info.Segments().size() / 2))) {
    // Approach that is optimized for the case where input segments are disjoint
    // and the number of segment ids is smallish.
    SubsetDefinition except_segment = segmentation_info.FullDefinition();
    except_segment.Subtract(combined);
    return except_segment;
  }

  // Otherwise this approach will always work even with non-disjoint segments
  SegmentSet except_segment_ids = segment_ids;
  except_segment_ids.invert();

  uint32_t num_segments = segmentation_info.Segments().size();
  SubsetDefinition except_segment = segmentation_info.InitFontSegment();
  for (segment_index_t s : except_segment_ids) {
    if (s >= num_segments) {
      break;
    }
    except_segment.Union(segmentation_info.Segments()[s].Definition());
  }

  return except_segment;
}

Status GlyphClosureCache::AnalyzeSegment(
    const RequestedSegmentationInformation& segmentation_info,
    const SegmentSet& segment_ids, GlyphSet& and_gids, GlyphSet& or_gids,
    GlyphSet& exclusive_gids) {
  if (segment_ids.empty()) {
    return absl::OkStatus();
  }

  // This function tests various closures using the segment codepoints to
  // determine what conditions are present for the inclusion of closure glyphs.
  //
  // At a high level we do the following (where s_i is the segment being
  // tested):
  //
  // * Set A: glyph closure on original font of the union of all segments.
  // * Set B: glyph closure on original font of the union of all segments except
  //          for s_i
  // * Set I: (glyph closure on original font of s_0 union s_i) - (glyph closure
  //           on original font of s_0)
  // * Set D: A - B, the set of glyphs that are dropped when s_i is removed.
  //
  // Then we know the following:
  // * Glyphs in I should be included whenever s_i is activated.
  // * s_i is necessary for glyphs in D to be required, but other segments may
  //   be needed too.
  //
  // Furthermore we can intersect I and D to produce three sets:
  // * D - I: the activation condition for these glyphs is s_i AND …
  //          Where … is one or more additional segments.
  // * I - D: the activation conditions for these glyphs is s_i OR …
  //          Where … is one or more additional segments.
  // * D intersection I: the activation conditions for these glyphs is only s_i

  SubsetDefinition
      combined;  // This is the subset definition of the unions of segment_ids.
  for (segment_index_t s_id : segment_ids) {
    combined.Union(segmentation_info.Segments()[s_id].Definition());
  }

  SubsetDefinition except_segment =
      ComputeExceptSegment(segmentation_info, segment_ids, combined);
  auto B_except_segment_closure = TRY(GlyphClosure(except_segment));

  SubsetDefinition only_segment = combined;
  only_segment.Union(segmentation_info.InitFontSegment());

  auto I_only_segment_closure = TRY(GlyphClosure(only_segment));
  I_only_segment_closure.subtract(segmentation_info.InitFontGlyphs());

  GlyphSet D_dropped = segmentation_info.FullClosure();
  D_dropped.subtract(B_except_segment_closure);

  and_gids.union_set(D_dropped);
  and_gids.subtract(I_only_segment_closure);

  or_gids.union_set(I_only_segment_closure);
  or_gids.subtract(D_dropped);

  exclusive_gids.union_set(I_only_segment_closure);
  exclusive_gids.intersect(D_dropped);

  return absl::OkStatus();
}

}  // namespace ift::encoder