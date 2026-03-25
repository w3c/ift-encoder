#ifndef IFT_ENCODER_DEPENDENCY_CLOSURE_H_
#define IFT_ENCODER_DEPENDENCY_CLOSURE_H_

#include <memory>
#include <optional>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "hb.h"
#include "ift/common/font_data.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/dep_graph/traversal.h"
#include "ift/encoder/reachability_index.h"
#include "ift/encoder/types.h"

#ifdef HB_DEPEND_API
#include "ift/dep_graph/dependency_graph.h"
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
    TRYV(result->SegmentsChanged(true, ift::common::SegmentSet::all()));
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

  // This structure caches information derived from the segmentation info
  // segments. This function signals that segmentation info segments have
  // changed and recomputes the internal cached information.
  absl::Status SegmentsChanged(bool init_font_changed,
                               const ift::common::SegmentSet& segments);

  // Finds the complete set of segments that may have some interaction on the
  // presence of glyphs in the glyph closure.
  //
  // Utilizes the dependency graph to make the determination, so it's possible
  // that the result may be overestimated.
  absl::StatusOr<ift::common::SegmentSet> SegmentsThatInteractWith(
      const ift::common::GlyphSet& glyphs);

  absl::StatusOr<ift::common::SegmentSet> SegmentInteractionGroup(
      const ift::common::SegmentSet& segments);

  uint64_t AccurateResults() const { return accurate_results_; }
  uint64_t InaccurateResults() const { return inaccurate_results_; }

 private:
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

  absl::StatusOr<AnalysisAccuracy> ConjunctiveConditionDiscovery(
      // assumes segments has been filtered and bound checked already
      const common::SegmentSet& start, const common::GlyphSet& glyph_filter,
      absl::flat_hash_map<glyph_id_t, common::SegmentSet>& conditions_for_glyph)
      const;

  absl::StatusOr<AnalysisAccuracy> ConjunctiveConditionEdges(
      const common::SegmentSet& node, const dep_graph::Traversal& traversal,
      common::SegmentSet& edges) const;

  AnalysisAccuracy TraversalAccuracy(
      const dep_graph::Traversal& traversal) const;

  static absl::flat_hash_map<hb_codepoint_t, glyph_id_t> UnicodeToGid(
      hb_face_t* face);

  void ReachabilityInitFontAddToCheck(
      ift::common::GlyphSet& visited_glyphs,
      absl::btree_set<hb_tag_t>& visited_features,
      ift::common::GlyphSet& to_check,
      absl::btree_set<hb_tag_t>& features_to_check) const;
  absl::Status ReachabilitySegmentsAddToCheck(
      const ift::common::SegmentSet& segments,
      ift::common::SegmentSet& visited_segments,
      ift::common::GlyphSet& visited_glyphs,
      absl::btree_set<hb_tag_t>& visited_features,
      ift::common::GlyphSet& to_check,
      absl::btree_set<hb_tag_t>& features_to_check) const;

  ift::common::SegmentSet ConnectedSegments(segment_index_t s);
  ift::common::SegmentSet InitFontConnections();

  absl::Status UpdateReachabilityIndex(ift::common::SegmentSet segments);
  absl::Status UpdateReachabilityIndex(segment_index_t segment);
  void ClearReachabilityIndex(segment_index_t segment);

  const RequestedSegmentationInformation* segmentation_info_;
  ift::common::hb_face_unique_ptr original_face_;
  dep_graph::DependencyGraph graph_;
  ift::common::GlyphSet context_glyphs_;
  ift::common::GlyphSet init_font_reachable_glyphs_;
  ift::common::GlyphSet init_font_context_glyphs_;
  absl::flat_hash_set<hb_tag_t> init_font_features_;
  absl::flat_hash_set<hb_tag_t> init_font_context_features_;

  // Reachability indexes: these indexes are used to quickly locate segments
  // reachable from glyph and features (and in reverse as well).
  bool reachability_index_valid_ = false;

  // Unconstrained reachability, these are the glyphs/features that can be
  // reached by a segment if context constraints are not enforced.
  ReachabilityIndex unconstrained_reachability_;

  // Isolated reachability: the set of glyphs that can be reached from
  // each segment in isolation. Conjunctive edges where the relevant
  // context is not present are not traversed.
  ReachabilityIndex isolated_reachability_;

  // Segments for which the isolated graph traversal was ACCURATE.
  ift::common::SegmentSet isolated_reachability_is_accurate_;

  // For these segments AnalyzeSegment() is able to reach all glyphs
  // in the unconstrainted reachability.
  ift::common::SegmentSet fully_explorable_segments_;

  // Tracks which context glyphs (for contextual gsub substitutions) can be
  // reached from a segment.
  ReachabilityIndex context_reachability_;
#endif

  uint64_t accurate_results_ = 0;
  uint64_t inaccurate_results_ = 0;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_DEPENDENCY_CLOSURE_H_