#include "ift/dep_graph/dependency_graph.h"

#include "absl/log/log.h"
#include "common/hb_set_unique_ptr.h"
#include "common/int_set.h"
#include "common/font_helper.h"
#include "ift/dep_graph/traversal.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/types.h"

using absl::Status;
using absl::StatusOr;
using absl::flat_hash_map;
using absl::flat_hash_set;
using common::hb_set_unique_ptr;
using common::make_hb_set;
using common::FontHelper;
using common::GlyphSet;
using common::IntSet;
using ift::encoder::Segment;
using ift::encoder::glyph_id_t;
using ift::encoder::segment_index_t;
using ift::encoder::RequestedSegmentationInformation;

namespace ift::dep_graph {

StatusOr<Traversal> DependencyGraph::TraverseGraph(const absl::btree_set<Node>& nodes) const {
  VLOG(1) << "DependencyGraph::TraverseGraph(...)";
  Traversal traversal;
  std::vector<Node> next;
  for (Node node : nodes) {
    next.push_back(node);
    traversal.VisitInitNode(node);
  }

  flat_hash_set<Node> visited;
  while (!next.empty()) {
    Node node = next.back();
    next.pop_back();

    if (visited.contains(node)) {
      continue;
    }
    visited.insert(node);

    if (node.IsGlyph()) {
      TRYV(HandleGlyphOutgoingEdges(node.Id(), next, traversal));
    }

    if (node.IsUnicode()) {
      HandleUnicodeOutgoingEdges(node.Id(), next, traversal);
    }

    if (node.IsSegment()) {
      HandleSegmentOutgoingEdges(node.Id(), next, traversal);
    }
  }

  return traversal;
}

bool DependencyGraph::ShouldFollowEdge(
  hb_tag_t table_tag,
  glyph_id_t from_gid,
  glyph_id_t to_gid,
  hb_tag_t feature_tag) const {

  const GlyphSet& closure_glyphs = segmentation_info_->NonInitFontGlyphs();
  bool r = closure_glyphs.contains(to_gid) &&
           closure_glyphs.contains(from_gid) &&
         (feature_tag == HB_CODEPOINT_INVALID ||
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

void DependencyGraph::HandleUnicodeOutgoingEdges(
    hb_codepoint_t unicode,
    std::vector<Node>& next,
    Traversal& traversal
) const {

  auto it = unicode_to_gid_.find(unicode);
  if (it == unicode_to_gid_.end()) {
    // Unknown unicode has no outgoing edges.
    return;
  }

  if (segmentation_info_->NonInitFontGlyphs().contains(it->second)) {
    Node node = Node::Glyph(it->second);
    traversal.Visit(node);
    next.push_back(node);
  }

  // The subsetter adds unicode bidi mirrors for any unicode codepoints,
  // so add a dep graph edge for those if they exist:
  auto unicode_funcs = hb_unicode_funcs_get_default ();
  hb_codepoint_t mirror = hb_unicode_mirroring(unicode_funcs, unicode);
  if (mirror != unicode && !segmentation_info_->InitFontSegment().codepoints.contains(mirror)) {
    Node node = Node::Unicode(mirror);
    traversal.Visit(node);
    next.push_back(node);
  }

  // TODO XXXX handle UVS edges here instead, probably want to still pre-record edges as unicode -> gid
  // mappings, which means UVS edges will get ignored in the glyph outgoing edges.
}

Status DependencyGraph::HandleGlyphOutgoingEdges(
    glyph_id_t gid,
    std::vector<Node>& next,
    Traversal& traversal
) const {
  hb_codepoint_t index = 0;
  hb_tag_t table_tag = HB_CODEPOINT_INVALID;
  hb_codepoint_t dep_gid = HB_CODEPOINT_INVALID;
  hb_tag_t layout_tag = HB_CODEPOINT_INVALID;
  hb_codepoint_t ligature_set = HB_CODEPOINT_INVALID;
  hb_codepoint_t context_set = HB_CODEPOINT_INVALID;

  while (hb_depend_get_glyph_entry(dependency_graph_.get(), gid, index++, &table_tag,
                                   &dep_gid, &layout_tag, &ligature_set, &context_set)) {
    if (!ShouldFollowEdge(table_tag, gid, dep_gid, layout_tag)) {
      continue;
    }

    Node node = Node::Glyph(dep_gid);

    next.push_back(node);
    if (table_tag == HB_TAG('G', 'S', 'U', 'B')) {
      if (context_set != HB_CODEPOINT_INVALID) {
        GlyphSet context = TRY(GetContextSet(context_set));
        traversal.VisitContextual(node, layout_tag, context);
      } else if (ligature_set != HB_CODEPOINT_INVALID) {
        GlyphSet liga = TRY(GetLigaSet(ligature_set));
        traversal.VisitLigature(node, layout_tag, liga);
      } else {
        traversal.VisitGsub(node, layout_tag);
      }
      continue;
    }

    if (table_tag == HB_TAG('c', 'm', 'a', 'p') && layout_tag != HB_CODEPOINT_INVALID) {
      traversal.VisitUVS(node, layout_tag /* layout tag holds the VS char */);
    } else {
      // Just a regular edge
      traversal.Visit(node, table_tag);
    }


  }

  return absl::OkStatus();
}

void DependencyGraph::HandleSegmentOutgoingEdges(
    segment_index_t id,
    std::vector<Node>& next,
    Traversal& traversal
  ) const {

  if (id >= segmentation_info_->Segments().size()) {
    // Unknown segment has no outgoing edges.
    return;
  }

  const Segment& s = segmentation_info_->Segments().at(id);
  for (hb_codepoint_t u : s.Definition().codepoints) {
    if (segmentation_info_->InitFontSegment().codepoints.contains(u)) {
      continue;
    }
    Node node = Node::Unicode(u);
    traversal.Visit(node);
    next.push_back(node);
  }
}

flat_hash_map<hb_codepoint_t, glyph_id_t> DependencyGraph::UnicodeToGid(
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

StatusOr<IntSet> DependencyGraph::FullFeatureSet(
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

StatusOr<GlyphSet> DependencyGraph::GetLigaSet(hb_codepoint_t liga_set_id) const {
  hb_set_unique_ptr out = make_hb_set();
  if (!hb_depend_get_set_from_index(dependency_graph_.get(), liga_set_id, out.get())) {
    return absl::InternalError("Ligature set lookup failed.");
  }
  GlyphSet glyphs(out.get());
  return glyphs;
}

StatusOr<GlyphSet> DependencyGraph::GetContextSet(hb_codepoint_t context_set_id) const {
  // the context set is actually a set of sets.
  hb_set_unique_ptr context_sets = make_hb_set();
  if (!hb_depend_get_set_from_index(dependency_graph_.get(), context_set_id, context_sets.get())) {
    return absl::InternalError("Context set lookup failed.");
  }

  GlyphSet glyphs;
  hb_codepoint_t set_id = HB_CODEPOINT_INVALID;
  while (hb_set_next(context_sets.get(), &set_id)) {
    if (set_id < 0x80000000) {
      // special case, set of one element.
      glyphs.insert(set_id);
      continue;
    }

    hb_codepoint_t actual_set_id = set_id & 0x7FFFFFFF;
    hb_set_unique_ptr context_glyphs = make_hb_set();
    if (!hb_depend_get_set_from_index(dependency_graph_.get(), actual_set_id, context_glyphs.get())) {
      return absl::InternalError("Context sub set lookup failed.");
    }
    glyphs.union_from(context_glyphs.get());
  }

  return glyphs;
}

}  // namespace ift::dep_graph