#ifndef IFT_ENCODER_DEPENDENCY_CLOSURE_H_
#define IFT_ENCODER_DEPENDENCY_CLOSURE_H_

#include <memory>
#include <optional>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "common/font_data.h"
#include "common/int_set.h"
#include "common/try.h"
#include "hb.h"
#include "ift/dep_graph/dependency_graph.h"
#include "ift/encoder/types.h"

namespace ift::encoder {

class RequestedSegmentationInformation;

/*
 * Performs closure analysis (like GlyphClosureCache) using a depedency graph
 * instead of closure. The dependency graph is not always accurate (overestimating
 * the true closure in some cases) so this returns a signal on the accuracy of
 * the analysis.
 */
class DependencyClosure {
 public:
  static absl::StatusOr<std::unique_ptr<DependencyClosure>> Create(
      const RequestedSegmentationInformation* segmentation_info,
      hb_face_t* face) {
    dep_graph::DependencyGraph graph = TRY(dep_graph::DependencyGraph::Create(segmentation_info, face));
    auto result = std::unique_ptr<DependencyClosure>(new DependencyClosure(std::move(graph), segmentation_info, face));
    TRYV(result->SegmentsChanged(true, common::SegmentSet::all()));
    return result;
  }

  enum AnalysisAccuracy {
    // The analysis is accurate and should match true glyph closure.
    ACCURATE,

    // The analysis may not be accurate and as a result may overestimate
    // the true glyph closure.
    INACCURATE,
  };

  // Attempts to analyze the given segment using a glyph dependency graph
  // from harfbuzz. The return value signals if an analysis was able to be performed
  // or not.
  //
  // If ACCURATE is returned then the dep graph is able to produce an analysis
  // which should match GlyphClosureCache::AnalyzeSegment(). In this case
  // the three output sets (and_gids, or_gids, and exclusive_gids) will be populated
  // with the anaysis results.
  //
  // Otherwise if INACCURATE is returned then the dep graph analysis was found
  // to possibly not match GlyphClosureCache::AnalyzeSegment(). The three
  // output sets will not be modified in this case.
  //
  // The three output sets have the following interpretation:
  // and_gids: these gids have the union of input segments as a conjunctive condition.
  //           ie. (s_1 U ... U s_n) AND ... -> and_gids
  //
  // or_gids: these gids have the union of input segments as a disjunctive condition.
  //          ie. (s_1 U ... U s_n) OR ... -> or_gids
  //
  // exclusive_gids: these gids are exclusively needed by the union of input segments.
  //                 ie. (s_1 U ... U s_n) -> exclusive_gids
  absl::StatusOr<AnalysisAccuracy> AnalyzeSegment(const common::SegmentSet& segments,
                      common::GlyphSet& and_gids,
                      common::GlyphSet& or_gids,
                      common::GlyphSet& exclusive_gids);

  // This structure caches information derived from the segmentation info segments.
  // This function signals that segmentation info segments have changed and recomputes
  // the internal cached information.
  absl::Status SegmentsChanged(bool init_font_changed, const common::SegmentSet& segments);

  // Finds the complete set of segments that may have some interaction on the presence of glyphs
  // in the glyph closure.
  //
  // Utilizes the depedency graph to make the determination, so it's possible that the result
  // may be overestimated.
  absl::StatusOr<common::SegmentSet> SegmentsThatInteractWith(const common::GlyphSet& glyphs);

  absl::StatusOr<common::SegmentSet> SegmentInteractionGroup(const common::SegmentSet& segments);

  uint64_t AccurateResults() const { return accurate_results_; }
  uint64_t InaccurateResults() const { return inaccurate_results_; }

 private:

  DependencyClosure(
    dep_graph::DependencyGraph&& graph,
    const RequestedSegmentationInformation* segmentation_info,
    hb_face_t* face)
      : segmentation_info_(segmentation_info),
        original_face_(common::make_hb_face(hb_face_reference(face))),
        graph_(std::move(graph)) {
  }

  std::optional<common::GlyphSet> AccurateReachedGlyphsFor(segment_index_t s) const {
    auto it = accurate_glyphs_that_can_be_reached_.find(s);
    if (it == accurate_glyphs_that_can_be_reached_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  // Returns true if all segments that can reach gid have accurate reachability in the index.
  // Segments in the excluded set are ignored for this check.
  bool GlyphHasFullyAccurateReachability(glyph_id_t gid, const common::SegmentSet& excluded) const;

  AnalysisAccuracy TraversalAccuracy(const dep_graph::Traversal& traversal) const;

  static absl::flat_hash_map<hb_codepoint_t, glyph_id_t> UnicodeToGid(
      hb_face_t* face);

  void ReachabilityInitFontAddToCheck(
    common::GlyphSet& visited_glyphs,
    absl::btree_set<hb_tag_t>& visited_features,
    common::GlyphSet& to_check,
    absl::btree_set<hb_tag_t>& features_to_check
  ) const;
  absl::Status ReachabilitySegmentsAddToCheck(
    const common::SegmentSet& segments,
    common::SegmentSet& visited_segments,
    common::GlyphSet& visited_glyphs,
    absl::btree_set<hb_tag_t>& visited_features,
    common::GlyphSet& to_check,
    absl::btree_set<hb_tag_t>& features_to_check
  ) const;

  common::SegmentSet ConnectedSegments(segment_index_t s);
  common::SegmentSet InitFontConnections();

  absl::Status UpdateReachabilityIndex(common::SegmentSet segments);
  absl::Status UpdateReachabilityIndex(segment_index_t segment);
  void ClearReachabilityIndex(segment_index_t segment);

  const RequestedSegmentationInformation* segmentation_info_;
  common::hb_face_unique_ptr original_face_;
  dep_graph::DependencyGraph graph_;
  common::GlyphSet context_glyphs_;
  common::GlyphSet init_font_reachable_glyphs_;
  common::GlyphSet init_font_context_glyphs_;
  absl::flat_hash_set<hb_tag_t> init_font_features_;
  absl::flat_hash_set<hb_tag_t> init_font_context_features_;

  // Reachability indexes: these indexes are used to quickly locate segments reachable
  // from glyph and features (and in reverse as well).
  bool reachability_index_valid_ = false;

  absl::flat_hash_map<glyph_id_t, common::SegmentSet> segments_that_can_reach_;
  absl::flat_hash_map<segment_index_t, common::GlyphSet> glyphs_that_can_be_reached_;

  // This index only includes segments where the traversal is considered accurate (ie. reproduces
  // glyph closure exactly) it is a subset of segments_that_can_reach_/glyphs_that_can_be_reached_.
  absl::flat_hash_map<glyph_id_t, common::SegmentSet> accurate_segments_that_can_reach_;
  absl::flat_hash_map<segment_index_t, common::GlyphSet> accurate_glyphs_that_can_be_reached_;

  absl::flat_hash_map<hb_tag_t, common::SegmentSet> segments_that_can_reach_feature_;
  absl::flat_hash_map<segment_index_t, absl::btree_set<hb_tag_t>> features_that_can_be_reached_;

  absl::flat_hash_map<glyph_id_t, common::SegmentSet> segments_that_have_context_glyph_;
  absl::flat_hash_map<segment_index_t, common::GlyphSet> segment_context_glyphs_;
  absl::flat_hash_map<hb_tag_t, common::SegmentSet> segments_that_have_context_feature_;
  absl::flat_hash_map<segment_index_t, absl::btree_set<hb_tag_t>> segment_context_features_;

  uint64_t accurate_results_ = 0;
  uint64_t inaccurate_results_ = 0;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_DEPENDENCY_CLOSURE_H_