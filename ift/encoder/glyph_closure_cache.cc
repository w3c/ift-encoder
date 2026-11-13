#include "ift/encoder/glyph_closure_cache.h"

#include "ift/common/font_data.h"
#include "ift/common/font_helper.h"
#include "ift/common/hb_set_unique_ptr.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/dep_graph/unicode_edges.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"

using absl::Status;
using absl::StatusOr;
using ift::common::CodepointSet;
using ift::common::DataFileResolver;
using ift::common::FontHelper;
using ift::common::GlyphSet;
using ift::common::hb_face_unique_ptr;
using ift::common::hb_set_unique_ptr;
using ift::common::make_hb_face;
using ift::common::make_hb_set;
using ift::common::SegmentSet;
using ift::dep_graph::UnicodeEdges;

namespace ift::encoder {

StatusOr<std::unique_ptr<GlyphClosureCache>> GlyphClosureCache::Create(
    hb_face_t* face, const DataFileResolver& resolver) {
  auto preprocessed_face = make_hb_face(hb_subset_preprocess(face));
  auto unicode_edges = TRY(UnicodeEdges::ComputeUnicodeDependencyEdges(
      preprocessed_face.get(), resolver));
  return std::unique_ptr<GlyphClosureCache>(new GlyphClosureCache(
      face, std::move(preprocessed_face), std::move(unicode_edges)));
}

GlyphClosureCache::GlyphClosureCache(hb_face_t* original_face,
                                     hb_face_unique_ptr preprocessed_face,
                                     dep_graph::UnicodeEdges unicode_edges)
    : original_face_(make_hb_face(hb_face_reference(original_face))),
      preprocessed_face_(std::move(preprocessed_face)),
      unicode_edges_(std::move(unicode_edges)) {}

StatusOr<GlyphSet> GlyphClosureCache::SegmentClosure(
    const RequestedSegmentationInformation* segmentation_info,
    const SegmentSet& segments) {
  SubsetDefinition closure_def =
      segmentation_info->CombinedDefinition(segments);
  return GlyphClosure(closure_def);
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

StatusOr<bool> GlyphClosureCache::HasAdditionalConditions(
    const RequestedSegmentationInformation* segmentation_info,
    const SegmentSet& segments, const GlyphSet& glyphs) {
  SubsetDefinition combined;
  for (segment_index_t s : segments) {
    combined.Union(segmentation_info->Segments().at(s).Definition());
  }

  SubsetDefinition except =
      ComputeExceptSegment(*segmentation_info, segments, combined);
  GlyphSet closure_glyphs = TRY(GlyphClosure(except));
  return closure_glyphs.intersects(glyphs);
}

CodepointSet GlyphClosureCache::UnicodeClosure(
    const CodepointSet& unicodes) const {
  CodepointSet queue = unicodes;
  CodepointSet visited = unicodes;

  while (!queue.empty()) {
    hb_codepoint_t u = *queue.min();
    queue.erase(u);

    // outgoing decomp edges
    auto decomp = unicode_edges_.decomposition.find(u);
    if (decomp != unicode_edges_.decomposition.end()) {
      CodepointSet edges = decomp->second;
      edges.subtract(visited);
      queue.union_set(edges);
      visited.union_set(edges);
    }

    // outgoing comp edges
    auto comp = unicode_edges_.composition.find(u);
    if (comp != unicode_edges_.composition.end()) {
      for (const auto& edge : comp->second) {
        if (!visited.contains(edge.dest) &&
            visited.contains(edge.other_source)) {
          // edge requirements are satisfied, we can follow it.
          queue.insert(edge.dest);
          visited.insert(edge.dest);
        }
        // Note: if edge requirements are not satisfied, then this edge will be
        // rechecked later on if edge.other_source is visited, since edges for a
        // dest are always present from both sources.
      }
    }
  }

  return visited;
}

StatusOr<GlyphSet> GlyphClosureCache::GlyphClosure(
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

  SubsetDefinition expanded_segment = segment;
  expanded_segment.codepoints = UnicodeClosure(segment.codepoints);

  expanded_segment.ConfigureInput(input, preprocessed_face_.get());
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

CodepointSet GlyphClosureCache::CodepointsForGlyphs(
    const GlyphSet& glyphs) const {
  return unicode_edges_.CodepointsForGlyphs(glyphs);
}

StatusOr<SubsetDefinition> GlyphClosureCache::ExpandClosure(
    const SubsetDefinition& definition) {
  SubsetDefinition expanded = definition;
  bool changed = true;
  while (changed) {
    GlyphSet closure_gids = TRY(GlyphClosure(expanded));

    CodepointSet closure_unicodes = CodepointsForGlyphs(closure_gids);

    changed = false;
    if (!closure_gids.is_subset_of(expanded.gids)) {
      changed = true;
      expanded.gids.union_set(closure_gids);
    }

    if (!closure_unicodes.is_subset_of(expanded.codepoints)) {
      changed = true;
      expanded.codepoints.union_set(closure_unicodes);
    }
  }

  return expanded;
}

}  // namespace ift::encoder