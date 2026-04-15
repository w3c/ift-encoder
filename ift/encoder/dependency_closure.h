#ifndef IFT_ENCODER_DEPENDENCY_CLOSURE_H_
#define IFT_ENCODER_DEPENDENCY_CLOSURE_H_

#include <memory>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "hb.h"
#include "ift/common/font_data.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"

#ifdef HB_DEPEND_API
#include "ift/dep_graph/dependency_graph.h"
#include "ift/dep_graph/node.h"
#endif

namespace ift::encoder {

class RequestedSegmentationInformation;

/*
 * Performs closure analysis (like GlyphClosureCache) using a dependency graph
 * instead of closure. The dependency graph is not always accurate
 * (overestimating the true closure in some cases) so this returns a signal on
 * the accuracy of the analysis.
 */
class DependencyClosure {
 public:
  static absl::StatusOr<std::unique_ptr<DependencyClosure>> Create(
      const RequestedSegmentationInformation* segmentation_info,
      hb_face_t* face) {
#ifndef HB_DEPEND_API
    return std::unique_ptr<DependencyClosure>(new DependencyClosure());
#else
    dep_graph::DependencyGraph graph =
        TRY(dep_graph::DependencyGraph::Create(segmentation_info, face));
    auto result = std::unique_ptr<DependencyClosure>(
        new DependencyClosure(std::move(graph), segmentation_info, face));
    TRYV(result->InitFontChanged(ift::common::SegmentSet::all()));

    return result;
#endif
  }

  enum AnalysisAccuracy {
    // The analysis is accurate and should match true glyph closure.
    ACCURATE,

    // The analysis may not be accurate and as a result may overestimate
    // the true glyph closure.
    INACCURATE,
  };

  // Attempts to analyze the given segment using a glyph dependency graph
  // from harfbuzz. The return value signals if an analysis was able to be
  // performed or not.
  //
  // If ACCURATE is returned then the dep graph is able to produce an analysis
  // which should match GlyphClosureCache::AnalyzeSegment(). In this case
  // the three output sets (and_gids, or_gids, and exclusive_gids) will be
  // populated with the anaysis results.
  //
  // Otherwise if INACCURATE is returned then the dep graph analysis was found
  // to possibly not match GlyphClosureCache::AnalyzeSegment(). The three
  // output sets will not be modified in this case.
  //
  // The three output sets have the following interpretation:
  // and_gids: these gids have the union of input segments as a conjunctive
  // condition.
  //           ie. (s_1 U ... U s_n) AND ... -> and_gids
  //
  // or_gids: these gids have the union of input segments as a disjunctive
  // condition.
  //          ie. (s_1 U ... U s_n) OR ... -> or_gids
  //
  // exclusive_gids: these gids are exclusively needed by the union of input
  // segments.
  //                 ie. (s_1 U ... U s_n) -> exclusive_gids
  absl::StatusOr<AnalysisAccuracy> AnalyzeSegment(
      const ift::common::SegmentSet& segments, ift::common::GlyphSet& and_gids,
      ift::common::GlyphSet& or_gids, ift::common::GlyphSet& exclusive_gids);

#ifdef HB_DEPEND_API
  // Extracts the full activations conditions (as specified by the dependency
  // graph) for all glyphs. In some cases may overestimate activation conditions
  // versus real subsetting closure due to reliance on the dependency graph.
  const absl::flat_hash_map<glyph_id_t, ActivationCondition>&
  AllGlyphConditions() const {
    return glyph_condition_cache_;
  }

  common::GlyphSet SegmentsToAffectedGlyphs(
      const common::SegmentSet& segments) const;
#endif

  // This structure caches information derived from the segmentation info
  // segments. These two function signal that segmentation info segments have
  // changed and recomputes the internal cached information.
  absl::Status InitFontChanged(const ift::common::SegmentSet& segments);
  absl::Status SegmentsMerged(segment_index_t base_segment,
                              const ift::common::SegmentSet& segments);

  // The collection of SegmentsThatInteractWith(...) methods are used to
  // locate segments that have interactions with unicodes/features/glyphs.
  //
  // Interaction is defined as: given a node n, a segment interacts with
  // n if the segment can influence the presence in the glyph
  // closure of n or any things that depend on n (directly or indirectly).
  //
  // Utilizes the dependency graph to make the determination, so it's possible
  // that the result may be overestimated.
  absl::StatusOr<ift::common::SegmentSet> SegmentsThatInteractWith(
      const common::GlyphSet& glyphs) const;
  absl::StatusOr<ift::common::SegmentSet> SegmentsThatInteractWith(
      const SubsetDefinition& def) const;
#ifdef HB_DEPEND_API
  absl::StatusOr<ift::common::SegmentSet> SegmentsThatInteractWith(
      const absl::flat_hash_set<dep_graph::Node> nodes) const;
#endif

  uint64_t AccurateResults() const { return accurate_results_; }
  uint64_t InaccurateResults() const { return inaccurate_results_; }

 private:
#ifdef HB_DEPEND_API

  // Extracts the full activations conditions (as specified by the dependency
  // graph) for all graph nodes. In some cases may overestimate activation
  // conditions versus real subsetting closure due to reliance on the dependency
  // graph.
  absl::StatusOr<absl::flat_hash_map<dep_graph::Node, ActivationCondition>>
  ExtractAllNodeConditions() const;

  absl::flat_hash_map<dep_graph::Node, ActivationCondition>
  InitializeConditions() const;

  static absl::StatusOr<std::optional<ActivationCondition>>
  EdgeConditionsToActivationCondition(
      const dep_graph::EdgeConditionsCnf& edge_conditions,
      const absl::flat_hash_map<dep_graph::Node, ActivationCondition>&
          node_conditions);

  absl::Status PropagateConditions(
      const absl::flat_hash_map<dep_graph::Node,
                                absl::btree_set<dep_graph::EdgeConditionsCnf>>&
          incoming_edges,
      const std::vector<std::vector<dep_graph::Node>>& sccs,
      absl::flat_hash_map<dep_graph::Node, ActivationCondition>& conditions)
      const;
#endif

#ifndef HB_DEPEND_API
  DependencyClosure() {}
#else
  DependencyClosure(dep_graph::DependencyGraph&& graph,
                    const RequestedSegmentationInformation* segmentation_info,
                    hb_face_t* face)
      : segmentation_info_(segmentation_info),
        original_face_(ift::common::make_hb_face(hb_face_reference(face))),
        graph_(std::move(graph)) {}

  struct AnalysisResult {
    AnalysisAccuracy accuracy;
    ift::common::GlyphSet reached_glyphs;
    ift::common::GlyphSet and_gids;
    ift::common::GlyphSet or_gids;
    ift::common::GlyphSet exclusive_gids;
  };

  absl::StatusOr<AnalysisResult> AnalyzeSegmentInternal(
      const ift::common::SegmentSet& segments) const;

  absl::StatusOr<common::SegmentSet> FilterSegments(
      const common::SegmentSet& segments) const;

  absl::Status InitNodeConditionsCache();
  void UpdateNodeConditionsCache(segment_index_t base_segment,
                                 const common::SegmentSet& segments);
  absl::flat_hash_set<dep_graph::Node> SegmentsToAffectedNodeConditions(
      const common::SegmentSet& segments) const;

  const RequestedSegmentationInformation* segmentation_info_;
  ift::common::hb_face_unique_ptr original_face_;
  dep_graph::DependencyGraph graph_;

  ift::common::GlyphSet context_glyphs_;
  ift::common::GlyphSet init_font_context_glyphs_;

  absl::flat_hash_map<glyph_id_t, ActivationCondition> glyph_condition_cache_;
  absl::flat_hash_map<dep_graph::Node, ActivationCondition>
      node_condition_cache_;
  absl::flat_hash_map<segment_index_t, absl::flat_hash_set<dep_graph::Node>>
      node_conditions_with_segment_;

#endif

  uint64_t accurate_results_ = 0;
  uint64_t inaccurate_results_ = 0;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_DEPENDENCY_CLOSURE_H_