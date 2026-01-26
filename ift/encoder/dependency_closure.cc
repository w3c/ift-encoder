#include "ift/encoder/dependency_closure.h"

#include "absl/log/log.h"
#include "common/font_helper.h"
#include "common/hb_set_unique_ptr.h"
#include "common/int_set.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/types.h"

using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::StatusOr;
using common::CodepointSet;
using common::GlyphSet;
using common::IntSet;
using common::FontHelper;
using common::hb_set_unique_ptr;

namespace ift::encoder {

bool DependencyClosure::ShouldFollowEdge(
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

static DependencyClosure::AnalysisAccuracy Combine(DependencyClosure::AnalysisAccuracy a, DependencyClosure::AnalysisAccuracy b) {
  if (a == DependencyClosure::INACCURATE || b == DependencyClosure::INACCURATE) {
    return DependencyClosure::INACCURATE;
  }
  return DependencyClosure::ACCURATE;
}

DependencyClosure::AnalysisAccuracy DependencyClosure::AccuracyForGlyph(glyph_id_t gid) const {
  if (segmentation_info_->InitFontGlyphs().contains(gid)) {
    return ACCURATE;
  }

  return context_glyphs_.contains(gid) ? INACCURATE : ACCURATE;
}

static DependencyClosure::AnalysisAccuracy AccuracyForTableTag(hb_tag_t tag) {
  // TODO(garretrieger): implement handling for these tables to allow them to be removed
  //                     from the disallowed list.
  // - cmap: needs special handling for UVS
  // - gsub: needs special handling based on the lookup type (support the "simple" GSUB types).
  // - CFF/CFF2: investigate if seac components need any special handling.
  if (tag == HB_TAG('G', 'S', 'U', 'B') ||
         tag == HB_TAG('c', 'm', 'a', 'p') ||
         tag == HB_TAG('C', 'F', 'F', ' ') || tag == HB_TAG('C', 'F', 'F', '2')) {
    return DependencyClosure::INACCURATE;
  }
  return DependencyClosure::ACCURATE;
}


DependencyClosure::AnalysisAccuracy DependencyClosure::HandleUnicodeOutgoingEdges(
    hb_codepoint_t unicode,
    std::vector<Node>& next,
    absl::flat_hash_map<Node, unsigned>& traversed_edges
) const {

  auto it = unicode_to_gid_.find(unicode);
  if (it == unicode_to_gid_.end()) {
    // Unknown unicode has no outgoing edges.
    return ACCURATE;
  }


  if (segmentation_info_->NonInitFontGlyphs().contains(it->second)) {
    Node node = Node::Glyph(it->second);
    traversed_edges[node]++;
    next.push_back(node);
  }

  // The subsetter adds unicode bidi mirrors for any unicode codepoints,
  // so add a dep graph edge for those if they exist:
  auto unicode_funcs = hb_unicode_funcs_get_default ();
  hb_codepoint_t mirror = hb_unicode_mirroring(unicode_funcs, unicode);
  if (mirror != unicode && !segmentation_info_->InitFontSegment().codepoints.contains(mirror)) {
    Node node = Node::Unicode(mirror);
    traversed_edges[node]++;
    next.push_back(node);
  }

  return ACCURATE;
}

DependencyClosure::AnalysisAccuracy DependencyClosure::HandleGlyphOutgoingEdges(
    glyph_id_t gid,
    std::vector<Node>& next,
    absl::flat_hash_map<Node, unsigned>& traversed_edges
) const {
  hb_codepoint_t index = 0;
  hb_tag_t table_tag = HB_CODEPOINT_INVALID;
  hb_codepoint_t dep_gid = HB_CODEPOINT_INVALID;
  hb_tag_t layout_tag = HB_CODEPOINT_INVALID;
  hb_codepoint_t ligature_set = HB_CODEPOINT_INVALID;
  AnalysisAccuracy accuracy = ACCURATE;
  while (hb_depend_get_glyph_entry(dependency_graph_.get(), gid, index++, &table_tag,
                                   &dep_gid, &layout_tag, &ligature_set)) {
    if (!ShouldFollowEdge(table_tag, gid, dep_gid, layout_tag)) {
      continue;
    }

    accuracy = Combine(accuracy, AccuracyForTableTag(table_tag));

    Node node = Node::Glyph(dep_gid);
    traversed_edges[node]++;
    next.push_back(node);
  }

  return accuracy;
}

DependencyClosure::AnalysisAccuracy DependencyClosure::HandleSegmentOutgoingEdges(
    segment_index_t id,
    std::vector<Node>& next,
    absl::flat_hash_map<Node, unsigned>& traversed_edges
  ) const {

  if (id >= segmentation_info_->Segments().size()) {
    // Unknown segment has no outgoing edges.
    return ACCURATE;
  }

  const Segment& s = segmentation_info_->Segments().at(id);
  for (hb_codepoint_t u : s.Definition().codepoints) {
    if (segmentation_info_->InitFontSegment().codepoints.contains(u)) {
      continue;
    }
    Node node = Node::Unicode(u);
    traversed_edges[node]++;
    next.push_back(node);
  }

  return ACCURATE;
}

DependencyClosure::AnalysisAccuracy DependencyClosure::TraverseGraph(const absl::btree_set<Node>& nodes,
                                      flat_hash_map<Node, unsigned>& traversed_edges) const {
  VLOG(1) << "DependencyClosure::TraverseGraph(...)";
  std::vector<Node> next;
  for (Node node : nodes) {
    next.push_back(node);
    // Ensure an edge count entry exists.
    traversed_edges.insert(std::pair(node, 0));
  }

  AnalysisAccuracy accuracy = ACCURATE;
  flat_hash_set<Node> visited;
  while (!next.empty()) {
    Node node = next.back();
    next.pop_back();

    if (visited.contains(node)) {
      continue;
    }
    visited.insert(node);

    if (node.IsGlyph()) {
      accuracy = Combine(accuracy, AccuracyForGlyph(node.Id()));
    }

    if (node.IsGlyph()) {
      accuracy = Combine(accuracy, HandleGlyphOutgoingEdges(node.Id(), next, traversed_edges));
    }

    if (node.IsUnicode()) {
      accuracy = Combine(accuracy, HandleUnicodeOutgoingEdges(node.Id(), next, traversed_edges));
    }

    if (node.IsSegment()) {
      accuracy = Combine(accuracy, HandleSegmentOutgoingEdges(node.Id(), next, traversed_edges));
    }
  }

  return accuracy;
}

DependencyClosure::AnalysisAccuracy DependencyClosure::TraverseGlyphGraph(
    const common::GlyphSet& glyphs,
    absl::flat_hash_map<Node, unsigned>& traversed_edges) const {
  VLOG(1) << "DependencyClosure::TraverseGlyphGraph(" << glyphs.ToString() << ")";
  btree_set<Node> nodes;
  for (glyph_id_t gid : glyphs) {
    nodes.insert(Node::Glyph(gid));
  }
  return TraverseGraph(nodes, traversed_edges);
}

flat_hash_map<DependencyClosure::Node, glyph_id_t>
DependencyClosure::ComputeIncomingEdgeCount() const {
  VLOG(1) << "DependencyClosure::ComputeIncomingEdgeCount()";

  btree_set<Node> nodes;
  for (segment_index_t s = 0; s < segmentation_info_->Segments().size(); s++) {
    nodes.insert(Node::Segment(s));
  }

  flat_hash_map<Node, glyph_id_t> incoming_edge_counts;
  TraverseGraph(nodes, incoming_edge_counts);
  return incoming_edge_counts;
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

StatusOr<DependencyClosure::AnalysisAccuracy> DependencyClosure::AnalyzeSegment(
    const common::SegmentSet& segments, GlyphSet& and_gids, GlyphSet& or_gids,
    GlyphSet& exclusive_gids) {

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
  btree_set<Node> start_nodes;
  for (segment_index_t segment_id : segments) {
    if (segment_id >= segmentation_info_->Segments().size()) {
      return absl::InvalidArgumentError(absl::StrCat("Segment index ", segment_id, " is out of bounds."));
    }

    const Segment& segment = segmentation_info_->Segments().at(segment_id);
    if (!segment.Definition().feature_tags.empty()) {
      // Feature based segments not yet handled.
      inaccurate_results_++;
      return INACCURATE;
    }

    start_nodes.insert(Node::Segment(segment_id));
  }

  flat_hash_map<Node, unsigned> traversed_edge_counts;
  if (TraverseGraph(start_nodes, traversed_edge_counts) == INACCURATE) {
    inaccurate_results_++;
    return INACCURATE;
  }

  btree_set<Node> shared_nodes; // set of nodes which are accessible from outside this subgraph.
  for (auto [node, count] : traversed_edge_counts) {
    unsigned incoming_edge_count = incoming_edge_count_.at(node);

    if (node.IsGlyph()) {
      exclusive_gids.insert(node.Id());
    }

    if (count < incoming_edge_count) {
      shared_nodes.insert(node);
    } else if (count != incoming_edge_count) {
      return absl::InternalError(absl::StrCat(
        "Should not happen traversed incoming edge count is greater than "
        "the precomputed incoming edge counts: ", node.ToString(), " = ", count, " > ", incoming_edge_count));
    }
  }

  // We need to find glyphs that are reachable from other segments, which are those
  // glyphs that are reachable from any shared_glyphs found above.
  flat_hash_map<Node, unsigned> all_shared_nodes;
  if (TraverseGraph(shared_nodes, all_shared_nodes) == INACCURATE) {
    inaccurate_results_++;
    return INACCURATE;
  }

  // Now we can make the glyph condition categorizations
  // any glyphs not in 'shared_glyphs' are only reachable from
  // the iput segment so are exclusive. Everything else is disjunctive.
  for (auto [node, _] : all_shared_nodes) {
    if (node.IsGlyph()) {
      or_gids.insert(node.Id());
    }
  }
  exclusive_gids.subtract(or_gids);

  accurate_results_++;
  return ACCURATE;
}

// the depedency graph doesn't capture any information above lookup ahead/lookup behind glyphs,
// these glyphs are context for the lookups and as a result may interact with them despite
// not being directly recorded in the dep graph.
GlyphSet DependencyClosure::CollectContextGlyphs(hb_face_t* face, const IntSet& full_feature_set) {
  constexpr hb_tag_t GSUB = HB_TAG('G', 'S', 'U', 'B');
  std::vector<hb_tag_t> feature_array = full_feature_set.to_vector();
  feature_array.push_back(0); // hb takes null terminated arrays.

  hb_set_unique_ptr lookup_indices = common::make_hb_set();
  hb_ot_layout_collect_lookups(face, GSUB, nullptr, nullptr, feature_array.data(), lookup_indices.get());

  hb_codepoint_t lookup_index = HB_SET_VALUE_INVALID;
  hb_set_unique_ptr glyphs_before = common::make_hb_set();
  hb_set_unique_ptr glyphs_after = common::make_hb_set();
  while (hb_set_next(lookup_indices.get(), &lookup_index)) {
    // the glyphs input/output sets don't need to be collected since these will already be part
    // of the depedency graph.
    hb_ot_layout_lookup_collect_glyphs(face, GSUB, lookup_index,
      glyphs_before.get(), nullptr, glyphs_after.get(), nullptr);
  }

  GlyphSet context;
  context.union_from(glyphs_before.get());
  context.union_from(glyphs_after.get());

  return context;
}

}  // namespace ift::encoder