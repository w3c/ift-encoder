#include "ift/encoder/closure_glyph_segmenter.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/compat_id.h"
#include "common/font_data.h"
#include "common/font_helper.h"
#include "common/hb_set_unique_ptr.h"
#include "common/int_set.h"
#include "common/try.h"
#include "common/woff2.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/merge_strategy.h"
#include "ift/encoder/merger.h"
#include "ift/encoder/patch_size_cache.h"
#include "ift/encoder/segmentation_context.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"
#include "ift/freq/probability_calculator.h"
#include "ift/glyph_keyed_diff.h"

using absl::btree_map;
using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::node_hash_map;
using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using common::CodepointSet;
using common::CompatId;
using common::FontData;
using common::FontHelper;
using common::GlyphSet;
using common::hb_face_unique_ptr;
using common::hb_set_unique_ptr;
using common::IntSet;
using common::make_hb_face;
using common::make_hb_set;
using common::SegmentSet;
using common::Woff2;
using ift::GlyphKeyedDiff;
using ift::freq::ProbabilityCalculator;

namespace ift::encoder {

// An indepth description of how this segmentation implementation works can
// be found in ../../docs/closure_glyph_segmentation.md.

Status CheckForDisjointCodepoints(
    const std::vector<SubsetDefinition>& subset_definitions) {
  CodepointSet union_of_codepoints;
  for (const auto& def : subset_definitions) {
    CodepointSet intersection = def.codepoints;
    intersection.intersect(union_of_codepoints);
    if (!intersection.empty()) {
      return absl::InvalidArgumentError(
          "Input subset definitions must have disjoint codepoint sets when "
          "using cost-based merging.");
    }
    union_of_codepoints.union_set(def.codepoints);
  }
  return absl::OkStatus();
}

/*
 * Checks that the incrementally generated glyph conditions and groupings in
 * context match what would have been produced by a non incremental process.
 *
 * Returns OkStatus() if they match.
 */
Status ValidateIncrementalGroupings(hb_face_t* face,
                                    const SegmentationContext& context) {
  SegmentationContext non_incremental_context(
      face, context.SegmentationInfo().InitFontSegment(),
      context.SegmentationInfo().Segments(), 1);

  // Compute the glyph groupings/conditions from scratch to compare against the
  // incrementall produced ones.
  for (segment_index_t segment_index = 0;
       segment_index < context.SegmentationInfo().Segments().size();
       segment_index++) {
    TRY(non_incremental_context.ReprocessSegment(segment_index));
  }

  // Transfer over information on combined patches
  for (const GlyphSet& group :
       TRY(context.glyph_groupings.CombinedPatches().NonIdentityGroups())) {
    TRYV(non_incremental_context.glyph_groupings.CombinePatches(group, {}));
  }

  GlyphSet all_glyphs;
  uint32_t glyph_count = hb_face_get_glyph_count(face);
  all_glyphs.insert_range(0, glyph_count - 1);
  TRYV(non_incremental_context.GroupGlyphs(all_glyphs));

  if (non_incremental_context.glyph_groupings.ConditionsAndGlyphs() !=
      context.glyph_groupings.ConditionsAndGlyphs()) {
    return absl::FailedPreconditionError(
        "conditions_and_glyphs aren't correct.");
  }

  if (non_incremental_context.glyph_condition_set !=
      context.glyph_condition_set) {
    return absl::FailedPreconditionError("glyph_condition_set isn't correct.");
  }

  if (non_incremental_context.glyph_groupings != context.glyph_groupings) {
    return absl::FailedPreconditionError("glyph groups aren't correct.");
  }

  return absl::OkStatus();
}

static StatusOr<std::vector<Segment>> ToSegments(
    const std::vector<SubsetDefinition>& subset_definitions,
    const MergeStrategy& merge_strategy) {
  auto calculator = merge_strategy.ProbabilityCalculator();
  std::vector<Segment> segments;
  for (const auto& def : subset_definitions) {
    auto probability = calculator->ComputeProbability(def);
    segments.emplace_back(def, probability);
  }
  if (merge_strategy.UseCosts()) {
    // Cost based merging has probability data available for segments, use that
    // to sort from highest to lowest. Later processing relies on this ordering.
    std::sort(segments.begin(), segments.end(),
              [](const Segment& a, const Segment& b) {
                if (a.Probability() != b.Probability()) {
                  return a.Probability() > b.Probability();
                }
                if (a.Definition().codepoints != b.Definition().codepoints) {
                  return a.Definition().codepoints < b.Definition().codepoints;
                }
                return a.Definition().feature_tags <
                       b.Definition().feature_tags;
              });
  }
  return segments;
}

StatusOr<GlyphSegmentation> ClosureGlyphSegmenter::CodepointToGlyphSegments(
    hb_face_t* face, SubsetDefinition initial_segment,
    const std::vector<SubsetDefinition>& subset_definitions,
    std::optional<MergeStrategy> strategy, uint32_t brotli_quality) const {
  MergeStrategy merge_strategy = MergeStrategy::None();
  if (strategy.has_value()) {
    merge_strategy = std::move(*strategy);
  }

  if (merge_strategy.UseCosts()) {
    TRYV(CheckForDisjointCodepoints(subset_definitions));
  }

  std::vector<Segment> segments =
      TRY(ToSegments(subset_definitions, merge_strategy));
  SegmentationContext context = TRY(InitializeSegmentationContext(
      face, initial_segment, std::move(segments), brotli_quality));

  if (merge_strategy.IsNone()) {
    // No merging will be needed so we're done.
    return context.ToGlyphSegmentation();
  }

  // TODO XXXXX add support for having multiple merge strategies and mergers
  // (eg. one per script)
  Merger merger = TRY(Merger::New(context, std::move(merge_strategy)));

  // ### First phase of merging is to check for any patches which should be
  // moved to the initial font
  //     (eg. cases where the probability of a patch is ~1.0).
  if (merger.Strategy().UseCosts() &&
      merger.Strategy().InitFontMergeThreshold().has_value()) {
    TRYV(merger.MoveSegmentsToInitFont());
  }

  // ### Iteratively merge segments and incrementally reprocess affected data.
  segment_index_t last_merged_segment_index = 0;
  while (true) {
    auto merged = TRY(merger.TryNextMerge());

    if (!merged.has_value()) {
      // Nothing was merged so we're done.
      TRYV(ValidateIncrementalGroupings(face, context));
      return context.ToGlyphSegmentation();
    }

    const auto& [merged_segment_index, modified_gids] = *merged;
    last_merged_segment_index = merged_segment_index;

    GlyphSet analysis_modified_gids;
    if (!context.InertSegments().contains(last_merged_segment_index)) {
      VLOG(0) << "Re-analyzing segment " << last_merged_segment_index
              << " due to merge.";
      analysis_modified_gids =
          TRY(context.ReprocessSegment(last_merged_segment_index));
    }
    analysis_modified_gids.union_set(modified_gids);

    TRYV(context.GroupGlyphs(analysis_modified_gids));

    context.glyph_closure_cache.LogClosureCount("Condition grouping");
  }

  return absl::InternalError("unreachable");
}

StatusOr<SegmentationContext>
ClosureGlyphSegmenter::InitializeSegmentationContext(
    hb_face_t* face, SubsetDefinition initial_segment,
    std::vector<Segment> segments, uint32_t brotli_quality) const {
  uint32_t glyph_count = hb_face_get_glyph_count(face);
  if (!glyph_count) {
    return absl::InvalidArgumentError("Provided font has no glyphs.");
  }

  // The IFT compiler has a set of defaults always included in the initial font
  // add them here so we correctly factor them into the generated segmentation.
  AddInitSubsetDefaults(initial_segment);

  // No merging is done during init.
  SegmentationContext context(face, initial_segment, segments, brotli_quality);

  // ### Generate the initial conditions and groupings by processing all
  // segments and glyphs. ###
  VLOG(0) << "Forming initial segmentation plan.";
  for (segment_index_t segment_index = 0;
       segment_index < context.SegmentationInfo().Segments().size();
       segment_index++) {
    TRY(context.ReprocessSegment(segment_index));
  }
  context.glyph_closure_cache.LogClosureCount("Inital segment analysis");

  GlyphSet all_glyphs;
  all_glyphs.insert_range(0, glyph_count - 1);
  TRYV(context.GroupGlyphs(all_glyphs));
  context.glyph_closure_cache.LogClosureCount("Condition grouping");

  return context;
}

StatusOr<SegmentationCost> ClosureGlyphSegmenter::TotalCost(
    hb_face_t* original_face, const GlyphSegmentation& segmentation,
    const ProbabilityCalculator& probability_calculator) const {
  SubsetDefinition non_ift;
  non_ift.Union(segmentation.InitialFontSegment());

  std::vector<Segment> segments;
  for (const auto& def : segmentation.Segments()) {
    non_ift.Union(def);

    auto P = probability_calculator.ComputeProbability(def);
    Segment s(def, P);
    segments.push_back(std::move(s));
  }

  double init_font_size = TRY(CandidateMerge::Woff2SizeOf(
      original_face, segmentation.InitialFontSegment(), 11));
  double non_ift_font_size =
      TRY(CandidateMerge::Woff2SizeOf(original_face, non_ift, 11));

  // TODO(garretrieger): for the total cost we need to also add in the table
  // keyed patch costs
  //                     may want to use the IFT compiler to produce the
  //                     complete encoding then compute table keyed costs from
  //                     that (in conjunction) with probability calculations.
  double total_cost = init_font_size;

  // Use highest quality so we get the true cost.
  PatchSizeCacheImpl patch_sizer(original_face, 11);
  for (const auto& c : segmentation.Conditions()) {
    double Pc = TRY(c.Probability(segments, probability_calculator));
    const GlyphSet& gids = segmentation.GidSegments().at(c.activated());
    double patch_size = (double)TRY(patch_sizer.GetPatchSize(gids));
    total_cost += Pc * (patch_size + 75);
  }

  double ideal_cost = 0.0;
  double incremental_size =
      non_ift_font_size / (double)non_ift.codepoints.size();
  for (unsigned cp : non_ift.codepoints) {
    double Pcp = probability_calculator.ComputeProbability({cp}).Min();
    ideal_cost += Pcp * incremental_size;
  }

  return SegmentationCost{
      .total_cost = total_cost,
      .cost_for_non_segmented = non_ift_font_size,
      .ideal_cost = ideal_cost,
  };
}

}  // namespace ift::encoder
