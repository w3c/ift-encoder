#include "ift/encoder/complex_condition_finder.h"

#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "common/int_set.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/types.h"

using absl::btree_map;
using absl::flat_hash_map;
using absl::Status;
using absl::StatusOr;
using common::GlyphSet;
using common::SegmentSet;

// For more information on this process see the explanation in:
// ../../docs/experimental/closure_glyph_segmentation_complex_conditions.md

namespace ift::encoder {

// One unit of work for the analysis. One from to_be_tested will be checked.
struct Task {
  // These segments are already fully analyzed and should be excluded from this
  // analysis.
  SegmentSet excluded;

  // These segments have been determined to be required.
  SegmentSet required;

  // These segments have not yet been tested.
  SegmentSet to_be_tested;

  // The set of glyphs in scope for analysis.
  GlyphSet glyphs;
};

struct Context {
  const SegmentSet all_segments;
  const RequestedSegmentationInformation* segmentation_info;
  GlyphClosureCache* glyph_closure_cache;
  std::vector<Task> queue;

 public:
  Status ScheduleInitialTasks(
      GlyphSet glyphs,
      flat_hash_map<SegmentSet, GlyphSet> existing_conditions) {
    if (glyphs.intersects(segmentation_info->InitFontGlyphs())) {
      return absl::InvalidArgumentError(
          "Can't analyze glyphs that are in the init  font.");
    }

    // Each existing condition will map to one initial task that excludes the
    // existing condition from the analysis.
    for (const auto& [segments, glyph_sub_group] : existing_conditions) {
      if (!TRY(InClosure(segments, glyph_sub_group))) {
        return absl::InvalidArgumentError(
            "The glyphs of existing conditions must be in the closure of "
            "condition segments.");
      }
      TRYV(ScheduleExistingConditionTask(segments, glyph_sub_group, glyphs));
    }

    if (glyphs.empty()) {
      return absl::OkStatus();
    }

    if (!TRY(InClosure(all_segments, glyphs))) {
      return absl::InvalidArgumentError(
          "glyphs to analyze must be in the closure of all segments.");
    }

    // If any glyphs remain that do not have existing conditions these are
    // covered by a task with no excluded segments.
    queue.push_back(Task{
        .excluded = {},
        .required = {},
        .to_be_tested = all_segments,
        .glyphs = glyphs,
    });

    return absl::OkStatus();
  }

  Status ProcessQueue(btree_map<glyph_id_t, SegmentSet>& glyph_to_conditions) {
    // TODO(garretrieger): to reduce runtime of this analysis the processing of
    // the queue could be parallelized by using a threadpool to run tasks. The
    // tasks are fully independent so this should be straightforward.
    while (!queue.empty()) {
      Task next = std::move(queue.back());
      queue.pop_back();
      TRYV(RunAnalysisTask(next, glyph_to_conditions));
    }
    return absl::OkStatus();
  }

 private:
  // Returns true if all glyphs are in the closure of segments.
  StatusOr<bool> InClosure(const SegmentSet& segments, const GlyphSet& glyphs) {
    GlyphSet closure = TRY(SegmentClosure(segments));
    return glyphs.is_subset_of(closure);
  }

  StatusOr<std::pair<GlyphSet, SegmentSet>> HasAdditionalConditions(
      const SegmentSet& segments, const GlyphSet& glyphs) {
    SegmentSet except = all_segments;
    except.subtract(segments);
    GlyphSet closure_glyphs = TRY(SegmentClosure(except));
    closure_glyphs.intersect(glyphs);
    return std::make_pair(closure_glyphs, std::move(except));
  }

  Status ScheduleExistingConditionTask(const SegmentSet& condition,
                                       const GlyphSet& condition_glyphs,
                                       GlyphSet& all_glyphs) {
    // We need to check if there are any additional conditions,
    // if there aren't there is no need to schedule the analysis.
    auto [glyphs_with_additional_conditions, except] =
        TRY(HasAdditionalConditions(condition, condition_glyphs));

    if (glyphs_with_additional_conditions.empty()) {
      return absl::OkStatus();
    }

    queue.push_back(Task{
        .excluded = condition,
        .required = {},
        .to_be_tested = except,
        .glyphs = glyphs_with_additional_conditions,
    });
    all_glyphs.subtract(condition_glyphs);

    return absl::OkStatus();
  }

  // Each analysis step checks one segment to see for which glyphs that segment
  // is required. The supplied task data structure gives the specific state
  // around which the segment is tested.
  //
  // To test a segment a closure is run without the segment being tested:
  // - For inscope glyphs which appear in the closure the test segment is not
  //   required for these glyphs
  // - For inscope glyphs which do not appear in the closure the test segment is
  //   required for these glyphs.
  //
  // Based on the anlysis results up to two more analysis steps are spawned (one
  // for glyphs where segment is required, the other where it is not required)
  // to test the next segment.
  //
  // Once all segments are tested the resulting minimal set of required segments
  // is recorded in out. Lastly, the non-required segments are checked to see
  // if additional conditions are present, if they are another analysis task is
  // queued to discover the additional conditions.
  Status RunAnalysisTask(
      Task task, btree_map<glyph_id_t, SegmentSet>& glyph_to_conditions) {
    if (task.glyphs.empty()) {
      // Nothing left to check.
      return absl::OkStatus();
    }

    if (task.to_be_tested.empty()) {
      return RecordMinimalCondition(task, glyph_to_conditions);
    }

    segment_index_t test_segment = *task.to_be_tested.min();
    task.to_be_tested.erase(test_segment);

    SegmentSet closure_segments = task.required;
    closure_segments.union_set(task.to_be_tested);
    GlyphSet closure_glyphs = TRY(SegmentClosure(closure_segments));

    GlyphSet needs_test_segment = task.glyphs;
    needs_test_segment.subtract(closure_glyphs);
    GlyphSet doesnt_need_test_segment = task.glyphs;
    doesnt_need_test_segment.intersect(closure_glyphs);

    queue.push_back(Task{
        .excluded = task.excluded,
        .required = task.required,
        .to_be_tested = task.to_be_tested,
        .glyphs = doesnt_need_test_segment,
    });

    task.required.insert(test_segment);
    queue.push_back(Task{
        .excluded = task.excluded,
        .required = task.required,
        .to_be_tested = task.to_be_tested,
        .glyphs = needs_test_segment,
    });

    return absl::OkStatus();
  }

  // A minimal condition has been found, record it and kick off any
  // further analysis needed for additional conditions.
  Status RecordMinimalCondition(
      Task task, btree_map<glyph_id_t, SegmentSet>& glyph_to_conditions) {
    for (glyph_id_t gid : task.glyphs) {
      glyph_to_conditions[gid].union_set(task.required);
    }

    // We have identified a minimal set of required segments for glyphs,
    // however as usual there may be remaining additional conditions which we
    // need to check for
    task.excluded.union_set(task.required);
    auto [additional_condition_glyphs, remaining] =
        TRY(HasAdditionalConditions(task.excluded, task.glyphs));

    // Anything left in glyphs has additional conditions, recurse again to
    // analyze them further
    queue.push_back(Task{
        .excluded = task.excluded,
        .required = {},
        .to_be_tested = remaining,
        .glyphs = additional_condition_glyphs,
    });
    return absl::OkStatus();
  }

  SubsetDefinition CombinedDefinition(const SegmentSet& segments) {
    SubsetDefinition def;
    for (segment_index_t s : segments) {
      def.Union(segmentation_info->Segments().at(s).Definition());
    }
    return def;
  }

  StatusOr<GlyphSet> SegmentClosure(const SegmentSet& segments) {
    SubsetDefinition closure_def = CombinedDefinition(segments);
    // Init font subset definition must be part of the closure input
    // since it contributes to reachability of things.
    closure_def.Union(segmentation_info->InitFontSegment());
    return glyph_closure_cache->GlyphClosure(closure_def);
  }
};

static flat_hash_map<SegmentSet, GlyphSet> ExistingConditions(
    const GlyphConditionSet& glyph_condition_set, const GlyphSet& glyphs,
    btree_map<glyph_id_t, SegmentSet>& glyph_to_conditions) {
  flat_hash_map<SegmentSet, GlyphSet> existing_conditions;
  for (glyph_id_t gid : glyphs) {
    SegmentSet or_segments = glyph_condition_set.ConditionsFor(gid).or_segments;
    if (or_segments.empty()) {
      continue;
    }
    existing_conditions[or_segments].insert(gid);
    glyph_to_conditions[gid].union_set(or_segments);
  }
  return existing_conditions;
}

static SegmentSet NonEmptySegments(
    const RequestedSegmentationInformation& segmentation_info) {
  SegmentSet segments;
  for (segment_index_t s = 0; s < segmentation_info.Segments().size(); s++) {
    if (segmentation_info.Segments().at(s).Definition().Empty()) {
      continue;
    }
    segments.insert(s);
  }
  return segments;
}

StatusOr<btree_map<SegmentSet, GlyphSet>> FindMinimalDisjunctiveConditionsFor(
    const RequestedSegmentationInformation& segmentation_info,
    const GlyphConditionSet& glyph_condition_set,
    GlyphClosureCache& closure_cache, GlyphSet glyphs) {
  VLOG(0) << "Analyzing " << glyphs.size()
          << " unmapped glyphs with the complex condition detector.";

  // TODO(garretrieger): we should see which unicodes (and thus which segments)
  // may interact with the GSUB table. Any segments which don't interact with
  // GSUB will already have relavent conditions discovered via the standard
  // closure analysis. Only segments which interact with GSUB may be part of
  // complex conditions (since complex conditions required at least one 'AND'
  // which only GSUB can introduce). As a result we can exclude any segments
  // with no GSUB interaction from this analysis which should significantly
  // speed things up.
  Context context{
      .all_segments = NonEmptySegments(segmentation_info),
      .segmentation_info = &segmentation_info,
      .glyph_closure_cache = &closure_cache,
      .queue = {},
  };

  // We may already have some partial conditions generated for the fallback
  // glyphs, preload these into the output and schedule the initial tasks
  // excluding those segments.
  btree_map<glyph_id_t, SegmentSet> glyph_to_conditions;
  TRYV(context.ScheduleInitialTasks(
      std::move(glyphs),
      ExistingConditions(glyph_condition_set, glyphs, glyph_to_conditions)));

  TRYV(context.ProcessQueue(glyph_to_conditions));

  btree_map<SegmentSet, GlyphSet> grouped_out;
  for (const auto& [gid, segments] : glyph_to_conditions) {
    grouped_out[segments].insert(gid);
  }

  VLOG(0) << "Found " << grouped_out.size()
          << " new conditions for the unmapped glyphs.";

  return grouped_out;
}

}  // namespace ift::encoder