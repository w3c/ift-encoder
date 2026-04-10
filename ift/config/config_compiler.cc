#include "ift/config/config_compiler.h"

#include <cstdint>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "hb.h"
#include "ift/common/axis_range.h"
#include "ift/common/font_helper.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/config/load_codepoints.h"
#include "ift/config/segmentation_plan.pb.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/compiler.h"
#include "ift/encoder/subset_definition.h"

using absl::flat_hash_map;
using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using ift::common::AxisRange;
using ift::common::FontHelper;
using ift::common::IntSet;
using ift::common::SegmentSet;
using ift::encoder::ActivationCondition;
using ift::encoder::Compiler;
using ift::encoder::design_space_t;
using ift::encoder::SubsetDefinition;

namespace ift::config {

static StatusOr<design_space_t> ToDesignSpace(const DesignSpace& proto) {
  design_space_t result;
  for (const auto& [tag_str, range_proto] : proto.ranges()) {
    auto range = TRY(AxisRange::Range(range_proto.start(), range_proto.end()));
    result[FontHelper::ToTag(tag_str)] = range;
  }
  return result;
}

static ActivationCondition FromProto(const ActivationConditionProto& condition) {
  // TODO(garretrieger): once glyph segmentation activation conditions can
  // support features copy those here.
  std::vector<SegmentSet> groups;
  for (const auto& group : condition.required_segments()) {
    SegmentSet set;
    set.insert(group.values().begin(), group.values().end());
    groups.push_back(set);
  }

  return ActivationCondition::composite_condition(groups,
                                                  condition.activated_patch());
}

Status ConfigCompiler::Configure(const SegmentationPlan& plan,
                                 Compiler& compiler) {
  // First configure the glyph keyed segments, including features deps
  for (const auto& [id, gids] : plan.glyph_patches()) {
    TRYV(compiler.AddGlyphDataPatch(id, Values(gids)));
  }

  std::vector<ActivationCondition> activation_conditions;
  for (const auto& c : plan.glyph_patch_conditions()) {
    activation_conditions.push_back(FromProto(c));
  }

  flat_hash_map<uint32_t, SubsetDefinition> segments;
  for (const auto& [id, set] : plan.segments()) {
    auto& segment = segments[id];
    for (hb_codepoint_t cp : set.codepoints().values()) {
      segment.codepoints.insert(cp);
    }
    for (const std::string& tag : set.features().values()) {
      segment.feature_tags.insert(FontHelper::ToTag(tag));
    }
  }

  auto condition_entries =
      TRY(ActivationCondition::ActivationConditionsToPatchMapEntries(
          activation_conditions, segments));
  for (const auto& entry : condition_entries) {
    TRYV(compiler.AddGlyphDataPatchCondition(entry));
  }

  // Initial subset definition
  auto init_codepoints = Values(plan.initial_codepoints());
  auto init_glyphs = Values(plan.initial_glyphs());
  auto init_features = TagValues(plan.initial_features());
  auto init_segments = Values(plan.initial_segments());
  auto init_design_space = TRY(ToDesignSpace(plan.initial_design_space()));

  SubsetDefinition init_subset;
  init_subset.codepoints.insert(init_codepoints.begin(), init_codepoints.end());
  init_subset.gids.insert(init_glyphs.begin(), init_glyphs.end());

  for (const auto segment_id : init_segments) {
    auto segment = segments.find(segment_id);
    if (segment == segments.end()) {
      return absl::InvalidArgumentError(
          StrCat("Segment id, ", segment_id, ", not found."));
    }

    init_subset.codepoints.union_set(segment->second.codepoints);
    init_subset.feature_tags.insert(segment->second.feature_tags.begin(),
                                    segment->second.feature_tags.end());
  }

  init_subset.feature_tags = init_features;
  init_subset.design_space = init_design_space;
  TRYV(compiler.SetInitSubsetFromDef(init_subset));

  // Next configure the table keyed segments
  for (const auto& codepoints : plan.non_glyph_codepoint_segmentation()) {
    compiler.AddNonGlyphDataSegment(Values(codepoints));
  }

  for (const auto& features : plan.non_glyph_feature_segmentation()) {
    compiler.AddFeatureGroupSegment(TagValues(features));
  }

  for (const auto& design_space_proto :
       plan.non_glyph_design_space_segmentation()) {
    auto design_space = TRY(ToDesignSpace(design_space_proto));
    compiler.AddDesignSpaceSegment(design_space);
  }

  for (const auto& segment_ids : plan.non_glyph_segments()) {
    // Because we're using (codepoints or features) we can union up to the
    // combined segment.
    SubsetDefinition combined;
    for (const auto& segment_id : segment_ids.values()) {
      auto segment = segments.find(segment_id);
      if (segment == segments.end()) {
        return absl::InvalidArgumentError(
            StrCat("Segment id, ", segment_id, ", not found."));
      }
      combined.Union(segment->second);
    }

    compiler.AddNonGlyphDataSegment(combined);
  }

  // Lastly graph shape parameters
  if (plan.jump_ahead() > 1) {
    compiler.SetJumpAhead(plan.jump_ahead());
  }
  compiler.SetUsePrefetchLists(plan.use_prefetch_lists());

  // Check for unsupported settings
  if (plan.include_all_segment_patches()) {
    return absl::UnimplementedError(
        "include_all_segment_patches is not yet supported.");
  }

  if (plan.max_depth() > 0) {
    return absl::UnimplementedError("max_depth is not yet supported.");
  }

  return absl::OkStatus();
}

}  // namespace ift::config
