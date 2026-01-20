#include "ift/encoder/dependency_closure.h"

#include "absl/log/log.h"
#include "common/font_helper.h"
#include "common/int_set.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/types.h"

using absl::flat_hash_map;
using absl::StatusOr;
using common::CodepointSet;
using common::GlyphSet;
using common::IntSet;
using common::FontHelper;

namespace ift::encoder {

bool DependencyClosure::FollowEdge(
  hb_tag_t table_tag,
  glyph_id_t from_gid,
  glyph_id_t to_gid,
  hb_tag_t feature_tag) const {

  const GlyphSet& closure_glyphs = segmentation_info_->NonInitFontGlyphs();
  bool r = closure_glyphs.contains(to_gid) &&
           closure_glyphs.contains(from_gid) &&
         (feature_tag == 0 ||
          full_feature_set_.contains(feature_tag));

  if (r) {
    VLOG(1) << "  following edge " << from_gid << " -> " << to_gid
            << " (" << FontHelper::ToString(table_tag) << ", " << FontHelper::ToString(feature_tag) << ")";
  } else {
    VLOG(2) << "  ignoring edge " << from_gid << " -> " << to_gid
            << " (" << FontHelper::ToString(table_tag) << ", " << FontHelper::ToString(feature_tag) << ")";
  }

  return r;
}

static bool DisallowedTableTag(hb_tag_t tag) {
  // TODO(garretrieger): implement handling for these tables to allow them to be removed
  //                     from the disallowed list.
  // - cmap: needs special handling for UVS
  // - gsub: needs special handling based on the lookup type (support the "simple" GSUB types).
  // - CFF/CFF2: investigate if seac components need any special handling.
  return tag == HB_TAG('G', 'S', 'U', 'B') ||
         tag == HB_TAG('c', 'm', 'a', 'p') ||
         tag == HB_TAG('C', 'F', 'F', ' ') || tag == HB_TAG('C', 'F', 'F', '2');
}

bool DependencyClosure::TraverseGraph(
    const common::GlyphSet& glyphs,
    absl::flat_hash_map<glyph_id_t, unsigned>& traversed_edges) const {

  VLOG(1) << "DependencyClosure::TraverseGraph(" << glyphs.ToString() << ")";
  std::vector<glyph_id_t> next;
  for (glyph_id_t gid : glyphs) {
    next.push_back(gid);
    // Ensure an edge count entry exists.
    traversed_edges.insert(std::pair(gid, 0));
  }

  GlyphSet visited;
  while (!next.empty()) {
    glyph_id_t gid = next.back();
    next.pop_back();

    if (visited.contains(gid)) {
      continue;
    }
    visited.insert(gid);

    hb_codepoint_t index = 0;
    hb_tag_t table_tag = HB_CODEPOINT_INVALID;
    hb_codepoint_t dep_gid = HB_CODEPOINT_INVALID;
    hb_tag_t layout_tag = HB_CODEPOINT_INVALID;
    hb_codepoint_t ligature_set = HB_CODEPOINT_INVALID;
    while (hb_depend_get_glyph_entry(dependency_graph_.get(), gid, index++, &table_tag,
                                     &dep_gid, &layout_tag, &ligature_set)) {
      if (!FollowEdge(table_tag, gid, dep_gid, layout_tag)) {
        continue;
      }

      if (DisallowedTableTag(table_tag)) {
        return false;
      }

      traversed_edges[dep_gid]++;
      next.push_back(dep_gid);
    }
  }

  return true;
}

flat_hash_map<hb_codepoint_t, glyph_id_t>
DependencyClosure::ComputeIncomingEdgeCount() const {
  VLOG(1) << "DependencyClosure::ComputeIncomingEdgeCount()";
  unsigned glyph_count = hb_face_get_glyph_count(original_face_.get());
  flat_hash_map<glyph_id_t, unsigned> incoming_edge_count;
  for (unsigned gid = 0; gid < glyph_count; gid++) {
    hb_codepoint_t index = 0;
    hb_tag_t table_tag = HB_CODEPOINT_INVALID;
    hb_codepoint_t dep_gid = HB_CODEPOINT_INVALID;
    hb_tag_t layout_tag = HB_CODEPOINT_INVALID;
    hb_codepoint_t ligature_set = HB_CODEPOINT_INVALID;
    incoming_edge_count.insert(std::make_pair(gid, 0)); // ensure each gid has an entry

    while (hb_depend_get_glyph_entry(dependency_graph_.get(), gid, index++, &table_tag,
                                     &dep_gid, &layout_tag, &ligature_set)) {
      if (!FollowEdge(table_tag, gid, dep_gid, layout_tag)) {
        continue;
      }
      incoming_edge_count[dep_gid]++;
    }
  }

  // For our use case, the unicode -> gid mappings also contribute an incoming edge to the
  // counts, so collect those up.
  const CodepointSet& full_def = segmentation_info_->FullDefinition().codepoints;
  const GlyphSet& closure_glyphs = segmentation_info_->NonInitFontGlyphs();
  for (const auto [u, g] : unicode_to_gid_) {
    if (full_def.contains(u) && closure_glyphs.contains(g)) {
      incoming_edge_count[g]++;
    }
  }

  return incoming_edge_count;
}

StatusOr<IntSet> DependencyClosure::FullFeatureSet(
    const RequestedSegmentationInformation* segmentation_info,
    hb_face_t* face) {
  hb_subset_input_t* input = hb_subset_input_create_or_fail();
  if (!input) {
    return absl::InternalError("Failed to create subset input object.");
  }

  // By extracting the feature list of the harfbuzz subset input we will also
  // include the features that harfbuzz adds by default.
  segmentation_info->FullDefinition().ConfigureInput(input, face);
  IntSet features(
      hb_subset_input_set(input, HB_SUBSET_SETS_LAYOUT_FEATURE_TAG));
  hb_subset_input_destroy(input);

  return features;
}

flat_hash_map<hb_codepoint_t, glyph_id_t> DependencyClosure::UnicodeToGid(
    hb_face_t* face) {
  flat_hash_map<hb_codepoint_t, glyph_id_t> out;
  hb_map_t* unicode_to_gid = hb_map_create();
  hb_face_collect_nominal_glyph_mapping(face, unicode_to_gid, nullptr);
  int index = -1;
  uint32_t cp = HB_MAP_VALUE_INVALID;
  uint32_t gid = HB_MAP_VALUE_INVALID;
  while (hb_map_next(unicode_to_gid, &index, &cp, &gid)) {
    out[cp] = gid;
  }
  return out;
}

StatusOr<bool> DependencyClosure::AnalyzeSegment(
    segment_index_t segment_id, GlyphSet& and_gids, GlyphSet& or_gids,
    GlyphSet& exclusive_gids) const {

  // This uses a dependency graph (from harfbuzz) to infer how 'segment_id'
  // appears in the activation conditions of any glyphs reachable from it.
  // This aims to have identical output to GlyphClosureCache::AnalyzeSegment()
  // which uses harfbuzz glyph closure to infer conditions.
  //
  // The high level process works like this:
  // 1. Input segment is converted to a list of codepoints, and those to their nominal glyphs.
  // 2. We walk the dependency graph from the nominal glyphs. During traversal edges
  //    are filtered out that are not in the space of all segments (eg. we don't traverse
  //    into the subgraph of the init font)
  // 3. All glyphs encountered during the traversal are categorized into OR, AND, or EXCLUSIVE
  //    based on the details of the traversal.
  //
  // EXCLUSIVE: glyphs that are reachable only from this segment and/or the init font subgraph.
  // OR: Non exclusive glyphs that are reached via disjunctive dependencies, for example glyf components.
  // AND: Non exclusive glyphs that are via via conjunctive dependencies, for example UVS.
  //
  // TODO(garretrieger): This implementation is still early stages and is missing quite a bit,
  // here's a list of some additional things that are needed:
  //
  // - CFF/CFF2 seac components.
  // - preprocess to find the set of VS in the graph, for now disallow segments
  //   that intersect these.
  // - or, just add proper support for UVS handling. These would be treated as conjunctive.
  //   will need to extract the VS codepoints from the graph edges.
  // - Handle simple disjunctive GSUB lookups (may need conjunction with features).
  // - Handle simple conjunctive GSUB lookups (eg. liga)
  // - Handle features in the input segment (once GSUB is supported).
  if (segment_id >= segmentation_info_->Segments().size()) {
    return absl::InvalidArgumentError(absl::StrCat("Segment index ", segment_id, " is out of bounds."));
  }

  const CodepointSet& unicodes =
      segmentation_info_->Segments().at(segment_id).Definition().codepoints;

  flat_hash_map<glyph_id_t, unsigned> traversed_edge_counts;
  const GlyphSet& closure_glyphs = segmentation_info_->NonInitFontGlyphs();
  GlyphSet glyphs;
  for (hb_codepoint_t u : unicodes) {
    auto it = unicode_to_gid_.find(u);
    if (it == unicode_to_gid_.end()) {
      continue;
    }
    glyph_id_t gid = it->second;
    if (!closure_glyphs.contains(gid)) {
      continue;
    }

    glyphs.insert(gid);
    traversed_edge_counts[gid]++;
  }

  if (!TraverseGraph(glyphs, traversed_edge_counts)) {
    return false;
  }

  GlyphSet shared_glyphs; // set of glyphs which are accessible from outside this subgraph.
  for (auto [gid, count] : traversed_edge_counts) {
    unsigned incoming_edge_count = incoming_edge_count_.at(gid);

    exclusive_gids.insert(gid);
    if (count < incoming_edge_count) {
      shared_glyphs.insert(gid);
    } else if (count != incoming_edge_count) {
      return absl::InternalError(absl::StrCat(
        "Should not happen traversed incoming edge count is greater than "
        "the precomputed incoming edge counts: g", gid, " = ", count, " > ", incoming_edge_count));
    }
  }

  // We need to find glyphs that are reachable from other segments, which are those
  // glyphs that are reachable from any shared_glyphs found above.
  flat_hash_map<glyph_id_t, unsigned> all_shared_glyphs;
  if (!TraverseGraph(shared_glyphs, all_shared_glyphs)) {
    return false;
  }

  // Now we can make the glyph condition categorizations
  // any glyphs not in 'shared_glyphs' are only reachable from
  // the iput segment so are exclusive. Everything else is disjunctive.
  for (auto [gid, _] : all_shared_glyphs) {
    or_gids.insert(gid);
  }
  exclusive_gids.subtract(or_gids);


  return true;
}

}  // namespace ift::encoder