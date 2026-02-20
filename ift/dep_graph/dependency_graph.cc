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

static bool HbSetIsSubset(const hb_set_t* glyphs, const GlyphSet& reached) {
  hb_codepoint_t g = HB_CODEPOINT_INVALID;
  while (hb_set_next(glyphs, &g)) {
    if (!reached.contains(g)) {
      return false;
    }
  }
  return true;
}

static bool HbSetIntersects(const hb_set_t* glyphs, const GlyphSet& reached) {
  hb_codepoint_t g = HB_CODEPOINT_INVALID;
  while (hb_set_next(glyphs, &g)) {
    if (reached.contains(g)) {
      return true;
    }
  }
  return false;
}

static StatusOr<bool> LigaSetSatisfied(hb_depend_t* depend, hb_codepoint_t liga_set, const GlyphSet& reached) {
  hb_set_unique_ptr liga_glyphs = make_hb_set();
  if (!hb_depend_get_set_from_index(depend, liga_set, liga_glyphs.get())) {
    return absl::InternalError("ConstraintsSatisfied(): Ligature set lookup failed.");
  }

  // All liga glyphs must be reached.
  return HbSetIsSubset(liga_glyphs.get(), reached);
}

static StatusOr<bool> ContextSetSatisfied(hb_depend_t* depend, hb_codepoint_t context_set_index, const GlyphSet& reached) {
  // the context set is actually a set of sets.
  hb_set_unique_ptr context_sets = make_hb_set();
  if (!hb_depend_get_set_from_index(depend, context_set_index, context_sets.get())) {
    return absl::InternalError("ContextSetSatisfied(): Context set lookup failed.");
  }

  GlyphSet glyphs;
  hb_codepoint_t set_id = HB_CODEPOINT_INVALID;
  while (hb_set_next(context_sets.get(), &set_id)) {
    if (set_id < 0x80000000) {
      // special case, set of one element.
      if (!reached.contains(set_id)) {
        return false;
      }
      continue;
    }

    hb_codepoint_t actual_set_id = set_id & 0x7FFFFFFF;
    hb_set_unique_ptr context_glyphs = make_hb_set();
    if (!hb_depend_get_set_from_index(depend, actual_set_id, context_glyphs.get())) {
      return absl::InternalError("Context sub set lookup failed.");
    }

    // need minimum of one glyph in each sub-group to be reached.
    if (!HbSetIntersects(context_glyphs.get(), reached)) {
      return false;
    }
  }
  return true;
}

// Tracks an edge who's context requirements are not yet satisfied.
class PendingEdge {
 public:
  static PendingEdge Uvs(hb_codepoint_t a, hb_codepoint_t b, glyph_id_t gid) {
    PendingEdge edge(Node::Glyph(gid), cmap);
    edge.required_codepoints_ = std::make_pair(a, b);
    return edge;
  }

  static PendingEdge Gsub(hb_tag_t feature, glyph_id_t gid) {
    PendingEdge edge(Node::Glyph(gid), GSUB);
    edge.required_feature_ = feature;
    return edge;
  }

  static PendingEdge Ligature(hb_tag_t feature, glyph_id_t gid, hb_codepoint_t liga_set_index) {
    PendingEdge edge(Node::Glyph(gid), GSUB);
    edge.required_feature_ = feature;
    edge.required_liga_set_index_ = liga_set_index;
    return edge;
  }

  static PendingEdge Context(hb_tag_t feature, glyph_id_t gid, hb_codepoint_t context_set_index) {
    PendingEdge edge(Node::Glyph(gid), GSUB);
    edge.required_feature_ = feature;
    edge.required_context_set_index_ = context_set_index;
    return edge;
  }

  std::optional<hb_tag_t> RequiredLayoutFeature() const {
    return required_feature_;
  }

  Status DoTraversal(TraversalContext& context) const;

  StatusOr<bool> ConstraintsSatisfied(
    hb_depend_t* depend,
    const CodepointSet& reached_unicodes,
    const GlyphSet& reached_glyphs,
    const flat_hash_set<hb_tag_t>& reached_features
  ) const {

    if (required_codepoints_.has_value() &&
        (!reached_unicodes.contains(required_codepoints_->first) ||
         !reached_unicodes.contains(required_codepoints_->second))) {
      return false;
    }

    if (required_feature_.has_value() && !reached_features.contains(*required_feature_)) {
      return false;
    }

    if (required_liga_set_index_.has_value() &&
        !TRY(LigaSetSatisfied(depend, *required_liga_set_index_, reached_glyphs))) {
      return false;
    }

    if (required_context_set_index_.has_value() &&
        !TRY(ContextSetSatisfied(depend, *required_context_set_index_, reached_glyphs))) {
      return false;
    }

    return true;
  }

 private:

  PendingEdge(Node dest, hb_tag_t table_tag) : dest_(dest), table_tag_(table_tag) {}

  Node dest_;
  hb_tag_t table_tag_;

  std::optional<hb_tag_t> required_feature_ = std::nullopt;
  std::optional<uint32_t> required_liga_set_index_ = std::nullopt;
  std::optional<uint32_t> required_context_set_index_ = std::nullopt;
  std::optional<std::pair<hb_codepoint_t, hb_codepoint_t>> required_codepoints_ = std::nullopt;
};

// Tracks the details of an inprogress traversal.
class TraversalContext {
 public:
  hb_depend_t* depend = nullptr;

  // Only edges from these tables will be followed.
  flat_hash_set<hb_tag_t> table_filter = {
    cmap,
    glyf,
    GSUB,
    COLR,
    MATH,
    CFF
  };

  // Only edges that originate from and end at glyphs from this filter will be followed.
  const GlyphSet* glyph_filter = nullptr;

  // For unicode based edges (unicode-unicode, unicode-gid), they will only be followed
  // when all unicodes are in this filter.
  const CodepointSet* unicode_filter = nullptr;

  // The set of all glyphs in the full closure.
  const GlyphSet* full_closure = nullptr;

  // For GSUB edges, they will only be followed when the features are in this filter.
  const flat_hash_set<hb_tag_t>* feature_filter = nullptr;

  // Only edges between node types in this filter will be followed, bitmask
  // using the NodeType enum values.
  uint32_t node_type_filter = 0xFFFFFFFF;

  // If true, then for conjunctive type edges (UVS/Ligature/Context) they will only
  // be followed when the context is satisfied (ie. appropriate glyphs are reached).
  bool enforce_context = false;

  // Results of the traversal.
  Traversal traversal;

  // Sets the nodes from which traversal starts.
  void SetStartNodes(const btree_set<Node>& start) {
    for (Node node : start) {
      Reached(node);
    }
  }

  // Preloads all of the reached glyphs/unicodes/features sets to be those in the init font
  // of segmentation_info.
  //
  // When context is enforced this will allow conjunctive edges that intersect the initial font
  // to be traversed.
  void SetReachedToInitFont(
    const RequestedSegmentationInformation& segmentation_info,
    const flat_hash_set<hb_tag_t>& init_features) {

    reached_glyphs_ = segmentation_info.InitFontGlyphs();
    reached_unicodes_ = segmentation_info.InitFontSegment().codepoints;
    reached_features_ = init_features;
  }

  // Returns the next node to be visited.
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
  StatusOr<bool> CheckPending(hb_depend_t* depend_graph) {
    auto it = pending_edges_.begin();
    while (it != pending_edges_.end()) {
      const auto& pending = *it;
      if (TRY(pending.ConstraintsSatisfied(depend_graph, reached_unicodes_, reached_glyphs_, reached_features_))) {
        TRYV(pending.DoTraversal(*this));
        pending_edges_.erase(it);
      } else {
        it++;
      }
    }

    return !next_.empty();
  }

  // Returns true if one or more pending edges remains.
  //
  // Pending edges are conjunctive edges which have been encountered who's conditions
  // are not yet satisfied.
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

    if (table_tag.has_value()) {
      traversal.Visit(dest, *table_tag);
    } else {
      traversal.Visit(dest);
    }
    Reached(dest);
  }

  // Traverse an edge with the associated PendingEdge.
  //
  // Will check if the pending edge is satisfied. If it is the edge will be traversed,
  // otherwise it will be added to the pending edge set.
  Status TraverseEdgeTo(Node dest, PendingEdge edge, hb_tag_t table_tag) {
    if (!table_filter.contains(table_tag)) {
      return absl::OkStatus();
    }

    if (!ShouldFollow(dest, edge.RequiredLayoutFeature())) {
      return absl::OkStatus();
    }

    if (enforce_context &&
        !TRY(edge.ConstraintsSatisfied(depend, reached_unicodes_, reached_glyphs_, reached_features_))) {
      Pending(edge);
    } else {
      TRYV(edge.DoTraversal(*this));
    }

    return absl::OkStatus();
  }

  Status TraverseUvsEdge(hb_codepoint_t a, hb_codepoint_t b, glyph_id_t gid) {
    bool can_reach_a = !unicode_filter || reached_unicodes_.contains(a) || unicode_filter->contains(a);
    bool can_reach_b = !unicode_filter || reached_unicodes_.contains(b) || unicode_filter->contains(b);
    if (enforce_context && (!can_reach_a || !can_reach_b)) {
      // edge can't be reached, ignore.
      return absl::OkStatus();
    }

    PendingEdge edge = PendingEdge::Uvs(a, b, gid);
    Node dest = Node::Glyph(gid);
    return TraverseEdgeTo(dest, edge, cmap);
  }

  Status TraverseGsubEdgeTo(glyph_id_t gid, hb_tag_t feature) {
    PendingEdge edge = PendingEdge::Gsub(feature, gid);
    Node dest = Node::Glyph(gid);
    return TraverseEdgeTo(dest, edge, GSUB);
  }

  Status TraverseContextualEdgeTo(glyph_id_t gid, hb_tag_t feature, hb_codepoint_t context_set) {
    if (!TRY(ContextSetSatisfied(depend, context_set, *full_closure))) {
      // Not possible for this edge to be activated so it can be ignored.
      return absl::OkStatus();
    }

    PendingEdge edge = PendingEdge::Context(feature, gid, context_set);
    Node dest = Node::Glyph(gid);
    return TraverseEdgeTo(dest, edge, GSUB);
  }

  Status TraverseLigatureEdgeTo(glyph_id_t gid, hb_tag_t feature, hb_codepoint_t liga_set_index) {
    if (!TRY(LigaSetSatisfied(depend, liga_set_index, *full_closure))) {
      // Not possible for this edge to be activated so it can be ignored.
      return absl::OkStatus();
    }

    PendingEdge edge = PendingEdge::Ligature(feature, gid, liga_set_index);
    Node dest = Node::Glyph(gid);
    return TraverseEdgeTo(dest, edge, GSUB);
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

  StatusOr<GlyphSet> GetContextSet(hb_codepoint_t context_set_id) const {
    // the context set is actually a set of sets.
    hb_set_unique_ptr context_sets = make_hb_set();
    if (!hb_depend_get_set_from_index(depend, context_set_id, context_sets.get())) {
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
      if (!hb_depend_get_set_from_index(depend, actual_set_id, context_glyphs.get())) {
        return absl::InternalError("Context sub set lookup failed.");
      }
      glyphs.union_from(context_glyphs.get());
    }

    // Only glyphs in the full closure are relevant.
    if (full_closure != nullptr) {
      glyphs.intersect(*full_closure);
    }

    return glyphs;
  }

 private:
  void Pending(PendingEdge edge) {
    pending_edges_.push_back(edge);
  }

  bool ShouldFollow(Node node, std::optional<hb_tag_t> layout_feature) const {
    if (!node.Matches(node_type_filter)) {
      return false;
    }

    if (feature_filter != nullptr && layout_feature.has_value() &&
        !feature_filter->contains(*layout_feature)) {
      return false;
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

Status PendingEdge::DoTraversal(TraversalContext& context) const {
  if (table_tag_ == GSUB && required_feature_.has_value()) {
    if (required_context_set_index_.has_value()) {
      GlyphSet context_glyphs = TRY(context.GetContextSet(*required_context_set_index_));
      context.traversal.VisitContextual(dest_, *required_feature_, context_glyphs);
    } else {
      context.traversal.VisitGsub(dest_, *required_feature_);
    }
  } else {
    context.traversal.Visit(dest_, table_tag_);
  }

  context.Reached(dest_);
  return absl::OkStatus();
}

StatusOr<Traversal> DependencyGraph::TraverseGraph(TraversalContext* context) const {
  VLOG(1) << "DependencyGraph::TraverseGraph(...)";

  while (true) {
    std::optional<Node> next = context->GetNext();
    if (!next.has_value()) {
      if (TRY(context->CheckPending(dependency_graph_.get()))) {
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
      TRYV(HandleUnicodeOutgoingEdges(next->Id(), context));
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

StatusOr<Traversal> DependencyGraph::ClosureTraversal(
  const SegmentSet& start,
  bool enforce_context
) const {
  btree_set<Node> start_nodes;
  for (segment_index_t s : start) {
    start_nodes.insert(Node::Segment(s));
  }
  return ClosureTraversal(start_nodes, nullptr, nullptr, enforce_context);
}

StatusOr<Traversal> DependencyGraph::ClosureTraversal(
  const absl::btree_set<Node>& nodes,
  const GlyphSet* glyph_filter_ptr,
  const CodepointSet* unicode_filter_ptr,
  bool enforce_context
) const {

  // TODO(garretrieger): context edges don't have edges for each participating glyph,
  // so for full correctness in matching closure we should introduce pending edges for
  // any unsatisfied edges out of the init font. However, this behaviour will probably
  // need to be optional as it's not desirable for the current dependency closure use
  // cases which specifically ignore context as inaccurate, but would be needed if
  // we eventually want to try and handle some context cases in accurate analysis.
  CodepointSet non_init_font_codepoints;
  if (unicode_filter_ptr == nullptr) {
    non_init_font_codepoints = segmentation_info_->FullDefinition().codepoints;
    non_init_font_codepoints.subtract(segmentation_info_->InitFontSegment().codepoints);
  }

  GlyphSet non_init_font_glyphs;
  if (glyph_filter_ptr == nullptr) {
    non_init_font_glyphs = segmentation_info_->NonInitFontGlyphs();
  }

  flat_hash_set<hb_tag_t> table_tags = FontHelper::GetTags(original_face_.get());
  Traversal traversal_full;

  // Subsetting closure happens in phases which we need to mimic here:
  // 1. Unicode closure (bidi)
  // 2. Unicode to glyph (cmap + UVS)
  // 3. GSUB glyph closure
  // 4. MATH closure.
  // 5. COLR closure
  // 6. glyf closure
  // 7. CFF closure
  //
  // Reference for the phases and ordering:
  // _populate_gids_to_retain() from https://github.com/harfbuzz/harfbuzz/blob/main/src/hb-subset-plan.cc#L439

  TraversalContext base_context;
  base_context.depend = dependency_graph_.get();
  base_context.unicode_filter = unicode_filter_ptr ? unicode_filter_ptr : &non_init_font_codepoints;
  base_context.glyph_filter = glyph_filter_ptr ? glyph_filter_ptr : &non_init_font_glyphs;
  base_context.full_closure = &segmentation_info_->FullClosure();
  base_context.feature_filter = &full_feature_set_;
  base_context.enforce_context = enforce_context;
  if (enforce_context) {
    base_context.SetReachedToInitFont(*segmentation_info_, TRY(InitFontFeatureSet()));
  }

  /* ### Phase 1 + 2: Unicode and Unicode to glyph */
  {
    TraversalContext context = base_context;
    context.SetStartNodes(nodes);
    context.table_filter = {cmap};
    context.node_type_filter =
      Node::NodeType::INIT_FONT |
      Node::NodeType::SEGMENT |
      Node::NodeType::UNICODE |
      Node::NodeType::GLYPH |
      Node::NodeType::FEATURE;
    traversal_full = TRY(TraverseGraph(&context));
  }

  /* ### Phase 3: GSUB ### */
  if (table_tags.contains(GSUB)) {
    TRYV(ClosureSubTraversal(&base_context, GSUB, traversal_full));
  }

  /* ### Phase 4: MATH ### */
  if (table_tags.contains(MATH)) {
    TRYV(ClosureSubTraversal(&base_context, MATH, traversal_full));
  }

  /* ### Phase 5: COLR ### */
  if (table_tags.contains(COLR)) {
    TRYV(ClosureSubTraversal(&base_context, COLR, traversal_full));
  }

  /* ### Phase 6: glyf ### */
  if (table_tags.contains(glyf)) {
    TRYV(ClosureSubTraversal(&base_context, glyf, traversal_full));
  }

  /* ### Phase 7: CFF ### */
  if (table_tags.contains(CFF)) {
    TRYV(ClosureSubTraversal(&base_context, CFF, traversal_full));
  }

  return traversal_full;
}

Status DependencyGraph::ClosureSubTraversal(
  const TraversalContext* base_context,
  hb_tag_t table,
  Traversal& traversal_full) const {
  btree_set<Node> start_nodes;
  for (glyph_id_t gid : traversal_full.ReachedGlyphs()) {
    start_nodes.insert(Node::Glyph(gid));
  }

  TraversalContext context = *base_context;
  context.SetStartNodes(start_nodes);
  context.table_filter = {table};
  context.node_type_filter = Node::NodeType::GLYPH;
  traversal_full.Merge(TRY(TraverseGraph(&context)));
  return absl::OkStatus();
}

Status DependencyGraph::HandleUnicodeOutgoingEdges(
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
      TRYV(context->TraverseUvsEdge(unicode, edge.unicode, edge.gid));
    }
  }

  // The subsetter adds unicode bidi mirrors for any unicode codepoints,
  // so add a dep graph edge for those if they exist:
  auto unicode_funcs = hb_unicode_funcs_get_default ();
  hb_codepoint_t mirror = hb_unicode_mirroring(unicode_funcs, unicode);
  if (mirror != unicode) {
    context->TraverseEdgeTo(Node::Unicode(mirror));
  }

  return absl::OkStatus();
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
                                   &dep_gid, &layout_tag, &ligature_set, &context_set, nullptr /* flags */)) {
    Node dest = Node::Glyph(dep_gid);
    if (table_tag == HB_TAG('G', 'S', 'U', 'B')) {
      if (context_set != HB_CODEPOINT_INVALID) {
        TRYV(context->TraverseContextualEdgeTo(dep_gid, layout_tag, context_set));
      } else if (ligature_set != HB_CODEPOINT_INVALID) {
        TRYV(context->TraverseLigatureEdgeTo(dep_gid, layout_tag, ligature_set));
      } else {
        TRYV(context->TraverseGsubEdgeTo(dep_gid, layout_tag));
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
                                     &dep_gid, &variation_selector, &ligature_set, &context_set, nullptr /* flags */)) {
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