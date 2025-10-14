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
#include "ift/freq/probability_bound.h"
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
using ift::freq::ProbabilityBound;
using ift::freq::ProbabilityCalculator;

namespace ift::encoder {

// An indepth description of how this segmentation implementation works can
// be found in ../../docs/closure_glyph_segmentation.md.

Status CheckForDisjointCodepoints(
    const std::vector<SubsetDefinition>& subset_definitions,
    const SegmentSet& segments) {
  CodepointSet union_of_codepoints;
  for (segment_index_t s : segments) {
    const auto& def = subset_definitions[s];
    if (def.codepoints.intersects(union_of_codepoints)) {
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

static void ClassifySegments(
    const std::vector<SubsetDefinition>& subset_definitions,
    const btree_map<SegmentSet, MergeStrategy>& merge_groups,
    SegmentSet& ungrouped_segments, SegmentSet& shared_segments) {
  std::vector<uint32_t> group_count(subset_definitions.size());
  for (const auto& [segments, strategy] : merge_groups) {
    for (unsigned s : segments) {
      group_count[s]++;
    }
  }

  for (unsigned s = 0; s < subset_definitions.size(); s++) {
    if (group_count[s] == 0) {
      ungrouped_segments.insert(s);
    } else if (group_count[s] > 1) {
      shared_segments.insert(s);
    }
  }
}

static std::vector<ProbabilityBound> ComputeSegmentProbabilities(
    const std::vector<SubsetDefinition>& subset_definitions,
    const btree_map<SegmentSet, MergeStrategy>& merge_groups) {
  std::vector<ProbabilityBound> out(subset_definitions.size(),
                                    ProbabilityBound::Zero());
  for (const auto& [segments, strategy] : merge_groups) {
    if (!strategy.UseCosts()) {
      continue;
    }

    auto calculator = strategy.ProbabilityCalculator();
    for (segment_index_t s : segments) {
      ProbabilityBound p =
          calculator->ComputeProbability(subset_definitions[s]);
      if (p.Min() > out[s].Min()) {
        out[s] = p;
      }
    }
  }
  return out;
}

struct SegmentOrdering {
  unsigned group_index;
  freq::ProbabilityBound probability;
  unsigned original_index;

  bool operator<(const SegmentOrdering& other) const {
    if (group_index != other.group_index) {
      return group_index < other.group_index;
    }

    if (probability.Min() != other.probability.Min()) {
      // Probability descending.
      return probability.Min() > other.probability.Min();
    }

    if (probability.Max() != other.probability.Max()) {
      // Probability descending.
      return probability.Max() > other.probability.Max();
    }

    return original_index < other.original_index;
  }
};

// Converts the input subset definitions to a sorted list of segments, remaps
// the merge_groups segment set keys to reflect the ordering changes.
static StatusOr<std::vector<Segment>> ToOrderedSegments(
    const std::vector<SubsetDefinition>& subset_definitions,
    btree_map<SegmentSet, MergeStrategy>& merge_groups,
    btree_map<SegmentSet, SegmentSet>& with_shared) {
  // This generates the following ordering:
  //
  // merge group 1 segments
  // ...
  // merge group n segments
  // shared segments
  // ungrouped segments
  //
  // Within a group segments are sorted by probability (determined by that
  // groups frequency data) descending (with original ordering breaking ties).
  // For merge groups that don't utilize cost, the original sorting order is
  // used.

  SegmentSet ungrouped_segments;
  SegmentSet shared_segments;
  ClassifySegments(subset_definitions, merge_groups, ungrouped_segments,
                   shared_segments);

  VLOG(0) << "Segment classification: " << std::endl
          << "  "
          << subset_definitions.size() - ungrouped_segments.size() -
                 shared_segments.size()
          << " segments in exactly one merge groups" << std::endl
          << "  " << shared_segments.size()
          << " segments that in two or more merge groups" << std::endl
          << "  " << ungrouped_segments.size()
          << " segments that are ungrouped";

  std::vector<ProbabilityBound> segment_probabilities =
      ComputeSegmentProbabilities(subset_definitions, merge_groups);
  std::vector<SegmentOrdering> ordering;
  uint32_t group_index = 0;
  for (const auto& [segments, strategy] : merge_groups) {
    for (uint32_t s : segments) {
      if (shared_segments.contains(s)) {
        // shared segments are placed separately
        continue;
      }

      ordering.push_back({
          .group_index = group_index,
          .probability = segment_probabilities[s],
          .original_index = s,
      });
    }

    group_index++;
  }

  for (segment_index_t s : shared_segments) {
    ordering.push_back({
        .group_index = group_index,
        .probability = segment_probabilities[s],
        .original_index = s,
    });
  }

  group_index++;
  for (segment_index_t s : ungrouped_segments) {
    ordering.push_back({
        .group_index = group_index,
        .probability = ProbabilityBound::Zero(),
        .original_index = s,
    });
  }

  std::sort(ordering.begin(), ordering.end());

  // maps from index in subset_definitions to the new ordering.
  std::vector<uint32_t> segment_index_map(subset_definitions.size());
  std::vector<Segment> segments;
  unsigned i = 0;
  for (const auto& ordering : ordering) {
    segments.push_back(Segment{subset_definitions[ordering.original_index],
                               ordering.probability});
    segment_index_map[ordering.original_index] = i++;
  }

  btree_map<SegmentSet, MergeStrategy> new_merge_groups;
  for (auto& [segments, strategy] : merge_groups) {
    SegmentSet remapped;
    SegmentSet remapped_full;
    for (segment_index_t s : segments) {
      segment_index_t s_prime = segment_index_map[s];
      if (!shared_segments.contains(s)) {
        remapped.insert(s_prime);
      }
      remapped_full.insert(s_prime);
    }

    if (!new_merge_groups.insert(std::make_pair(remapped, std::move(strategy)))
             .second) {
      return absl::InvalidArgumentError(
          "Duplicate merge groups are not allowed.");
    }
    with_shared[remapped] = remapped_full;
  }

  merge_groups = std::move(new_merge_groups);
  return segments;
}

StatusOr<GlyphSegmentation> ClosureGlyphSegmenter::CodepointToGlyphSegments(
    hb_face_t* face, SubsetDefinition initial_segment,
    const std::vector<SubsetDefinition>& subset_definitions,
    std::optional<MergeStrategy> strategy, uint32_t brotli_quality) const {
  btree_map<SegmentSet, MergeStrategy> merge_groups;
  if (!subset_definitions.empty() && strategy.has_value()) {
    SegmentSet all;
    all.insert_range(0, subset_definitions.size() - 1);
    merge_groups = {{all, std::move(*strategy)}};
  }

  return CodepointToGlyphSegments(face, initial_segment, subset_definitions,
                                  merge_groups, brotli_quality, false);
}

StatusOr<std::vector<Merger>> ToMergers(
    SegmentationContext& context,
    const btree_map<SegmentSet, SegmentSet>& with_shared,
    btree_map<SegmentSet, MergeStrategy> merge_groups) {
  std::vector<Merger> mergers;
  for (auto& [segments, strategy] : merge_groups) {
    mergers.push_back(TRY(Merger::New(context, std::move(strategy), segments,
                                      with_shared.at(segments))));
  }
  return mergers;
}

StatusOr<GlyphSegmentation> ClosureGlyphSegmenter::CodepointToGlyphSegments(
    hb_face_t* face, SubsetDefinition initial_segment,
    const std::vector<SubsetDefinition>& subset_definitions,
    btree_map<SegmentSet, MergeStrategy> merge_groups, uint32_t brotli_quality,
    bool place_fallback_in_init) const {
  for (const auto& [segments, strategy] : merge_groups) {
    if (strategy.UseCosts()) {
      TRYV(CheckForDisjointCodepoints(subset_definitions, segments));
    }
  }

  btree_map<SegmentSet, SegmentSet> with_shared;
  std::vector<Segment> segments =
      TRY(ToOrderedSegments(subset_definitions, merge_groups, with_shared));
  SegmentationContext context = TRY(InitializeSegmentationContext(
      face, initial_segment, std::move(segments), brotli_quality));

  std::vector<Merger> mergers =
      TRY(ToMergers(context, with_shared, merge_groups));

  // ### First phase of merging is to check for any patches which should be
  // moved to the initial font (eg. cases where the probability of a patch is
  // ~1.0). Do this only for strategies that have opted in.
  for (Merger& merger : mergers) {
    if (merger.Strategy().UseCosts() &&
        merger.Strategy().InitFontMergeThreshold().has_value()) {
      // make sure candidate segments is up to date before attempting to
      // process.
      TRYV(merger.ReassignInitSubset());
      TRYV(merger.MoveSegmentsToInitFont());
    }
  }
  for (Merger& merger : mergers) {
    // Any init font moves above can cause segment removals that affect other
    // mergers, recompute the candiate segments for all mergers.
    TRYV(merger.ReassignInitSubset());
  }

  // Once we've gotten standard segments placed into the initial font as needed,
  // if requested any remaining fallback glyphs are also moved into the init
  // font.
  GlyphSet fallback_glyphs = context.glyph_groupings.FallbackGlyphs();
  if (place_fallback_in_init && !fallback_glyphs.empty()) {
    VLOG(0) << "Moving " << fallback_glyphs.size()
            << " fallback glyphs into the initial font." << std::endl;
    SubsetDefinition new_def = context.SegmentationInfo().InitFontSegment();
    new_def.gids.union_set(fallback_glyphs);
    TRYV(context.ReassignInitSubset(new_def));
  }

  // Before we start merging, make sure the state after init font processing is
  // correct.
  TRYV(ValidateIncrementalGroupings(face, context));

  if (merge_groups.empty()) {
    // No merging will be needed so we're done.
    return context.ToGlyphSegmentation();
  }

  // ### Iteratively merge segments and incrementally reprocess affected data.
  size_t merger_index = 0;
  segment_index_t last_merged_segment_index = 0;
  VLOG(0) << "Starting merge selection for merge group " << merger_index;
  while (true) {
    auto& merger = mergers[merger_index];
    auto merged = TRY(merger.TryNextMerge());

    if (!merged.has_value()) {
      merger_index++;
      if (merger_index < mergers.size()) {
        VLOG(0) << "Merge group finished, starting next group " << merger_index;
        continue;
      }

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
