#include "ift/encoder/dependency_closure.h"

#include "absl/log/log.h"
#include "common/font_helper.h"
#include "common/hb_set_unique_ptr.h"
#include "common/int_set.h"
#include "ift/dep_graph/dependency_graph.h"
#include "ift/dep_graph/node.h"
#include "ift/dep_graph/traversal.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/types.h"

using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::Status;
using absl::StatusOr;
using common::CodepointSet;
using common::GlyphSet;
using common::IntSet;
using common::FontHelper;
using common::SegmentSet;
using common::hb_set_unique_ptr;
using ift::dep_graph::Node;
using ift::dep_graph::DependencyGraph;
using ift::dep_graph::Traversal;

namespace ift::encoder {

DependencyClosure::AnalysisAccuracy DependencyClosure::TraversalAccuracy(const Traversal& traversal) const {
  // TODO(garretrieger): implement handling for these tables to allow them to be removed
  //                     from the disallowed list.
  // - cmap: needs special handling for UVS edges

  // TODO XXXXXXX allow UVS edges if their conditions are fully satisfied.
  if (traversal.HasOnlyLigaConditionalGlyphs()) {
    // When liga glyphs are present and accurate analysis is still possible
    // if all of the liga glyphs have been reached.
    GlyphSet required_liga = traversal.RequiredLigaGlyphs();
    required_liga.subtract(traversal.ReachableGlyphs());
    required_liga.subtract(segmentation_info_->InitFontGlyphs());
    if (!required_liga.empty()) {
      // TODO XXXX if there are nested ligatures this may be too simplisitic. Since reachable glyphs
      // might contain things are aren't actually accessible from the starting point (ie. ligature which
      // isn't triggerable from the starting set adds a glyph that unlocks it). Try and construct a
      // test case to demonstrate this.
      return AnalysisAccuracy::INACCURATE;
    }
  } else if (traversal.HasConditionalGlyphs()) {
    // TODO(garretrieger): it should be possible to support at least liga when all of
    // the liga glyphs have been reached.
    return AnalysisAccuracy::INACCURATE;
  }

  for (hb_tag_t tag : traversal.ContextLayoutFeatures()) {
    // TODO XXXX broader feature support. For now to keep things simple only allow features in the init font
    // which are always enabled no matter what.
    if (!init_font_features_.contains(tag)) {
      return AnalysisAccuracy::INACCURATE;
    }
  }

  for (hb_tag_t tag : traversal.ReachedLayoutFeatures()) {
    if (!init_font_features_.contains(tag)) {
      return AnalysisAccuracy::INACCURATE;
    }
  }

  if (traversal.ReachableGlyphs().intersects(context_glyphs_)) {
    return AnalysisAccuracy::INACCURATE;
  }

  return AnalysisAccuracy::ACCURATE;
}

Status DependencyClosure::SegmentsChanged(bool init_font_change, const SegmentSet& segments) {
  VLOG(1) << "DependencyClosure::SegmentsChanged()";

  if (init_font_change) {
    ClearReachabilityIndex();
  } else {
    TRYV(UpdateReachabilityIndex(segments));
  }

  if (!init_font_change && segmentation_info_->SegmentsAreDisjoint()) {
    // If the init font is changed and all segments are disjoint then there won't be any changes to incoming
    // edge counts as segment modifications will just shift outgoing edges around between segments.
    return absl::OkStatus();
  }

  // TODO XXXXX can we do an incremental update of incoming_edge_counts_, and context
  btree_set<Node> nodes;
  for (segment_index_t s = 0; s < segmentation_info_->Segments().size(); s++) {
    if (segmentation_info_->Segments().at(s).Definition().Empty()) {
      continue;
    }
    nodes.insert(Node::Segment(s));
  }

  Traversal traversal = TRY(graph_.TraverseGraph(nodes));
  incoming_edge_counts_ = traversal.TraversedIncomingEdgeCounts();

  context_glyphs_ = traversal.ContextGlyphs();
  init_font_features_ = TRY(InitFeatureSet(segmentation_info_, original_face_.get()));

  // The init font may have reachable glyphs which are not in the init font closure,
  // we need to record the context glyphs from these as they are potential interaction
  // points.
  init_font_context_glyphs_.clear();
  Traversal init_font_traversal = TRY(graph_.TraverseGraph({Node::InitFont()},
    &segmentation_info_->FullClosure(), &segmentation_info_->FullDefinition().codepoints));
  for (const auto& [g, context] : init_font_traversal.ContextPerGlyph()) {
    if (segmentation_info_->NonInitFontGlyphs().contains(g)) {
      context_glyphs_.union_set(context);
      init_font_context_glyphs_.union_set(context);
    }
  }
  init_font_reachable_glyphs_ = init_font_traversal.ReachableGlyphs();
  init_font_reachable_glyphs_.subtract(segmentation_info_->InitFontGlyphs());

  return absl::OkStatus();
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
    if (segment.Definition().Empty()) {
      // Empty segments are ignored.
      continue;
    }

    start_nodes.insert(Node::Segment(segment_id));
  }

  Traversal traversal = TRY(graph_.TraverseGraph(start_nodes));
  if (TraversalAccuracy(traversal) == INACCURATE) {
    inaccurate_results_++;
    return INACCURATE;
  }


  btree_set<Node> shared_nodes; // set of nodes which are accessible from outside this subgraph.
  for (auto [node, count] : traversal.TraversedIncomingEdgeCounts()) {
    unsigned incoming_edge_count = incoming_edge_counts_.at(node);

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
  Traversal all_shared_nodes = TRY(graph_.TraverseGraph(shared_nodes));
  if (TraversalAccuracy(all_shared_nodes) == INACCURATE) {
    inaccurate_results_++;
    return INACCURATE;
  }

  // Now we can make the glyph condition categorizations
  // any glyphs not in 'shared_glyphs' are only reachable from
  // the iput segment so are exclusive. Everything else is disjunctive.
  for (auto [node, _] : all_shared_nodes.TraversedIncomingEdgeCounts()) {
    if (node.IsGlyph()) {
      or_gids.insert(node.Id());
    }
  }
  exclusive_gids.subtract(or_gids);

  accurate_results_++;
  return ACCURATE;
}

StatusOr<IntSet> DependencyClosure::InitFeatureSet(
    const RequestedSegmentationInformation* segmentation_info,
    hb_face_t* face) {
  hb_subset_input_t* input = hb_subset_input_create_or_fail();
  if (!input) {
    return absl::InternalError("Failed to create subset input object.");
  }

  segmentation_info->InitFontSegment().ConfigureInput(input, face);
  IntSet features(
      hb_subset_input_set(input, HB_SUBSET_SETS_LAYOUT_FEATURE_TAG));
  hb_subset_input_destroy(input);

  return features;
}

StatusOr<SegmentSet> DependencyClosure::SegmentsThatInteractWith(const GlyphSet& glyphs) {
  TRYV(EnsureReachabilityIndexPopulated());

  // TODO XXXXX also need to check interactions with:
  // - features (including segments with features), treated as additional context items.
  SegmentSet visited_segments;
  GlyphSet visited_glyphs;
  GlyphSet to_check = glyphs;
  bool init_font_context_added = false;
  while (!to_check.empty()) {
    glyph_id_t gid = *to_check.min();
    to_check.erase(gid);
    visited_glyphs.insert(gid);

    // gid may be reachable from the init font.
    if (!init_font_context_added && init_font_reachable_glyphs_.contains(gid)) {
      GlyphSet additional = init_font_context_glyphs_;
      additional.subtract(visited_glyphs);
      to_check.union_set(additional);
      init_font_context_added = true;
    }

    // now check if any segments can reach it.
    auto segments = segments_that_can_reach_.find(gid);
    if (segments == segments_that_can_reach_.end()) {
      continue;
    }

    for (segment_index_t s : segments->second) {
      if (visited_segments.contains(s)) {
        continue;
      }

      visited_segments.insert(s);
      auto traversal = TRY(graph_.TraverseGraph({Node::Segment(s)}));
      GlyphSet additional = traversal.ContextGlyphs();
      additional.subtract(visited_glyphs);
      to_check.union_set(additional);
    }
  }

  return visited_segments;
}

Status DependencyClosure::EnsureReachabilityIndexPopulated() {
  if (!segments_that_can_reach_.empty() || !glyphs_that_can_be_reached_.empty()) {
    return absl::OkStatus();
  }
  return UpdateReachabilityIndex(SegmentSet::all());
}

Status DependencyClosure::UpdateReachabilityIndex(const common::SegmentSet& segments) {
  if (!segments_that_can_reach_.empty() || !glyphs_that_can_be_reached_.empty()) {
    // If indices have existing data, then we need to ensure prior entries for the
    // segments to be updated are cleared out.
    for (segment_index_t s : segments) {
      if (s < segmentation_info_->Segments().size()) {
        ClearReachabilityIndex(s);
      } else {
        break;
      }
    }
  }

  for (segment_index_t s : segments) {
    if (s < segmentation_info_->Segments().size()) {
      TRYV(UpdateReachabilityIndex(s));
    } else {
      break;
    }
  }
  return absl::OkStatus();
}

Status DependencyClosure::UpdateReachabilityIndex(segment_index_t s) {
  auto traversal = TRY(graph_.TraverseGraph(btree_set<Node> {Node::Segment(s)}));

  for (glyph_id_t g : traversal.ReachableGlyphs()) {
    segments_that_can_reach_[g].insert(s);
    glyphs_that_can_be_reached_[s].insert(g);
  }

  return absl::OkStatus();
}

void DependencyClosure::ClearReachabilityIndex() {
  glyphs_that_can_be_reached_.clear();
  segments_that_can_reach_.clear();
}

void DependencyClosure::ClearReachabilityIndex(segment_index_t segment) {
  auto glyphs = glyphs_that_can_be_reached_.find(segment);
  if (glyphs == glyphs_that_can_be_reached_.end()) {
    return;
  }

  for (glyph_id_t gid : glyphs->second) {
    segments_that_can_reach_[gid].erase(segment);
  }
  glyphs_that_can_be_reached_.erase(glyphs);
}

}  // namespace ift::encoder