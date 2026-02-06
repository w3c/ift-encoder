#include "ift/dep_graph/dependency_graph.h"
#include <cstdint>
#include <optional>
#include <utility>

#include "absl/log/log.h"
#include "common/hb_set_unique_ptr.h"
#include "common/int_set.h"
#include "common/font_helper.h"
#include "ift/dep_graph/node.h"
#include "ift/dep_graph/traversal.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/types.h"

using absl::Status;
using absl::StatusOr;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::btree_set;
using common::hb_set_unique_ptr;
using common::make_hb_set;
using common::CodepointSet;
using common::FontHelper;
using common::GlyphSet;
using common::IntSet;
using common::SegmentSet;
using ift::encoder::Segment;
using ift::encoder::glyph_id_t;
using ift::encoder::segment_index_t;
using ift::encoder::RequestedSegmentationInformation;
using ift::encoder::SubsetDefinition;

namespace ift::dep_graph {

static constexpr hb_tag_t cmap = HB_TAG('c', 'm', 'a', 'p');
static constexpr hb_tag_t glyf = HB_TAG('g', 'l', 'y', 'f');
static constexpr hb_tag_t GSUB = HB_TAG('G', 'S', 'U', 'B');
static constexpr hb_tag_t COLR = HB_TAG('C', 'O', 'L', 'R');
static constexpr hb_tag_t MATH = HB_TAG('M', 'A', 'T', 'H');
static constexpr hb_tag_t CFF = HB_TAG('C', 'F', 'F', ' ');

class TraversalContext;

// Tracks an edge who's context requirements are not yet satisfied.
class PendingEdge {
 public:
  static PendingEdge Uvs(hb_codepoint_t a, hb_codepoint_t b, glyph_id_t gid) {
    PendingEdge edge(Node::Glyph(gid));
    edge.required_codepoints = std::make_pair(a, b);
    edge.table_tag = cmap;
    return edge;
  }

  static PendingEdge Gsub(hb_tag_t feature, glyph_id_t gid) {
    PendingEdge edge(Node::Glyph(gid));
    edge.required_feature = feature;
    edge.table_tag = GSUB;
    return edge;
  }

  void DoTraversal(TraversalContext& context) const;

  bool ConstraintsSatisfied(
    const hb_depend_t* depend,
    const CodepointSet& reached_unicodes,
    const GlyphSet& reached_glyphs,
    const flat_hash_set<hb_tag_t>& reached_features
  ) const {

    if (required_codepoints.has_value() &&
        (!reached_unicodes.contains(required_codepoints->first) ||
         !reached_unicodes.contains(required_codepoints->second))) {
      return false;
    }

    if (required_feature.has_value() && !reached_features.contains(*required_feature)) {
      return false;
    }

    if (required_liga_set_index.has_value()) {
      // TODO XXXXX implement this check
      return false;
    }

    if (!required_context_set_indices.empty()) {
      // TODO XXXXX implement this check
      return false;
    }

    return true;
  }

 private:
  PendingEdge(Node dest_) : dest(dest_) {}

  Node dest;
  hb_tag_t table_tag;

  // TODO XXXX anything else?
  std::optional<hb_tag_t> required_feature = std::nullopt;
  std::optional<uint32_t> required_liga_set_index;
  IntSet required_context_set_indices;
  std::optional<std::pair<hb_codepoint_t, hb_codepoint_t>> required_codepoints = std::nullopt;
};

class TraversalContext {
 public:
  // These filter out edges from being traversed.
  flat_hash_set<hb_tag_t> table_filter = {
    cmap,
    glyf,
    GSUB,
    COLR,
    MATH,
    CFF
  };
  const GlyphSet* glyph_filter = nullptr;
  const CodepointSet* unicode_filter = nullptr;
  const flat_hash_set<hb_tag_t>* feature_filter = nullptr;
  uint32_t node_type_filter = 0xFFFFFFFF;

  bool enforce_context = false;

  Traversal traversal;

  void SetStartNodes(const btree_set<Node>& start) {
    for (Node node : start) {
      traversal.VisitInitNode(node);
      Reached(node);
    }
  }

  void SetReachedToInitFont(
    const RequestedSegmentationInformation& segmentation_info,
    const flat_hash_set<hb_tag_t>& init_features) {

    reached_glyphs_ = segmentation_info.InitFontGlyphs();
    reached_unicodes_ = segmentation_info.InitFontSegment().codepoints;
    reached_features_ = init_features;
  }

  std::optional<Node> GetNext() {
    if (next_.empty()) {
      return std::nullopt;
    }
    Node node = next_.back();
    next_.pop_back();
    return node;
  }

  // This checks all pending edges and if any have their constraints satisfied then they are
  // traversed. Returns true if there are now more nodes in the next queue.
  bool CheckPending(const hb_depend_t* depend_graph) {
    auto it = pending_edges_.begin();
    while (it != pending_edges_.end()) {
      const auto& pending = *it;
      if (pending.ConstraintsSatisfied(depend_graph, reached_unicodes_, reached_glyphs_, reached_features_)) {
        pending.DoTraversal(*this);
        pending_edges_.erase(it);
      } else {
        it++;
      }
    }

    return !next_.empty();
  }

  bool HasPendingEdges() const {
    return !pending_edges_.empty();
  }

  // Traverse an edge with no special context and/or additional information other than table tag
  void TraverseEdgeTo(Node dest, std::optional<hb_tag_t> table_tag = std::nullopt) {
    if (!ShouldFollow(dest, std::nullopt)) {
      return;
    }
    if (table_tag.has_value() && !table_filter.contains(*table_tag)) {
      return;
    }

    // TODO XXXXX simpler api to traversal maybe just record things of interest directly
    // insead of typing everything?
    if (table_tag.has_value()) {
      traversal.Visit(dest, *table_tag);
    } else {
      traversal.Visit(dest);
    }
    Reached(dest);
  }

  void TraverseUvsEdge(hb_codepoint_t a, hb_codepoint_t b, glyph_id_t gid) {
    if (!table_filter.contains(cmap)) {
      return;
    }

    Node dest = Node::Glyph(gid);
    if (!ShouldFollow(dest, std::nullopt)) {
      return;
    }

    bool can_reach_a = !unicode_filter || reached_unicodes_.contains(a) || unicode_filter->contains(a);
    bool can_reach_b = !unicode_filter || reached_unicodes_.contains(b) || unicode_filter->contains(b);
    if (enforce_context && (!can_reach_a || !can_reach_b)) {
      // edge can't be reached, ignore.
      return;
    }

    if (enforce_context && !reached_unicodes_.contains(a)) {
      Pending(PendingEdge::Uvs(a, b, gid));
    } else if (enforce_context && !reached_unicodes_.contains(b)) {
      Pending(PendingEdge::Uvs(a, b, gid));
    } else {
      // all requirements are satisfied
      traversal.Visit(dest, cmap);
      Reached(dest);
    }
  }

  void TraverseGsubEdgeTo(glyph_id_t gid, hb_tag_t feature) {
    if (!table_filter.contains(GSUB)) {
      return;
    }

    Node dest = Node::Glyph(gid);
    if (!ShouldFollow(dest, feature)) {
      return;
    }

    if (enforce_context && !reached_features_.contains(feature)) {
      Pending(PendingEdge::Gsub(feature, gid));
    } else {
      traversal.VisitGsub(dest, feature);
      Reached(dest);
    }
  }

  void TraverseContextualEdgeTo(glyph_id_t gid, hb_tag_t feature, const GlyphSet& context) {
    // TODO XXXX can implementation be shared with TraverseGsubEdgeTo?
    if (!table_filter.contains(GSUB)) {
      return;
    }

    Node dest = Node::Glyph(gid);
    if (!ShouldFollow(dest, feature)) {
      return;
    }

    // TODO XXXX check if it's possible for the constraints to be satisfied in the full closure.
    // ignore the edge if not.
    if (enforce_context) {
      // TODO XXXX add pending edge
    } else {
      traversal.VisitContextual(dest, feature, context);
      Reached(dest);
    }
  }

  void TraverseLigatureEdgeTo(glyph_id_t gid, hb_tag_t feature, const GlyphSet& context) {
    // TODO XXXX can implementation be shared with TraverseGsubEdgeTo?
    if (!table_filter.contains(GSUB)) {
      return;
    }

    Node dest = Node::Glyph(gid);
    if (!ShouldFollow(dest, feature)) {
      return;
    }

    // TODO XXXX check if it's possible for the constraints to be satisfied in the full closure.
    // ignore the edge if not.
    if (enforce_context) {
      // TODO XXXX add pending edge
    } else {
      traversal.VisitGsub(dest, feature);
      Reached(dest);
    }
  }

 private:
  void Pending(PendingEdge edge) {
    pending_edges_.push_back(edge);
  }

  void Reached(Node node) {
    if (visited_.contains(node)) {
      return;
    }

    next_.push_back(node);
    visited_.insert(node);

    if (!enforce_context) {
      return;
    }

    if (node.IsUnicode()) {
      reached_unicodes_.insert(node.Id());
    }

    if (node.IsGlyph()) {
      reached_glyphs_.insert(node.Id());
    }

    if (node.IsFeature()) {
      reached_features_.insert(node.Id());
    }
  }

  bool ShouldFollow(Node node, std::optional<hb_tag_t> layout_feature) const {
    if (!node.Matches(node_type_filter)) {
      return false;
    }

    if (feature_filter != nullptr && layout_feature.has_value() &&
        !feature_filter->contains(*layout_feature)) {
      if (!enforce_context || !reached_features_.contains(*layout_feature)) {
        return false;
      }
    }

    if (unicode_filter != nullptr && node.IsUnicode()) {
      return unicode_filter->contains(node.Id());
    }

    if (glyph_filter != nullptr && node.IsGlyph()) {
      return glyph_filter->contains(node.Id());
    }

    if (feature_filter != nullptr && node.IsFeature()) {
      return feature_filter->contains(node.Id());
    }

    return true;
  }

  std::vector<Node> next_ {};
  flat_hash_set<Node> visited_ {};

  // These track information about things that are needed to
  // activate context which is not yet seen.
  std::vector<PendingEdge> pending_edges_;

  CodepointSet reached_unicodes_;
  GlyphSet reached_glyphs_;
  flat_hash_set<hb_tag_t> reached_features_;

  // Visit methods.
};

void PendingEdge::DoTraversal(TraversalContext& context) const {
  if (table_tag == cmap && required_codepoints.has_value()) {
    // UVS edge
    context.TraverseUvsEdge(required_codepoints->first, required_codepoints->second, dest.Id());
    return;
  }

  if (table_tag == GSUB && required_feature.has_value()) {
    if (required_liga_set_index.has_value()) {
      // TODO XXXX implement me
    } else if (!required_context_set_indices.empty()) {
      // TODO XXXX implement me
    } else {
      // one to one GSUB edge.
      context.TraverseGsubEdgeTo(dest.Id(), *required_feature);
    }
    return;
  }

  // TODO XXXX other, add implementation
}

StatusOr<Traversal> DependencyGraph::TraverseGraph(TraversalContext* context) const {
  VLOG(1) << "DependencyGraph::TraverseGraph(...)";


  while (true) {
    std::optional<Node> next = context->GetNext();
    if (!next.has_value()) {
      if (context->CheckPending(dependency_graph_.get())) {
        continue;
      } else {
        // nothing left to traverse.
        break;
      }
    }

    if (next->IsGlyph()) {
      TRYV(HandleGlyphOutgoingEdges(next->Id(), context));
    }

    if (next->IsUnicode()) {
      HandleUnicodeOutgoingEdges(next->Id(), context);
    }

    if (next->IsSegment()) {
      HandleSegmentOutgoingEdges(next->Id(), context);
    }

    if (next->IsInitFont()) {
      HandleSubsetDefinitionOutgoingEdges(segmentation_info_->InitFontSegment(), context);
    }

    // Features don't have any outgoing edges
  }

  if (context->HasPendingEdges()) {
    context->traversal.SetPendingEdges();
  }

  return std::move(context->traversal);
}

StatusOr<Traversal> DependencyGraph::TraverseGraph(
  const btree_set<Node>& nodes,
  const GlyphSet* glyph_filter_ptr,
  const CodepointSet* unicode_filter_ptr
) const {
  TraversalContext context;

  CodepointSet non_init_font_codepoints;
  if (unicode_filter_ptr == nullptr) {
    non_init_font_codepoints = segmentation_info_->FullDefinition().codepoints;
    non_init_font_codepoints.subtract(segmentation_info_->InitFontSegment().codepoints);
  }

  GlyphSet non_init_font_glyphs;
  if (glyph_filter_ptr == nullptr) {
    non_init_font_glyphs = segmentation_info_->NonInitFontGlyphs();
  }

  context.unicode_filter = !unicode_filter_ptr ? &non_init_font_codepoints : unicode_filter_ptr;
  context.glyph_filter = !glyph_filter_ptr ? &non_init_font_glyphs : glyph_filter_ptr;
  context.feature_filter = &full_feature_set_;
  context.enforce_context = false;
  context.SetStartNodes(nodes);

  return TraverseGraph(&context);
}

StatusOr<Traversal> DependencyGraph::ClosureTraversal(const SegmentSet& start) const {

  // TODO XXXX need to properly handle the init font's impact on the closure:
  // - In context prepopulate the reached sets with things from the init font, so liga/uvs can be
  //   correctly unlocked.
  // - pre-populate pending edges from the init font traversal (ie. any reachable glyphs that
  //   are not in the current reached set, only the first level is needed).
  CodepointSet non_init_font_codepoints = segmentation_info_->FullDefinition().codepoints;
  non_init_font_codepoints.subtract(segmentation_info_->InitFontSegment().codepoints);
  GlyphSet non_init_font_glyphs = segmentation_info_->NonInitFontGlyphs();

  btree_set<Node> start_nodes;
  for (segment_index_t s : start) {
    start_nodes.insert(Node::Segment(s));
  }

  Traversal traversal_full;

  // Subsetting closure happens in phases which we need to mimic here:
  // 1. Unicode closure (bidi)
  // 2. Unicode to glyph (cmap + UVS)
  // 3. GSUB glyph closure
  // 4. MATH closure.
  // 5. COLR closure
  // 6. glyf closure
  // 7. CFF closure

  TraversalContext base_context;
  base_context.unicode_filter = &non_init_font_codepoints;
  base_context.glyph_filter = &non_init_font_glyphs;
  base_context.feature_filter = &full_feature_set_;
  base_context.enforce_context = true;
  base_context.SetReachedToInitFont(*segmentation_info_, TRY(InitFontFeatureSet()));

  /* ### Phase 1 + 2: Unicode and Unicode to glyph */
  {
    TraversalContext context = base_context;
    context.SetStartNodes(start_nodes);
    context.table_filter = {cmap};
    context.node_type_filter =
      Node::NodeType::SEGMENT | Node::NodeType::UNICODE | Node::NodeType::GLYPH | Node::NodeType::FEATURE;
    traversal_full = TRY(TraverseGraph(&context));
  }

  /* ### Phase 3: GSUB ### */
  {
    start_nodes.clear();
    for (glyph_id_t gid : traversal_full.ReachedGlyphs()) {
      start_nodes.insert(Node::Glyph(gid));
    }

    TraversalContext context = base_context;
    context.SetStartNodes(start_nodes);
    context.table_filter = {GSUB};
    context.node_type_filter = Node::NodeType::GLYPH;
    traversal_full.Merge(TRY(TraverseGraph(&context)));
  }

  // TODO XXXXXX phase 4, 5

  /* ### Phase 6: glyf ### */

  {
    start_nodes.clear();
    for (glyph_id_t gid : traversal_full.ReachedGlyphs()) {
      start_nodes.insert(Node::Glyph(gid));
    }

    TraversalContext context = base_context;
    context.SetStartNodes(start_nodes);
    context.table_filter = {glyf};
    context.node_type_filter = Node::NodeType::GLYPH;
    traversal_full.Merge(TRY(TraverseGraph(&context)));
  }

  // TODO XXXXXX phase 7

  return traversal_full;
}

void DependencyGraph::HandleUnicodeOutgoingEdges(
    hb_codepoint_t unicode,
    TraversalContext* context
) const {

  {
    auto it = unicode_to_gid_.find(unicode);
    if (it != unicode_to_gid_.end()) {
      context->TraverseEdgeTo(Node::Glyph(it->second));
    }
  }

  auto vs_edges = variation_selector_implied_edges_.find(unicode);
  if (vs_edges != variation_selector_implied_edges_.end()) {
    for (VariationSelectorEdge edge : vs_edges->second) {
      context->TraverseUvsEdge(unicode, edge.unicode, edge.gid);
    }
  }

  // The subsetter adds unicode bidi mirrors for any unicode codepoints,
  // so add a dep graph edge for those if they exist:
  auto unicode_funcs = hb_unicode_funcs_get_default ();
  hb_codepoint_t mirror = hb_unicode_mirroring(unicode_funcs, unicode);
  if (mirror != unicode) {
    context->TraverseEdgeTo(Node::Unicode(mirror));
  }
}

Status DependencyGraph::HandleGlyphOutgoingEdges(
    glyph_id_t gid,
    TraversalContext* context
) const {
  hb_codepoint_t index = 0;
  hb_tag_t table_tag = HB_CODEPOINT_INVALID;
  hb_codepoint_t dep_gid = HB_CODEPOINT_INVALID;
  hb_tag_t layout_tag = HB_CODEPOINT_INVALID;
  hb_codepoint_t ligature_set = HB_CODEPOINT_INVALID;
  hb_codepoint_t context_set = HB_CODEPOINT_INVALID;

  while (hb_depend_get_glyph_entry(dependency_graph_.get(), gid, index++, &table_tag,
                                   &dep_gid, &layout_tag, &ligature_set, &context_set)) {
    Node dest = Node::Glyph(dep_gid);
    if (table_tag == HB_TAG('G', 'S', 'U', 'B')) {
      if (context_set != HB_CODEPOINT_INVALID) {
        GlyphSet context_glyphs = TRY(GetContextSet(context_set));
        context->TraverseContextualEdgeTo(dep_gid, layout_tag, context_glyphs);
      } else if (ligature_set != HB_CODEPOINT_INVALID) {
        GlyphSet liga = TRY(GetLigaSet(ligature_set));
        context->TraverseLigatureEdgeTo(dep_gid, layout_tag, liga);
      } else {
        context->TraverseGsubEdgeTo(dep_gid, layout_tag);
      }
      continue;
    }

    if (table_tag == cmap && layout_tag != HB_CODEPOINT_INVALID) {
      // cmap edges are tracked in a separate structure and handled in HandleUnicodeOutgoingEdges.
      continue;
    } else {
      // Just a regular edge
      context->TraverseEdgeTo(dest, table_tag);
    }
  }

  return absl::OkStatus();
}

void DependencyGraph::HandleSegmentOutgoingEdges(
    segment_index_t id,
    TraversalContext* context
  ) const {

  if (id >= segmentation_info_->Segments().size()) {
    // Unknown segment has no outgoing edges.
    return;
  }

  const Segment& s = segmentation_info_->Segments().at(id);
  HandleSubsetDefinitionOutgoingEdges(s.Definition(), context);
}

void DependencyGraph::HandleSubsetDefinitionOutgoingEdges(
  const SubsetDefinition& subset_def,
  TraversalContext* context
) const {
  for (hb_codepoint_t u : subset_def.codepoints) {
    context->TraverseEdgeTo(Node::Unicode(u));
  }

  for (hb_tag_t f : subset_def.feature_tags) {
    context->TraverseEdgeTo(Node::Feature(f));
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

StatusOr<flat_hash_set<hb_tag_t>> DependencyGraph::FullFeatureSet(
    const RequestedSegmentationInformation* segmentation_info,
    hb_face_t* face) {
  hb_subset_input_t* input = hb_subset_input_create_or_fail();
  if (!input) {
    return absl::InternalError("Failed to create subset input object.");
  }

  // By extracting the feature list of the harfbuzz subset input we will also
  // include the features that harfbuzz adds by default.
  segmentation_info->FullDefinition().ConfigureInput(input, face);
  hb_set_t* features = hb_subset_input_set(input, HB_SUBSET_SETS_LAYOUT_FEATURE_TAG);

  flat_hash_set<hb_tag_t> out;
  hb_tag_t feature = HB_CODEPOINT_INVALID;
  while (hb_set_next(features, &feature)) {
    out.insert(feature);
  }

  hb_subset_input_destroy(input);

  return out;
}

StatusOr<flat_hash_set<hb_tag_t>> DependencyGraph::InitFeatureSet(
    const RequestedSegmentationInformation* segmentation_info,
    hb_face_t* face) {
  hb_subset_input_t* input = hb_subset_input_create_or_fail();
  if (!input) {
    return absl::InternalError("Failed to create subset input object.");
  }

  segmentation_info->InitFontSegment().ConfigureInput(input, face);
  hb_set_t* features = hb_subset_input_set(input, HB_SUBSET_SETS_LAYOUT_FEATURE_TAG);

  flat_hash_set<hb_tag_t> out;
  hb_tag_t feature = HB_CODEPOINT_INVALID;
  while (hb_set_next(features, &feature)) {
    out.insert(feature);
  }
  hb_subset_input_destroy(input);

  return out;
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

  // Only glyphs in the full closure are relevant.
  // TODO XXXXX this is probably not right... eg. what if something is unreachable because it's context
  // does not intersect the full closure? This would make it look reachable.
  glyphs.intersect(segmentation_info_->FullClosure());

  return glyphs;
}

flat_hash_map<hb_codepoint_t, std::vector<DependencyGraph::VariationSelectorEdge>> DependencyGraph::ComputeUVSEdges() const {
  flat_hash_map<hb_codepoint_t, std::vector<VariationSelectorEdge>> edges;
  for (auto [u, gid] : unicode_to_gid_) {
    hb_codepoint_t index = 0;
    hb_tag_t table_tag = HB_CODEPOINT_INVALID;
    hb_codepoint_t dep_gid = HB_CODEPOINT_INVALID;
    hb_codepoint_t variation_selector = HB_CODEPOINT_INVALID;
    hb_codepoint_t ligature_set = HB_CODEPOINT_INVALID;
    hb_codepoint_t context_set = HB_CODEPOINT_INVALID;
    while (hb_depend_get_glyph_entry(dependency_graph_.get(), gid, index++, &table_tag,
                                     &dep_gid, &variation_selector, &ligature_set, &context_set)) {
      if (table_tag != cmap) {
        continue;
      }

      // each UVS edge is two edges in reality, record both:
      edges[u].push_back(VariationSelectorEdge {
        .unicode = variation_selector,
        .gid = dep_gid,
      });
      edges[variation_selector].push_back(VariationSelectorEdge {
        .unicode = u,
        .gid = dep_gid,
      });
    }
  }

  return edges;
}

}  // namespace ift::dep_graph