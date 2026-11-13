#include "ift/encoder/glyph_closure_cache.h"

#include "hb-subset.h"
#include "hb.h"
#include "ift/common/font_data.h"
#include "ift/common/font_helper.h"
#include "ift/common/hb_set_unique_ptr.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/dep_graph/unicode_edges.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"

using ift::config::Glyphs;

using absl::Status;
using absl::StatusOr;
using ift::common::CodepointSet;
using ift::common::DataFileResolver;
using ift::common::FontHelper;
using ift::common::GlyphSet;
using ift::common::hb_blob_unique_ptr;
using ift::common::hb_face_unique_ptr;
using ift::common::hb_set_unique_ptr;
using ift::common::make_hb_blob;
using ift::common::make_hb_face;
using ift::common::make_hb_face_builder;
using ift::common::make_hb_set;
using ift::common::SegmentSet;
using ift::dep_graph::UnicodeEdges;

namespace ift::encoder {

namespace {

hb_face_unique_ptr RemoveCmapTable(hb_face_t* face) {
  hb_face_unique_ptr builder = make_hb_face_builder();
  for (hb_tag_t tag : FontHelper::GetTags(face)) {
    if (tag == FontHelper::kCmap) {
      continue;
    }
    hb_blob_unique_ptr table_blob =
        make_hb_blob(hb_face_reference_table(face, tag));
    hb_face_builder_add_table(builder.get(), tag, table_blob.get());
  }
  return builder;
}

}  // namespace

StatusOr<std::unique_ptr<GlyphClosureCache>> GlyphClosureCache::Create(
    hb_face_t* face, const DataFileResolver& resolver) {
  auto preprocessed_with_cmap = make_hb_face(hb_subset_preprocess(face));
  auto unicode_edges = TRY(UnicodeEdges::ComputeUnicodeDependencyEdges(
      preprocessed_with_cmap.get(), resolver));
  auto without_cmap = RemoveCmapTable(preprocessed_with_cmap.get());
  auto preprocessed_face =
      make_hb_face(hb_subset_preprocess(without_cmap.get()));
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

CodepointSet GlyphClosureCache::UnicodeClosure(const CodepointSet& unicodes,
                                               GlyphSet& uvs_glyphs) const {
  // Follow DependencyGraph's ordered unicode closure phases:
  // 1. Init + Bidi mirroring
  CodepointSet closed = unicodes;
  hb_unicode_funcs_t* unicode_funcs = hb_unicode_funcs_get_default();
  for (hb_codepoint_t u : unicodes) {
    hb_codepoint_t mirror = hb_unicode_mirroring(unicode_funcs, u);
    if (mirror != u && unicode_edges_.unicode_to_gid.contains(mirror)) {
      closed.insert(mirror);
    }
  }

  // 2. Variation selectors
  CodepointSet active_vs = closed;
  active_vs.intersect(unicode_edges_.vs_unicodes);
  for (hb_codepoint_t vs : active_vs) {
    auto it = unicode_edges_.variation_selector.find(vs);
    if (it == unicode_edges_.variation_selector.end()) {
      continue;
    }
    for (const auto& edge : it->second) {
      if (closed.contains(edge.unicode)) {
        uvs_glyphs.insert(edge.gid);
      }
    }
  }

  // 3. Unicode canonical decomposition
  CodepointSet decomp_queue = closed;
  while (!decomp_queue.empty()) {
    hb_codepoint_t u = *decomp_queue.min();
    decomp_queue.erase(u);

    auto decomp = unicode_edges_.decomposition.find(u);
    if (decomp != unicode_edges_.decomposition.end()) {
      CodepointSet edges = decomp->second;
      edges.subtract(closed);
      decomp_queue.union_set(edges);
      closed.union_set(edges);
    }
  }

  // 4. Unicode canonical composition
  CodepointSet comp_queue = closed;
  while (!comp_queue.empty()) {
    hb_codepoint_t u = *comp_queue.min();
    comp_queue.erase(u);

    auto comp = unicode_edges_.composition.find(u);
    if (comp != unicode_edges_.composition.end()) {
      for (const auto& edge : comp->second) {
        if (!closed.contains(edge.dest) &&
            closed.contains(edge.other_source)) {
          // edge requirements are satisfied, we can follow it.
          comp_queue.insert(edge.dest);
          closed.insert(edge.dest);
        }
        // Note: if edge requirements are not satisfied, then this edge will be
        // rechecked later on if edge.other_source is visited, since edges for a
        // dest are always present from both sources.
      }
    }
  }

  return closed;
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

  // Harfbuzz subsetter currently has only limited support for doing
  // unicode closure (only bidi is supported), so we need to manually
  // do the closure ourselves. In order to avoid subsetter from
  // incorrectly mapping uvs glyphs back to nominal codepoints we
  // manually do the cmap unicode -> glyph mapping and disable cmap
  // in the harfbuzz subsetting operation.
  //
  // This "hack" can be removed later on if harfbuzz gains full support
  // for unicode closure over unicode composition and decomposition.
  GlyphSet uvs_glyphs;
  CodepointSet closed_unicodes = UnicodeClosure(segment.codepoints, uvs_glyphs);

  GlyphSet initial_gids = std::move(uvs_glyphs);
  for (hb_codepoint_t u : closed_unicodes) {
    auto cmap_it = unicode_edges_.unicode_to_gid.find(u);
    if (cmap_it != unicode_edges_.unicode_to_gid.end()) {
      initial_gids.insert(cmap_it->second);
    }
  }

  SubsetDefinition expanded_segment = segment;
  expanded_segment.codepoints.clear();
  expanded_segment.gids.union_set(initial_gids);

  hb_subset_input_set_flags(
      // BIDI closure is disabled since we manually performed it already.
      input, hb_subset_input_get_flags(input) | HB_SUBSET_FLAGS_NO_BIDI_CLOSURE);
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