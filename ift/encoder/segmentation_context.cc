#include "ift/encoder/segmentation_context.h"

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "ift/common/font_helper.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/dependency_closure.h"
#include "ift/encoder/glyph_condition_set.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/init_subset_defaults.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"

using ift::config::ConditionAnalysisMode;
using ift::config::DEP_GRAPH_ONLY_WITH_SIMPLIFICATION;

using absl::Status;
using absl::StatusOr;
using ift::common::DataFileResolver;
using ift::common::GlyphSet;
using ift::common::IntSet;
using ift::common::SegmentSet;

namespace ift::encoder {

Status SegmentationContext::CacheInitialGlyphProbabilities(
    absl::Span<const freq::ProbabilityCalculator* const>
        probability_calculators) {
  initial_glyph_probabilities_.clear();
  for (const freq::ProbabilityCalculator* calculator :
       probability_calculators) {
    calculator->ResetSegmentProbabilities(
        segmentation_info_->Segments().size());
  }

  for (const auto& [cond, gids] : glyph_groupings.ConditionsAndGlyphs()) {
    double max_prob = 0.0;
    for (const freq::ProbabilityCalculator* calculator :
         probability_calculators) {
      double prob =
          TRY(cond.Probability(segmentation_info_->Segments(), *calculator));
      if (prob > max_prob) {
        max_prob = prob;
      }
    }
    for (glyph_id_t gid : gids) {
      initial_glyph_probabilities_[gid] = max_prob;
    }
  }
  return absl::OkStatus();
}

Status SegmentationContext::ValidateSegmentation(
    const GlyphSegmentation& segmentation) const {
  GlyphSet visited;
  const auto& initial_closure = segmentation.InitialFontGlyphClosure();
  auto check_disjoint = [&](const GlyphSet& gids) -> Status {
    for (glyph_id_t gid : gids) {
      if (initial_closure.contains(gid)) {
        return absl::FailedPreconditionError(absl::StrCat(
            "Initial font glyph g", gid, " is present in a patch."));
      }
      if (visited.contains(gid)) {
        return absl::FailedPreconditionError(
            "Glyph segments are not disjoint.");
      }
      visited.insert(gid);
    }
    return absl::OkStatus();
  };

  for (const auto& [id, gids] : segmentation.GidSegments()) {
    TRYV(check_disjoint(gids));
  }

  if (target_mode_ == config::UNICODE_RANGE) {
    for (const auto& condition : segmentation.Conditions()) {
      if (!condition.IsExclusive()) {
        return absl::FailedPreconditionError(absl::StrCat(
            "Non-exclusive condition found in UNICODE_RANGE mode: ",
            condition.ToString()));
      }
    }

    // In unicode range mode not all conditions get record into segmentation, so
    // check disjointness across the non-exclusive patches which were skipped.
    GlyphSet unreachable_glyphs;
    for (const auto& cond : glyph_groupings.OrderedConditions()) {
      if (cond.IsExclusive()) {
        continue;
      }
      const GlyphSet& gids = glyph_groupings.ConditionsAndGlyphs().at(cond);
      TRYV(check_disjoint(gids));
      unreachable_glyphs.union_set(gids);
    }

    LogUnreachableGlyphs(unreachable_glyphs);
    LogSegmentCodepointCountHistogram(segmentation);
  }

  GlyphSet full_minus_initial = segmentation_info_->FullClosure();
  full_minus_initial.subtract(initial_closure);

  if (full_minus_initial != visited) {
    GlyphSet missing = full_minus_initial;
    missing.subtract(visited);
    return absl::FailedPreconditionError(
        "Not all glyphs in the full closure have been placed. Missing: " +
        missing.ToString());
  }

  for (const auto& condition : segmentation.Conditions()) {
    auto it = segmentation.GidSegments().find(condition.activated());
    if (it == segmentation.GidSegments().end()) {
      return absl::FailedPreconditionError(absl::StrCat(
          "Patch ", condition.activated(), " does not have glyphs specified"));
    }

    const auto& glyphs = it->second;
    for (const auto& sub_group : condition.conditions()) {
      if (TRY(glyph_closure_cache->HasAdditionalConditions(
              &SegmentationInfo(), sub_group, glyphs))) {
        return absl::FailedPreconditionError(absl::StrCat(
            "Found a condition which does not satisfy the glyph closure "
            "requirement: ",
            condition.ToString(), " ", glyphs.ToString(), ", sub group ",
            sub_group.ToString(), " failed the check."));
      }
    }
  }

  return absl::OkStatus();
}

void SegmentationContext::LogUnreachableGlyphs(
    const GlyphSet& unreachable_glyphs) const {
  std::vector<std::pair<glyph_id_t, double>> ordered_glyphs;
  ordered_glyphs.reserve(unreachable_glyphs.size());
  for (glyph_id_t gid : unreachable_glyphs) {
    double initial_prob = 0.0;
    if (auto it = initial_glyph_probabilities_.find(gid);
        it != initial_glyph_probabilities_.end()) {
      initial_prob = it->second;
    }
    ordered_glyphs.emplace_back(gid, initial_prob);
  }

  std::sort(ordered_glyphs.begin(), ordered_glyphs.end(),
            [](const auto& a, const auto& b) {
              if (a.second != b.second) {
                return a.second > b.second;
              }
              return a.first < b.first;
            });

  auto gid_to_unicode = common::FontHelper::GidToUnicodeMap(original_face.get());
  for (const auto& [gid, initial_prob] : ordered_glyphs) {
    std::string cp_str;
    auto cp_it = gid_to_unicode.find(gid);
    if (cp_it != gid_to_unicode.end() && !cp_it->second.empty()) {
      if (cp_it->second.size() == 1) {
        cp_str = absl::StrFormat("U+%X, ", *cp_it->second.min());
      } else {
        std::vector<std::string> hex_cps;
        for (uint32_t cp : cp_it->second) {
          hex_cps.push_back(absl::StrFormat("U+%X", cp));
        }
        cp_str =
            absl::StrCat("{", absl::StrJoin(hex_cps, ", "), "}, ");
      }
    }

    std::string gid_str = absl::StrFormat("g%u, ", gid);
    std::string prob_str = absl::StrFormat("%.3g%%", initial_prob * 100.0);
    VLOG(0) << absl::StrFormat("Unreachable glyph %-8s%-10sP = %-9s",
                               gid_str, cp_str, prob_str);
  }
  VLOG(0) << "Total unreachable glyphs: " << unreachable_glyphs.size();
}

void SegmentationContext::LogSegmentCodepointCountHistogram(
    const GlyphSegmentation& segmentation) const {
  absl::btree_map<size_t, uint32_t> codepoint_count_histogram;
  for (const auto& segment : segmentation.Segments()) {
    if (segment.codepoints.empty()) {
      continue;
    }
    codepoint_count_histogram[segment.codepoints.size()]++;
  }

  std::stringstream histogram_string;
  histogram_string << "codepoint_count, segment_count" << std::endl;
  for (const auto& [codepoint_count, segment_count] :
       codepoint_count_histogram) {
    histogram_string << codepoint_count << ", " << segment_count << std::endl;
  }
  VLOG(0) << "Segment Codepoint Count Histogram:" << std::endl
          << histogram_string.str();
}

Status SegmentationContext::ReprocessChanged(InvalidationSet modified) {
  modified.glyphs.union_set(
      dependency_closure_->SegmentsToAffectedGlyphs({modified.base_segment}));
  TransferDependencyGraphGlyphConditions(modified.glyphs);

  return GroupGlyphs(modified.glyphs, modified.segments);
}

Status SegmentationContext::ReprocessAll() {
  // Pull conditions directly out of the dep graph instead of running closure
  // processing.
  TransferDependencyGraphGlyphConditions(
      segmentation_info_->NonInitFontGlyphs());

  return GroupGlyphs(segmentation_info_->NonInitFontGlyphs(), {});
}

/*
 * Invalidates all grouping information and fully reprocesses all segments.
 */
Status SegmentationContext::ReassignInitSubset(SubsetDefinition new_def) {
  // Figure out what's going to change before making the change so that we
  // can utilize the dep graph to locate affected segments.
  new_def = TRY(glyph_closure_cache->ExpandClosure(new_def));
  GlyphSet removed_gids = SegmentationInfo().NonInitFontGlyphs();
  removed_gids.intersect(new_def.gids);

  SegmentSet segments_with_changed_defs;
  uint32_t s_index = 0;
  for (auto& s : segmentation_info_->Segments()) {
    if (s.Definition().codepoints.intersects(new_def.codepoints)) {
      segments_with_changed_defs.insert(s_index);
    }
    for (hb_tag_t f : s.Definition().feature_tags) {
      if (new_def.feature_tags.contains(f)) {
        segments_with_changed_defs.insert(s_index);
        break;
      }
    }
    s_index++;
  }

  SubsetDefinition new_def_delta = new_def;
  new_def_delta.Subtract(SegmentationInfo().InitFontSegment());
  SegmentSet segments_to_reprocess =
      TRY(dependency_closure_->SegmentsThatInteractWith(new_def_delta));

  TRYV(segmentation_info_->ReassignInitSubset(*glyph_closure_cache, new_def));

  if (target_mode_ == config::UNICODE_RANGE) {
    estimated_compression_ratio_ = std::nullopt;
    estimated_woff2_base_overhead_ = std::nullopt;
    patch_size_cache = NewPatchSizeCache(original_face.get(), brotli_quality_);
    patch_size_cache_for_init_font = NewPatchSizeCache(
        original_face.get(), init_font_brotli_quality_);
  }

  // Dep closure doesn't need the expanded affected set, as it will traverse
  // from the input segments to find the affected subgraph.
  TRYV(dependency_closure_->InitFontChanged(segments_with_changed_defs));

  // All segments depend on the init subset def, so we must reprocess
  // everything. First reset condition set information:
  GlyphConditionSet previous_glyph_condition_set = glyph_condition_set;

  glyph_condition_set.InvalidateGlyphInformation(removed_gids);
  glyph_condition_set.InvalidateGlyphInformation(segments_to_reprocess);
  inert_segments_.subtract(segments_to_reprocess);

  // Then reprocess segments:
  GlyphSet gids;
  for (segment_index_t s : segments_to_reprocess) {
    gids.union_set(dependency_closure_->SegmentsToAffectedGlyphs({s}));
  }
  TransferDependencyGraphGlyphConditions(gids);

  // the groupings can be incrementally recomputed by looking at what conditions
  // have changed.
  GlyphSet changed_gids;
  changed_gids.union_set(removed_gids);
  for (glyph_id_t gid : SegmentationInfo().NonInitFontGlyphs()) {
    const auto& new_conditions = glyph_condition_set.ConditionsFor(gid);
    if (previous_glyph_condition_set.ConditionsFor(gid) != new_conditions) {
      changed_gids.insert(gid);
    }
  }

  TRYV(GroupGlyphs(changed_gids, segments_with_changed_defs));

  return absl::OkStatus();
}

Status SegmentationContext::ResetSegments(std::vector<Segment> segments) {
  segmentation_info_->ResetSegments(std::move(segments));
  TRYV(dependency_closure_->SegmentsReset());
  uint32_t glyph_count = hb_face_get_glyph_count(original_face.get());
  glyph_condition_set = GlyphConditionSet(glyph_count);
  glyph_groupings = GlyphGroupings(
      glyph_count,
      condition_analysis_mode_ == config::DEP_GRAPH_ONLY_WITH_SIMPLIFICATION);
  inert_segments_.clear();
  return ReprocessAll();
}

StatusOr<SegmentationContext>
SegmentationContext::InitializeSegmentationContext(
    hb_face_t* face, SubsetDefinition initial_segment,
    std::vector<Segment> segments,
    ConditionAnalysisMode condition_analysis_mode, uint32_t brotli_quality,
    uint32_t init_font_brotli_quality,
    std::shared_ptr<DataFileResolver> resolver,
    ift::config::TargetMode target_mode) {
  if (!hb_face_get_glyph_count(face)) {
    return absl::InvalidArgumentError("Provided font has no glyphs.");
  }

  // The IFT compiler has a set of defaults always included in the initial font
  // add them here so we correctly factor them into the generated segmentation.
  AddInitSubsetDefaults(initial_segment);

  // No merging is done during init.
  SegmentationContext context = TRY(SegmentationContext::Create(
      face, initial_segment, segments, condition_analysis_mode, brotli_quality,
      init_font_brotli_quality, std::move(resolver), target_mode));

  // ### Generate the initial conditions and groupings by processing all
  // segments and glyphs. ###
  VLOG(0) << "Forming initial segmentation plan.";
  TRYV(context.ReprocessAll());

  return context;
}

void SegmentationContext::TransferDependencyGraphGlyphConditions(
    const GlyphSet& gids) {
  const auto& conditions = dependency_closure_->AllGlyphConditions();
  for (glyph_id_t g : gids) {
    ActivationCondition condition = conditions.at(g);
    if (condition.IsUnitary()) {
      condition = ActivationCondition::exclusive_segment(
          *condition.TriggeringSegments().begin(), 0);
    } else if (condition_analysis_mode_ ==
                   config::DEP_GRAPH_ONLY_WITH_SIMPLIFICATION &&
               !condition.IsPurelyConjunctive() &&
               !condition.IsPurelyDisjunctive()) {
      // In DEP_GRAPH_ONLY_WITH_SIMPLIFICATION mode mixed conditions are
      // simplified into a disjunctive superset.
      // TODO(garretrieger): it may be better to do this conversion during dep
      // graph construction, since it could it some cases reduce the cost of dep
      // graph construction signficantly.
      condition = condition.NonCompositeSuperset();
    }
    glyph_condition_set.SetCondition(g, condition);
  }
  inert_segments_ = dependency_closure_->InertSegments();
}

}  // namespace ift::encoder