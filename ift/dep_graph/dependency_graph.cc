#include "ift/dep_graph/dependency_graph.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "ift/common/font_data.h"
#include "ift/common/font_helper.h"
#include "ift/common/hb_set_unique_ptr.h"
#include "ift/common/int_set.h"
#include "ift/dep_graph/node.h"
#include "ift/dep_graph/pending_edge.h"
#include "ift/dep_graph/traversal.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/types.h"

using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::Status;
using absl::StatusOr;
using ift::common::CodepointSet;
using ift::common::FontHelper;
using ift::common::GlyphSet;
using ift::common::hb_font_unique_ptr;
using ift::common::hb_set_unique_ptr;
using ift::common::IntSet;
using ift::common::make_hb_font;
using ift::common::make_hb_set;
using ift::common::SegmentSet;
using ift::encoder::glyph_id_t;
using ift::encoder::RequestedSegmentationInformation;
using ift::encoder::Segment;
using ift::encoder::segment_index_t;
using ift::encoder::SubsetDefinition;

namespace ift::dep_graph {

StatusOr<GlyphSet> GetContextSet(hb_depend_t* depend,
                                 const ift::common::GlyphSet* full_closure,
                                 hb_codepoint_t context_set_id) {
  // the context set is actually a set of sets.
  hb_set_unique_ptr context_sets = make_hb_set();
  if (!hb_depend_get_set_from_index(depend, context_set_id,
                                    context_sets.get())) {
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
    if (!hb_depend_get_set_from_index(depend, actual_set_id,
                                      context_glyphs.get())) {
      return absl::InternalError("Context sub set lookup failed.");
    }
    glyphs.union_from(context_glyphs.get());
  }

  // Only glyphs in the full closure are relevant.
  if (full_closure) {
    glyphs.intersect(*full_closure);
  }

  return glyphs;
}

// Tracks the details of an inprogress traversal.
template <typename CallbackT>
class TraversalContext {
 public:
  hb_depend_t* depend = nullptr;

  // Only edges from these tables will be followed.
  flat_hash_set<hb_tag_t> table_filter = {FontHelper::kCmap, FontHelper::kGlyf,
                                          FontHelper::kGSUB, FontHelper::kCOLR,
                                          FontHelper::kMATH, FontHelper::kCFF};

  // Only edges that originate from and end at glyphs from this filter will be
  // followed.
  const GlyphSet* glyph_filter = nullptr;

  // For unicode based edges (unicode-unicode, unicode-gid), they will only be
  // followed when all unicodes are in this filter.
  const CodepointSet* unicode_filter = nullptr;

  // The set of all glyphs in the full closure.
  const GlyphSet* full_closure = nullptr;

  // For GSUB edges, they will only be followed when the features are in this
  // filter.
  const flat_hash_set<hb_tag_t>* feature_filter = nullptr;

  // Only edges between node types in this filter will be followed, bitmask
  // using the NodeType enum values.
  uint32_t node_type_filter = 0xFFFFFFFF;

  // If true, then for conjunctive type edges (UVS/Ligature/Context) they will
  // only be followed when the context is satisfied (ie. appropriate glyphs are
  // reached).
  bool enforce_context = false;

  // Informed of the results of traversal
  CallbackT callback;

 private:
  // These track information about things that are needed to
  // activate context which is not yet seen.
  std::vector<PendingEdge> pending_edges_;

  CodepointSet reached_unicodes_;
  GlyphSet reached_glyphs_;
  flat_hash_set<hb_tag_t> reached_features_;

  // To avoid creating and destroying sets every time we pull a liga or context
  // set from the dep graph api store a scratch set that's reused.
  hb_set_unique_ptr scratch_set_ = make_hb_set();
  hb_set_unique_ptr scratch_set_aux_ = make_hb_set();

 public:
  TraversalContext() = default;

  TraversalContext(const TraversalContext& other) {
    depend = other.depend;
    table_filter = other.table_filter;
    glyph_filter = other.glyph_filter;
    unicode_filter = other.unicode_filter;
    full_closure = other.full_closure;
    feature_filter = other.feature_filter;
    node_type_filter = other.node_type_filter;
    enforce_context = other.enforce_context;
    callback = other.callback;

    pending_edges_ = other.pending_edges_;
    reached_unicodes_ = other.reached_unicodes_;
    reached_glyphs_ = other.reached_glyphs_;
    reached_features_ = other.reached_features_;
  }

  // Sets the nodes from which traversal starts.
  void SetReached(const btree_set<Node>& start) {
    for (Node node : start) {
      Reached(node);
    }
  }

  // Preloads all of the reached glyphs/unicodes/features sets to be those in
  // the init font of segmentation_info.
  //
  // When context is enforced this will allow conjunctive edges that intersect
  // the initial font to be traversed.
  void SetReachedToInitFont(
      const RequestedSegmentationInformation& segmentation_info,
      const flat_hash_set<hb_tag_t>& init_features) {
    reached_glyphs_ = segmentation_info.InitFontGlyphs();
    reached_unicodes_ = segmentation_info.InitFontSegment().codepoints;
    reached_features_ = init_features;
  }

  // Returns the next node to be visited.

  // This checks all pending edges and if any have their constraints satisfied
  // then they are traversed. Returns true if there are now more nodes in the
  // next queue.
  StatusOr<bool> CheckPending(hb_depend_t* depend_graph);

  // Returns true if one or more pending edges remains.
  //
  // Pending edges are conjunctive edges which have been encountered who's
  // conditions are not yet satisfied.
  bool HasPendingEdges() const { return !pending_edges_.empty(); }

  const std::vector<PendingEdge>& pending_edges() const {
    return pending_edges_;
  }

  // Traverse an edge with no special context and/or additional information
  // other than table tag
  void TraverseEdgeTo(Node source, Node dest,
                      std::optional<hb_tag_t> table_tag = std::nullopt) {
    if (!ShouldFollow(dest, table_tag, std::nullopt)) {
      return;
    }

    callback.VisitPending(PendingEdge::Disjunctive(
        source, dest, table_tag.value_or(HB_TAG(' ', ' ', ' ', ' '))));

    if (table_tag.has_value()) {
      callback.Visit(dest, *table_tag);
    } else {
      callback.Visit(dest);
    }
    Reached(dest);
  }

  // Traverse an edge with the associated PendingEdge.
  //
  // Will check if the pending edge is satisfied. If it is the edge will be
  // traversed, otherwise it will be added to the pending edge set.
  Status TraverseEdgeTo(Node dest, PendingEdge edge, hb_tag_t table_tag);

  Status TraverseUvsEdge(hb_codepoint_t a, hb_codepoint_t b, glyph_id_t gid) {
    bool can_reach_a = !unicode_filter || reached_unicodes_.contains(a) ||
                       unicode_filter->contains(a);
    bool can_reach_b = !unicode_filter || reached_unicodes_.contains(b) ||
                       unicode_filter->contains(b);
    if (enforce_context && (!can_reach_a || !can_reach_b)) {
      // edge can't be reached, ignore.
      return absl::OkStatus();
    }

    PendingEdge edge = PendingEdge::Uvs(a, b, gid);
    Node dest = Node::Glyph(gid);
    return TraverseEdgeTo(dest, edge, FontHelper::kCmap);
  }

  Status TraverseGsubEdgeTo(glyph_id_t source_gid, glyph_id_t dest_gid,
                            hb_tag_t feature) {
    PendingEdge edge = PendingEdge::Gsub(source_gid, feature, dest_gid);
    Node dest = Node::Glyph(dest_gid);
    return TraverseEdgeTo(dest, edge, FontHelper::kGSUB);
  }

  Status TraverseContextualEdgeTo(glyph_id_t source_gid, glyph_id_t dest_gid,
                                  hb_tag_t feature,
                                  hb_codepoint_t context_set) {
    if (!TRY(ContextSetSatisfied(context_set, *full_closure))) {
      // Not possible for this edge to be activated so it can be ignored.
      return absl::OkStatus();
    }

    PendingEdge edge =
        PendingEdge::Context(source_gid, feature, dest_gid, context_set);
    Node dest = Node::Glyph(dest_gid);
    return TraverseEdgeTo(dest, edge, FontHelper::kGSUB);
  }

  Status TraverseLigatureEdgeTo(glyph_id_t source_gid, glyph_id_t dest_gid,
                                hb_tag_t feature,
                                hb_codepoint_t liga_set_index) {
    // TODO(garretrieger): can we use glyph filter here instead for more
    // aggressive filtering?
    if (!TRY(LigaSetSatisfied(liga_set_index, *full_closure))) {
      // Not possible for this edge to be activated so it can be ignored.
      return absl::OkStatus();
    }

    PendingEdge edge =
        PendingEdge::Ligature(source_gid, feature, dest_gid, liga_set_index);
    Node dest = Node::Glyph(dest_gid);
    return TraverseEdgeTo(dest, edge, FontHelper::kGSUB);
  }

  void Reached(Node node) {
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

 private:
  StatusOr<bool> LigaSetSatisfied(hb_codepoint_t liga_set,
                                  const GlyphSet& reached) {
    if (!hb_depend_get_set_from_index(depend, liga_set, scratch_set_.get())) {
      return absl::InternalError(
          "ConstraintsSatisfied(): Ligature set lookup failed.");
    }

    // All liga glyphs must be reached.
    return IntSet::is_subset_of(scratch_set_.get(), reached);
  }

  StatusOr<bool> ContextSetSatisfied(hb_codepoint_t context_set_index,
                                     const GlyphSet& reached) {
    // the context set is actually a set of sets.
    if (!hb_depend_get_set_from_index(depend, context_set_index,
                                      scratch_set_.get())) {
      return absl::InternalError(
          "ContextSetSatisfied(): Context set lookup failed.");
    }

    GlyphSet glyphs;
    hb_codepoint_t set_id = HB_CODEPOINT_INVALID;
    while (hb_set_next(scratch_set_.get(), &set_id)) {
      if (set_id < 0x80000000) {
        // special case, set of one element.
        if (!reached.contains(set_id)) {
          return false;
        }
        continue;
      }

      hb_codepoint_t actual_set_id = set_id & 0x7FFFFFFF;
      if (!hb_depend_get_set_from_index(depend, actual_set_id,
                                        scratch_set_aux_.get())) {
        return absl::InternalError("Context sub set lookup failed.");
      }

      // need minimum of one glyph in each sub-group to be reached.
      if (!reached.intersects(scratch_set_aux_.get())) {
        return false;
      }
    }
    return true;
  }

  StatusOr<bool> ConstraintsSatisfied(
      const PendingEdge& edge, const CodepointSet& reached_unicodes,
      const GlyphSet& reached_glyphs,
      const flat_hash_set<hb_tag_t>& reached_features) {
    if (edge.source.IsFeature() &&
        !reached_features.contains(edge.source.Id())) {
      return false;
    }
    if (edge.source.IsUnicode() &&
        !reached_unicodes.contains(edge.source.Id())) {
      return false;
    }
    if (edge.source.IsGlyph() && !reached_glyphs.contains(edge.source.Id())) {
      return false;
    }

    if (edge.required_codepoints.has_value() &&
        (!reached_unicodes.contains(edge.required_codepoints->first) ||
         !reached_unicodes.contains(edge.required_codepoints->second))) {
      return false;
    }

    if (edge.required_feature.has_value() &&
        !reached_features.contains(*edge.required_feature)) {
      return false;
    }

    if (edge.required_liga_set_index.has_value() &&
        !TRY(LigaSetSatisfied(*edge.required_liga_set_index, reached_glyphs))) {
      return false;
    }

    if (edge.required_context_set_index.has_value() &&
        !TRY(ContextSetSatisfied(*edge.required_context_set_index,
                                 reached_glyphs))) {
      return false;
    }

    return true;
  }

  void Pending(PendingEdge edge) { pending_edges_.push_back(edge); }

  bool ShouldFollow(Node node, std::optional<hb_tag_t> table_tag,
                    std::optional<hb_tag_t> layout_feature) const {
    if (!node.Matches(node_type_filter)) {
      return false;
    }

    if (table_tag.has_value() && !table_filter.contains(*table_tag)) {
      return false;
    }

    if (feature_filter != nullptr && layout_feature.has_value() &&
        !feature_filter->contains(*layout_feature)) {
      return false;
    }

    if (glyph_filter != nullptr && node.IsGlyph()) {
      return glyph_filter->contains(node.Id());
    }

    if (unicode_filter != nullptr && node.IsUnicode()) {
      return unicode_filter->contains(node.Id());
    }

    if (feature_filter != nullptr && node.IsFeature()) {
      return feature_filter->contains(node.Id());
    }

    return true;
  }
};

void DependencyGraph::ClosureState::Visit(Node dest) {
  traversal.Visit(dest);
  Reached(dest);
}

void DependencyGraph::ClosureState::Visit(Node dest, hb_tag_t table) {
  traversal.Visit(dest, table);
  Reached(dest);
}

void DependencyGraph::ClosureState::VisitGsub(Node dest, hb_tag_t feature) {
  traversal.VisitGsub(dest, feature);
  Reached(dest);
}

void DependencyGraph::ClosureState::VisitContextual(Node dest, hb_tag_t feature,
                                                    GlyphSet context_glyphs) {
  traversal.VisitContextual(dest, feature, context_glyphs);
  Reached(dest);
}

std::optional<Node> DependencyGraph::ClosureState::GetNext() {
  if (next.empty()) {
    return std::nullopt;
  }
  Node node = next.back();
  next.pop_back();
  return node;
}

bool DependencyGraph::ClosureState::Reached(Node node) {
  auto [_, did_insert] = visited.insert(node);
  if (!did_insert) {
    return false;
  }

  next.push_back(node);
  return true;
}

void DependencyGraph::ClosureState::SetStartNodes(
    const btree_set<Node>& start) {
  for (Node node : start) {
    Reached(node);
  }
}

template <typename CallbackT>
static Status DoTraversal(const PendingEdge& edge,
                          TraversalContext<CallbackT>& context) {
  context.callback.VisitPending(edge);

  if (edge.table_tag == FontHelper::kGSUB &&
      edge.required_feature.has_value()) {
    if (edge.required_context_set_index.has_value()) {
      GlyphSet context_glyphs =
          TRY(GetContextSet(context.depend, context.full_closure,
                            *edge.required_context_set_index));
      context.callback.VisitContextual(edge.dest, *edge.required_feature,
                                       context_glyphs);
    } else {
      context.callback.VisitGsub(edge.dest, *edge.required_feature);
    }
  } else {
    context.callback.Visit(edge.dest, edge.table_tag);
  }

  context.Reached(edge.dest);
  return absl::OkStatus();
}

template <typename CallbackT>
StatusOr<bool> TraversalContext<CallbackT>::CheckPending(
    hb_depend_t* depend_graph) {
  bool did_work = false;
  auto it = pending_edges_.begin();
  while (it != pending_edges_.end()) {
    const auto& pending = *it;
    if (TRY(ConstraintsSatisfied(pending, reached_unicodes_, reached_glyphs_,
                                 reached_features_))) {
      // Edge contstraints are now satisfied, can traverse the edge.
      TRYV(DoTraversal(pending, *this));
      pending_edges_.erase(it);
      did_work = true;
    } else {
      it++;
    }
  }

  return did_work;
}

template <typename CallbackT>
Status TraversalContext<CallbackT>::TraverseEdgeTo(Node dest, PendingEdge edge,
                                                   hb_tag_t table_tag) {
  if (!ShouldFollow(dest, table_tag, edge.required_feature)) {
    return absl::OkStatus();
  }

  if (enforce_context &&
      !TRY(ConstraintsSatisfied(edge, reached_unicodes_, reached_glyphs_,
                                reached_features_))) {
    Pending(edge);
  } else {
    TRYV(DoTraversal(edge, *this));
  }

  return absl::OkStatus();
}

template <typename CallbackT>
absl::Status DependencyGraph::HandleOutgoingEdges(
    Node node, TraversalContext<CallbackT>* context) const {
  if (node.IsGlyph()) {
    TRYV(HandleGlyphOutgoingEdges(node.Id(), context));
  } else if (node.IsUnicode()) {
    TRYV(HandleUnicodeOutgoingEdges(node.Id(), context));
  } else if (node.IsSegment()) {
    HandleSegmentOutgoingEdges(node.Id(), context);
  } else if (node.IsFeature()) {
    TRYV(HandleFeatureOutgoingEdges(node.Id(), context));
  } else if (node.IsInitFont()) {
    HandleSubsetDefinitionOutgoingEdges(
        node, segmentation_info_->InitFontSegment(), context);
  }
  return absl::OkStatus();
}

StatusOr<std::vector<std::vector<Node>>>
DependencyGraph::StronglyConnectedComponents(
    const flat_hash_set<hb_tag_t>& table_filter,
    uint32_t node_type_filter) const {
  struct Callback {
    std::vector<Node> edges;
    void Visit(Node) {}
    void Visit(Node, hb_tag_t) {}
    void VisitGsub(Node, hb_tag_t) {}
    void VisitContextual(Node, hb_tag_t, GlyphSet) {}
    void VisitPending(const PendingEdge& pe) { edges.push_back(pe.dest); }
  };

  CodepointSet non_init_font_codepoints =
      segmentation_info_->FullDefinition().codepoints;
  non_init_font_codepoints.subtract(
      segmentation_info_->InitFontSegment().codepoints);

  flat_hash_set<hb_tag_t> non_init_font_features = full_feature_set_;
  for (hb_tag_t tag : segmentation_info_->InitFontSegment().feature_tags) {
    non_init_font_features.erase(tag);
  }

  GlyphSet non_init_font_glyphs = segmentation_info_->NonInitFontGlyphs();

  TraversalContext<Callback> context;
  context.depend = dependency_graph_.get();
  context.full_closure = &segmentation_info_->FullClosure();
  context.enforce_context = false;
  context.unicode_filter = &non_init_font_codepoints;
  context.glyph_filter = &non_init_font_glyphs;
  context.feature_filter = &full_feature_set_;
  context.table_filter = table_filter;
  context.node_type_filter = node_type_filter;

  // Implementation based on
  // https://en.wikipedia.org/wiki/Tarjan%27s_strongly_connected_components_algorithm
  // Modified to use stack instead of recursion.
  struct Meta {
    int64_t index = -1;
    int64_t lowlink = -1;
    bool on_stack = false;
  };

  flat_hash_map<Node, Meta> node_meta;
  std::vector<Node> stack;

  int64_t index = 0;
  std::vector<std::vector<Node>> sccs;

  struct DfsState {
    Node node;
    std::vector<Node> edges;
  };

  auto strongconnect = [&](Node start_node) -> Status {
    std::vector<DfsState> dfs_stack = {{start_node, {}}};

    while (!dfs_stack.empty()) {
      Node v = dfs_stack.back().node;

      std::vector<Node>& edges = dfs_stack.back().edges;
      {
        // Scope v_meta to where it's still valid
        auto& v_meta = node_meta[v];
        if (v_meta.index == -1) {
          // first time time visiting this node, set up initial state.
          v_meta.index = v_meta.lowlink = index++;
          stack.push_back(v);
          v_meta.on_stack = true;
          context.callback.edges.clear();
          TRYV(HandleOutgoingEdges(v, &context));
          edges = std::move(context.callback.edges);
        }
      }

      bool recursing = false;
      while (!edges.empty()) {
        Node w = edges.back();
        edges.pop_back();

        auto& w_meta = node_meta[w];
        if (w_meta.index == -1) {
          // recurse strongconnect()
          dfs_stack.push_back({w, {}});
          recursing = true;
          break;
        } else if (w_meta.on_stack) {
          // Successor w is in stack and hence in the current SCC
          auto& v_meta = node_meta[v];
          v_meta.lowlink = std::min(v_meta.lowlink, w_meta.index);
        }
      }

      if (recursing) continue;

      const auto& v_meta = node_meta.at(v);
      // If v is a root node, pop the stack and generate an SCC
      if (v_meta.lowlink == v_meta.index) {
        std::vector<Node> scc;
        while (true) {
          Node node = stack.back();
          stack.pop_back();
          node_meta.at(node).on_stack = false;
          scc.push_back(node);
          if (node == v) break;
        }
        sccs.push_back(std::move(scc));
      }

      dfs_stack.pop_back();
      if (!dfs_stack.empty()) {
        Node parent = dfs_stack.back().node;
        auto& parent_meta = node_meta.at(parent);
        parent_meta.lowlink = std::min(parent_meta.lowlink, v_meta.lowlink);
      }
    }
    return absl::OkStatus();
  };

  /* ### Run strongconnect for every possible, non init font node. #### */
  for (segment_index_t s : segmentation_info_->NonEmptySegments()) {
    Node n = Node::Segment(s);
    if (!node_meta.contains(n)) {
      TRYV(strongconnect(n));
    }
  }

  for (hb_codepoint_t u : non_init_font_codepoints) {
    Node n = Node::Unicode(u);
    if (!node_meta.contains(n)) {
      TRYV(strongconnect(n));
    }
  }

  for (hb_tag_t tag : non_init_font_features) {
    Node n = Node::Feature(tag);
    if (!node_meta.contains(n)) {
      TRYV(strongconnect(n));
    }
  }

  for (glyph_id_t gid : non_init_font_glyphs) {
    Node n = Node::Glyph(gid);
    if (!node_meta.contains(n)) {
      TRYV(strongconnect(n));
    }
  }

  // Tarjan's returns SCCs in reverse topological order, reverse to get
  // topological order.
  std::reverse(sccs.begin(), sccs.end());
  return sccs;
}

StatusOr<Traversal> DependencyGraph::TraverseGraph(
    TraversalContext<ClosureState>* context) const {
  VLOG(1) << "DependencyGraph::TraverseGraph(...)";

  while (true) {
    std::optional<Node> next = context->callback.GetNext();
    if (!next.has_value()) {
      if (TRY(context->CheckPending(dependency_graph_.get())) &&
          !context->callback.next.empty()) {
        continue;
      } else {
        // nothing left to traverse.
        break;
      }
    }

    TRYV(HandleOutgoingEdges(*next, context));
  }

  for (const auto& edge : context->pending_edges()) {
    if (!context->callback.visited.contains(edge.dest)) {
      // Only output pending edges where the dest has not been reached
      context->callback.traversal.AddPending(edge);
    }
  }

  return std::move(context->callback.traversal);
}

StatusOr<Traversal> DependencyGraph::ClosureTraversal(
    const SegmentSet& start, bool enforce_context) const {
  btree_set<Node> start_nodes;
  for (segment_index_t s : start) {
    start_nodes.insert(Node::Segment(s));
  }
  return ClosureTraversal(start_nodes, nullptr, nullptr, enforce_context);
}

StatusOr<Traversal> DependencyGraph::ClosureTraversal(
    const btree_set<Node>& nodes, const GlyphSet* glyph_filter_ptr,
    const CodepointSet* unicode_filter_ptr, bool enforce_context) const {
  // TODO(garretrieger): context edges don't have edges for each participating
  // glyph, so for full correctness in matching closure we should introduce
  // pending edges for any unsatisfied edges out of the init font. However, this
  // behaviour will probably need to be optional as it's not desirable for the
  // current dependency closure use cases which specifically ignore context as
  // inaccurate, but would be needed if we eventually want to try and handle
  // some context cases in accurate analysis.
  CodepointSet non_init_font_codepoints;
  if (unicode_filter_ptr == nullptr) {
    non_init_font_codepoints = segmentation_info_->FullDefinition().codepoints;
    non_init_font_codepoints.subtract(
        segmentation_info_->InitFontSegment().codepoints);
  }

  GlyphSet non_init_font_glyphs;
  if (glyph_filter_ptr == nullptr) {
    non_init_font_glyphs = segmentation_info_->NonInitFontGlyphs();
  }

  flat_hash_set<hb_tag_t> table_tags =
      FontHelper::GetTags(original_face_.get());
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
  // _populate_gids_to_retain() from
  // https://github.com/harfbuzz/harfbuzz/blob/main/src/hb-subset-plan.cc#L439

  TraversalContext<ClosureState> base_context;
  base_context.depend = dependency_graph_.get();
  base_context.unicode_filter =
      unicode_filter_ptr ? unicode_filter_ptr : &non_init_font_codepoints;
  base_context.glyph_filter =
      glyph_filter_ptr ? glyph_filter_ptr : &non_init_font_glyphs;
  base_context.full_closure = &segmentation_info_->FullClosure();
  base_context.feature_filter = &full_feature_set_;
  base_context.enforce_context = enforce_context;
  if (enforce_context) {
    base_context.SetReachedToInitFont(*segmentation_info_,
                                      TRY(InitFontFeatureSet()));
  }

  /* ### Phase 1 + 2: Unicode and Unicode to glyph */
  {
    TraversalContext context = base_context;
    context.SetReached(nodes);
    context.callback.SetStartNodes(nodes);
    context.table_filter = {DependencyGraph::kClosurePhaseTable[0]};
    context.node_type_filter = DependencyGraph::kClosurePhaseNodeFilter[0];
    traversal_full = TRY(TraverseGraph(&context));
  }

  /* ### Remaining Phases ### */
  for (unsigned phase = 1; phase < DependencyGraph::kNumberOfClosurePhases;
       phase++) {
    if (table_tags.contains(DependencyGraph::kClosurePhaseTable[phase])) {
      TRYV(ClosureSubTraversal(&base_context, phase, traversal_full));
    }
  }

  return traversal_full;
}

Status DependencyGraph::ClosureSubTraversal(
    const TraversalContext<ClosureState>* base_context, uint32_t phase_index,
    Traversal& traversal_full) const {
  btree_set<Node> start_nodes;

  if (DependencyGraph::kClosurePhaseStartNodes[phase_index] &
      Node::NodeType::GLYPH) {
    for (glyph_id_t gid : traversal_full.ReachedGlyphs()) {
      start_nodes.insert(Node::Glyph(gid));
    }
  }

  if (DependencyGraph::kClosurePhaseStartNodes[phase_index] &
      Node::NodeType::FEATURE) {
    for (hb_tag_t feature : traversal_full.ReachedLayoutFeatures()) {
      start_nodes.insert(Node::Feature(feature));
    }
  }

  TraversalContext<ClosureState> context = *base_context;
  context.SetReached(start_nodes);
  context.callback.SetStartNodes(start_nodes);
  context.table_filter = {DependencyGraph::kClosurePhaseTable[phase_index]};
  context.node_type_filter =
      DependencyGraph::kClosurePhaseNodeFilter[phase_index];
  traversal_full.Merge(TRY(TraverseGraph(&context)));
  return absl::OkStatus();
}

template <typename CallbackT>
Status DependencyGraph::HandleUnicodeOutgoingEdges(
    hb_codepoint_t unicode, TraversalContext<CallbackT>* context) const {
  {
    auto it = unicode_to_gid_.find(unicode);
    if (it != unicode_to_gid_.end()) {
      context->TraverseEdgeTo(Node::Unicode(unicode), Node::Glyph(it->second));
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
  auto unicode_funcs = hb_unicode_funcs_get_default();
  hb_codepoint_t mirror = hb_unicode_mirroring(unicode_funcs, unicode);
  if (mirror != unicode) {
    context->TraverseEdgeTo(Node::Unicode(unicode), Node::Unicode(mirror));
  }

  return absl::OkStatus();
}

template <typename CallbackT>
Status DependencyGraph::HandleGlyphOutgoingEdges(
    glyph_id_t gid, TraversalContext<CallbackT>* context) const {
  hb_codepoint_t index = 0;
  hb_tag_t table_tag = HB_CODEPOINT_INVALID;
  hb_codepoint_t dep_gid = HB_CODEPOINT_INVALID;
  hb_tag_t layout_tag = HB_CODEPOINT_INVALID;
  hb_codepoint_t ligature_set = HB_CODEPOINT_INVALID;
  hb_codepoint_t context_set = HB_CODEPOINT_INVALID;

  while (hb_depend_get_glyph_entry(
      dependency_graph_.get(), gid, index++, &table_tag, &dep_gid, &layout_tag,
      &ligature_set, &context_set, nullptr /* flags */)) {
    if (context->glyph_filter != nullptr &&
        !context->glyph_filter->contains(dep_gid)) {
      continue;
    }

    Node dest = Node::Glyph(dep_gid);
    if (table_tag == HB_TAG('G', 'S', 'U', 'B')) {
      TRYV(HandleGsubGlyphOutgoingEdges(gid, dep_gid, layout_tag, ligature_set,
                                        context_set, context));
      continue;
    }

    context->TraverseEdgeTo(Node::Glyph(gid), dest, table_tag);
  }

  auto it = context_glyph_implied_edges_.find(gid);
  if (it == context_glyph_implied_edges_.end()) {
    return absl::OkStatus();
  }

  for (const auto& edge : it->second) {
    if (context->glyph_filter != nullptr &&
        !context->glyph_filter->contains(edge.dest_gid)) {
      continue;
    }

    TRYV(HandleGsubGlyphOutgoingEdges(edge.source_gid, edge.dest_gid,
                                      edge.layout_tag, edge.ligature_set,
                                      edge.context_set, context));
  }

  return absl::OkStatus();
}

template <typename CallbackT>
Status DependencyGraph::HandleGsubGlyphOutgoingEdges(
    glyph_id_t source_gid, glyph_id_t dest_gid, hb_tag_t layout_tag,
    hb_codepoint_t ligature_set, hb_codepoint_t context_set,
    TraversalContext<CallbackT>* context) const {
  if (context_set != HB_CODEPOINT_INVALID) {
    return context->TraverseContextualEdgeTo(source_gid, dest_gid, layout_tag,
                                             context_set);
  } else if (ligature_set != HB_CODEPOINT_INVALID) {
    return context->TraverseLigatureEdgeTo(source_gid, dest_gid, layout_tag,
                                           ligature_set);
  } else {
    return context->TraverseGsubEdgeTo(source_gid, dest_gid, layout_tag);
  }
}

template <typename CallbackT>
Status DependencyGraph::HandleFeatureOutgoingEdges(
    hb_tag_t feature_tag, TraversalContext<CallbackT>* context) const {
  if (!context->table_filter.contains(FontHelper::kGSUB)) {
    // All feature edges are GSUB edges so we can skip
    // this if GSUB is being filtered out.
    return absl::OkStatus();
  }

  auto edges = layout_feature_implied_edges_.find(feature_tag);
  if (edges == layout_feature_implied_edges_.end()) {
    // No outgoing edges
    return absl::OkStatus();
  }
  for (const auto& edge : edges->second) {
    if (context->glyph_filter != nullptr &&
        !context->glyph_filter->contains(edge.dest_gid)) {
      continue;
    }

    TRYV(HandleGsubGlyphOutgoingEdges(edge.source_gid, edge.dest_gid,
                                      feature_tag, edge.ligature_set,
                                      edge.context_set, context));
  }
  return absl::OkStatus();
}

template <typename CallbackT>
void DependencyGraph::HandleSegmentOutgoingEdges(
    segment_index_t id, TraversalContext<CallbackT>* context) const {
  if (id >= segmentation_info_->Segments().size()) {
    // Unknown segment has no outgoing edges.
    return;
  }

  const Segment& s = segmentation_info_->Segments().at(id);
  HandleSubsetDefinitionOutgoingEdges(Node::Segment(id), s.Definition(),
                                      context);
}

template <typename CallbackT>
void DependencyGraph::HandleSubsetDefinitionOutgoingEdges(
    Node source, const SubsetDefinition& subset_def,
    TraversalContext<CallbackT>* context) const {
  for (hb_codepoint_t u : subset_def.codepoints) {
    context->TraverseEdgeTo(source, Node::Unicode(u));
  }

  for (hb_tag_t f : subset_def.feature_tags) {
    context->TraverseEdgeTo(source, Node::Feature(f));
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
  hb_set_t* features =
      hb_subset_input_set(input, HB_SUBSET_SETS_LAYOUT_FEATURE_TAG);

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
  hb_set_t* features =
      hb_subset_input_set(input, HB_SUBSET_SETS_LAYOUT_FEATURE_TAG);

  flat_hash_set<hb_tag_t> out;
  hb_tag_t feature = HB_CODEPOINT_INVALID;
  while (hb_set_next(features, &feature)) {
    out.insert(feature);
  }
  hb_subset_input_destroy(input);

  return out;
}

StatusOr<GlyphSet> DependencyGraph::RequiredGlyphsFor(
    const PendingEdge& edge) const {
  GlyphSet out;

  if (edge.source.IsGlyph()) {
    out.insert(edge.source.Id());
  }

  if (edge.required_liga_set_index.has_value()) {
    out.union_set(TRY(GetLigaSet(*edge.required_liga_set_index)));
  }

  if (edge.required_context_set_index.has_value()) {
    out.union_set(TRY(GetContextSet(dependency_graph_.get(),
                                    &segmentation_info_->FullClosure(),
                                    *edge.required_context_set_index)));
  }

  return out;
}

StatusOr<GlyphSet> DependencyGraph::GetLigaSet(
    hb_codepoint_t liga_set_id) const {
  if (!hb_depend_get_set_from_index(dependency_graph_.get(), liga_set_id,
                                    scratch_set_.get())) {
    return absl::InternalError("Ligature set lookup failed.");
  }
  GlyphSet glyphs(scratch_set_.get());
  return glyphs;
}

StatusOr<EdgeConditionsCnf> DependencyGraph::ExtractRequirements(
    const PendingEdge& edge) const {

  // Note: the final returned cnf form should be sorted and de-dup'd,
  // so throughout this method we ensure the sub-groups are created
  // sorted (most are just single node's). Then the overall condition
  // is sorted and de-duped at the end.
  EdgeConditionsCnf requirements;

  requirements.push_back({edge.source});

  if (edge.required_feature.has_value()) {
    requirements.push_back({Node::Feature(*edge.required_feature)});
  }

  if (edge.required_codepoints.has_value()) {
    requirements.push_back({Node::Unicode(edge.required_codepoints->first)});
    requirements.push_back({Node::Unicode(edge.required_codepoints->second)});
  }

  if (edge.required_liga_set_index.has_value()) {
    GlyphSet gids = TRY(GetLigaSet(*edge.required_liga_set_index));
    for (glyph_id_t gid : gids) {
      requirements.push_back({Node::Glyph(gid)});
    }
  }

  // TODO(garretrieger): take contextual lookup flags into consideration. See
  // dep graph documentation for more details. They can be used to
  // decide if the resulting conditions are exact or an over approximation.
  if (edge.required_context_set_index.has_value()) {
    // the context set is actually a set of sets.
    if (!hb_depend_get_set_from_index(dependency_graph_.get(),
                                      *edge.required_context_set_index,
                                      scratch_set_.get())) {
      return absl::InternalError("Context set lookup failed.");
    }

    hb_codepoint_t set_id = HB_CODEPOINT_INVALID;
    while (hb_set_next(scratch_set_.get(), &set_id)) {
      std::vector<Node> req;
      if (set_id < 0x80000000) {
        req.push_back(Node::Glyph(set_id));
      } else {
        hb_codepoint_t actual_set_id = set_id & 0x7FFFFFFF;
        if (!hb_depend_get_set_from_index(dependency_graph_.get(),
                                          actual_set_id,
                                          scratch_set_aux_.get())) {
          return absl::InternalError("Context sub set lookup failed.");
        }

        // Creating the sub group from a hb set ensure's it's created
        // sorted.
        hb_codepoint_t gid = HB_CODEPOINT_INVALID;
        while (hb_set_next(scratch_set_aux_.get(), &gid)) {
          req.push_back(Node::Glyph(gid));
        }
      }
      if (!req.empty()) {
        requirements.push_back(std::move(req));
      }
    }
  }

  // Sort and remove duplicate entries
  std::sort(requirements.begin(), requirements.end());
  requirements.erase(std::unique(requirements.begin(), requirements.end()), requirements.end());

  return requirements;
}

flat_hash_map<glyph_id_t, std::vector<DependencyGraph::LayoutFeatureEdge>>
DependencyGraph::ComputeContextGlyphEdges() const {
  flat_hash_map<glyph_id_t, btree_set<LayoutFeatureEdge>> edges;
  for (const auto& [tag, feature_edges] : layout_feature_implied_edges_) {
    for (const auto& edge : feature_edges) {
      if (edge.context_set == HB_CODEPOINT_INVALID) {
        continue;
      }

      StatusOr<GlyphSet> context_glyphs =
          GetContextSet(dependency_graph_.get(), nullptr, edge.context_set);
      if (!context_glyphs.ok()) {
        continue;
      }
      for (glyph_id_t gid : *context_glyphs) {
        edges[gid].insert(edge);
      }
    }
  }

  flat_hash_map<glyph_id_t, std::vector<LayoutFeatureEdge>> out;
  for (auto& [gid, edge_set] : edges) {
    auto& edges_out = out[gid];
    for (const auto& edge : edge_set) {
      edges_out.push_back(edge);
    }
  }

  return out;
}

flat_hash_map<hb_codepoint_t,
              std::vector<DependencyGraph::VariationSelectorEdge>>
DependencyGraph::ComputeUVSEdges() const {
  hb_set_unique_ptr vs_unicodes_hb = make_hb_set();
  hb_face_collect_variation_selectors(original_face_.get(),
                                      vs_unicodes_hb.get());
  CodepointSet vs_unicodes(vs_unicodes_hb.get());

  hb_font_unique_ptr font = make_hb_font(hb_font_create(original_face_.get()));

  flat_hash_map<hb_codepoint_t, std::vector<VariationSelectorEdge>> edges;
  for (auto [u, gid] : unicode_to_gid_) {
    for (auto vs : vs_unicodes) {
      glyph_id_t dep_gid;
      if (!hb_font_get_variation_glyph(font.get(), u, vs, &dep_gid)) {
        continue;
      }

      if (dep_gid == gid) {
        // default mapping, gid isn't changed so we can ignore.
        continue;
      }

      edges[u].push_back(VariationSelectorEdge{
          .unicode = vs,
          .gid = dep_gid,
      });
      edges[vs].push_back(VariationSelectorEdge{
          .unicode = u,
          .gid = dep_gid,
      });
    }
  }

  return edges;
}

flat_hash_map<hb_tag_t, std::vector<DependencyGraph::LayoutFeatureEdge>>
DependencyGraph::ComputeFeatureEdges() const {
  flat_hash_map<hb_tag_t, btree_set<DependencyGraph::LayoutFeatureEdge>> edges;

  for (glyph_id_t gid = 0; gid < hb_face_get_glyph_count(original_face_.get());
       gid++) {
    hb_codepoint_t index = 0;
    hb_tag_t table_tag = HB_CODEPOINT_INVALID;
    hb_codepoint_t dest_gid = HB_CODEPOINT_INVALID;
    hb_tag_t layout_tag = HB_CODEPOINT_INVALID;
    hb_codepoint_t ligature_set = HB_CODEPOINT_INVALID;
    hb_codepoint_t context_set = HB_CODEPOINT_INVALID;
    while (hb_depend_get_glyph_entry(
        dependency_graph_.get(), gid, index++, &table_tag, &dest_gid,
        &layout_tag, &ligature_set, &context_set, nullptr /* flags */)) {
      if (table_tag != FontHelper::kGSUB ||
          layout_tag == HB_CODEPOINT_INVALID) {
        continue;
      }

      edges[layout_tag].insert(LayoutFeatureEdge{
          .layout_tag = layout_tag,
          .source_gid = gid,
          .dest_gid = dest_gid,
          .ligature_set = ligature_set,
          .context_set = context_set,
      });
    }
  }

  flat_hash_map<hb_tag_t, std::vector<DependencyGraph::LayoutFeatureEdge>> out;
  for (const auto& [tag, edges] : edges) {
    auto& edges_out = out[tag];
    for (const auto& e : edges) {
      edges_out.push_back(e);
    }
  }

  return out;
}

StatusOr<flat_hash_map<Node, btree_set<EdgeConditionsCnf>>>
DependencyGraph::CollectIncomingEdges(
    const flat_hash_set<hb_tag_t>& table_filter,
    uint32_t node_type_filter) const {
  struct IncomingEdgeCollector {
    flat_hash_map<Node, btree_set<EdgeConditionsCnf>>* incoming_edges = nullptr;
    const DependencyGraph* graph = nullptr;
    Status had_error = absl::OkStatus();

    void Visit(Node dest) {}
    void Visit(Node dest, hb_tag_t) {}
    void VisitGsub(Node, hb_tag_t) {}
    void VisitContextual(Node, hb_tag_t, GlyphSet) {}
    void VisitPending(const PendingEdge& pe) {
      auto reqs = graph->ExtractRequirements(pe);
      had_error.Update(reqs.status());
      if (!reqs.ok()) {
        return;
      }

      (*incoming_edges)[pe.dest].insert(std::move(*reqs));
    }
  };

  flat_hash_map<Node, btree_set<EdgeConditionsCnf>> incoming_edges;
  TraversalContext<IncomingEdgeCollector> context;
  context.depend = dependency_graph_.get();
  context.full_closure = &segmentation_info_->FullClosure();
  context.feature_filter = &full_feature_set_;
  context.glyph_filter = &segmentation_info_->FullClosure();
  context.unicode_filter = &segmentation_info_->FullDefinition().codepoints;
  context.enforce_context = false;
  context.table_filter = table_filter;
  context.node_type_filter = node_type_filter;
  context.callback.incoming_edges = &incoming_edges;
  context.callback.graph = this;

  // TODO(garretrieger): we have a few places iterating all nodes now, consider
  // adding a function to generate the all nodes set or an all nodes iterator.
  btree_set<Node> all_nodes;
  all_nodes.insert(Node::InitFont());
  for (segment_index_t s : segmentation_info_->NonEmptySegments()) {
    all_nodes.insert(Node::Segment(s));
  }
  for (hb_codepoint_t u : segmentation_info_->FullDefinition().codepoints) {
    all_nodes.insert(Node::Unicode(u));
  }
  for (hb_tag_t f : full_feature_set_) {
    all_nodes.insert(Node::Feature(f));
  }
  for (glyph_id_t g : segmentation_info_->FullClosure()) {
    all_nodes.insert(Node::Glyph(g));
  }

  for (Node n : all_nodes) {
    TRYV(HandleOutgoingEdges(n, &context));
    TRYV(context.callback.had_error);
  }

  return incoming_edges;
}

void PrintTo(const Node& node, std::ostream* os) { *os << node.ToString(); }

}  // namespace ift::dep_graph