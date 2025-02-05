#include "ift/encoder/glyph_segmentation.h"

#include <cstdio>
#include <sstream>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "common/font_data.h"
#include "common/hb_set_unique_ptr.h"
#include "common/try.h"
#include "hb-subset.h"

using absl::btree_map;
using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::Status;
using absl::StatusOr;
using common::hb_face_unique_ptr;
using common::hb_set_unique_ptr;
using common::make_hb_set;

namespace ift::encoder {

// TODO(garretrieger): extensions/improvements that could be made:
// - Multi segment combination testing with GSUB dep analysis to guide.
// - Use merging and/or duplication to ensure minimum patch size.

btree_set<uint32_t> to_btree_set(const hb_set_t* set) {
  btree_set<uint32_t> out;
  uint32_t v = HB_SET_VALUE_INVALID;
  while (hb_set_next(set, &v)) {
    out.insert(v);
  }
  return out;
}

void subtract(btree_set<uint32_t>& a, const btree_set<uint32_t>& b) {
  for (auto v : b) {
    a.erase(v);
  }
}

class SegmentationContext {
 public:
  SegmentationContext(
      hb_face_t* face, const flat_hash_set<uint32_t>& initial_segment,
      const std::vector<flat_hash_set<uint32_t>>& codepoint_segments)
      : original_face(common::make_hb_face(hb_subset_preprocess(face))),
        segments(),
        initial_codepoints(make_hb_set(initial_segment)),
        all_codepoints(make_hb_set()),
        full_closure(make_hb_set()),
        initial_closure(make_hb_set()) {
    for (const auto& s : codepoint_segments) {
      segments.push_back(make_hb_set(s));
    }

    hb_set_union(all_codepoints.get(), initial_codepoints.get());
    for (const auto& s : segments) {
      hb_set_union(all_codepoints.get(), s.get());
    }

    {
      auto closure = glyph_closure(initial_codepoints.get());
      if (closure.ok()) {
        initial_closure.reset(closure->release());
      }
    }

    auto closure = glyph_closure(all_codepoints.get());
    if (closure.ok()) {
      full_closure.reset(closure->release());
    }
  }

  StatusOr<hb_set_unique_ptr> glyph_closure(const hb_set_t* codepoints) {
    hb_subset_input_t* input = hb_subset_input_create_or_fail();
    if (!input) {
      return absl::InternalError("Closure subset configuration failed.");
    }

    hb_set_union(hb_subset_input_unicode_set(input), codepoints);
    // TODO(garretrieger): configure features (and other settings) appropriately
    // based on the IFT default feature list.

    hb_subset_plan_t* plan =
        hb_subset_plan_create_or_fail(original_face.get(), input);
    hb_subset_input_destroy(input);
    if (!plan) {
      return absl::InternalError("Closure calculation failed.");
    }

    hb_map_t* new_to_old = hb_subset_plan_new_to_old_glyph_mapping(plan);
    hb_set_unique_ptr gids = common::make_hb_set();
    hb_map_values(new_to_old, gids.get());
    hb_subset_plan_destroy(plan);

    return gids;
  }

  common::hb_face_unique_ptr original_face;
  std::vector<hb_set_unique_ptr> segments;

  hb_set_unique_ptr initial_codepoints;
  hb_set_unique_ptr all_codepoints;
  hb_set_unique_ptr full_closure;
  hb_set_unique_ptr initial_closure;
  btree_set<glyph_id_t> unmapped_glyphs;
};

class GlyphConditions {
 public:
  GlyphConditions() : and_segments(make_hb_set()), or_segments(make_hb_set()) {}
  hb_set_unique_ptr and_segments;
  hb_set_unique_ptr or_segments;
};

Status AnalyzeSegment(SegmentationContext& context,
                      segment_index_t segment_index, const hb_set_t* codepoints,
                      std::vector<GlyphConditions>& gid_conditions) {
  // This function tests various closures using the segment codepoints to
  // determine what conditions are present for the inclusion of closure glyphs.
  //
  // At a high level we do the following (where s_i is the segment being
  // tested):
  //
  // * Set A: glyph closure on original font of the union of all segments.
  // * Set B: glyph closure on original font of the union of all segments except
  //          for s_i
  // * Set I: (glyph closure on original font of s_0 union s_i) - (glyph closure
  //           on original font of s_0)
  // * Set D: A - B, the set of glyphs that are dropped when s_i is removed.
  //
  // Then we know the following:
  // * Glyphs in I should be included whenever s_i is activated.
  // * s_i is necessary for glyphs in D to be required, but other segments may
  //   be needed too.
  //
  // Furthermore we can intersect I and D to produce three sets:
  // * D - I: the activation condition for these glyphs is s_i AND …
  //          Where … is one or more additional segments.
  // * I - D: the activation conditions for these glyphs is s_i OR …
  //          Where … is one or more additional segments.
  // * D intersection I: the activation conditions for these glyphs is only s_i
  hb_set_unique_ptr except_segment = make_hb_set();
  hb_set_union(except_segment.get(), context.all_codepoints.get());
  hb_set_subtract(except_segment.get(), codepoints);
  auto B_except_segment_closure =
      TRY(context.glyph_closure(except_segment.get()));

  hb_set_unique_ptr only_segment = make_hb_set();
  hb_set_union(only_segment.get(), context.initial_codepoints.get());
  hb_set_union(only_segment.get(), codepoints);
  auto I_only_segment_closure = TRY(context.glyph_closure(only_segment.get()));
  hb_set_subtract(I_only_segment_closure.get(), context.initial_closure.get());

  hb_set_unique_ptr D_dropped = make_hb_set();
  hb_set_union(D_dropped.get(), context.full_closure.get());
  hb_set_subtract(D_dropped.get(), B_except_segment_closure.get());

  hb_set_unique_ptr and_gids = make_hb_set();
  hb_set_union(and_gids.get(), D_dropped.get());
  hb_set_subtract(and_gids.get(), I_only_segment_closure.get());

  hb_set_unique_ptr or_gids = make_hb_set();
  hb_set_union(or_gids.get(), I_only_segment_closure.get());
  hb_set_subtract(or_gids.get(), D_dropped.get());

  hb_set_unique_ptr exclusive_gids = make_hb_set();
  hb_set_union(exclusive_gids.get(), I_only_segment_closure.get());
  hb_set_intersect(exclusive_gids.get(), D_dropped.get());

  hb_codepoint_t and_gid = HB_SET_VALUE_INVALID;
  while (hb_set_next(exclusive_gids.get(), &and_gid)) {
    // TODO(garretrieger): if we are assigning an exclusive gid there should be
    // no other and segments, check and error if this is violated.
    hb_set_add(gid_conditions[and_gid].and_segments.get(), segment_index);
  }
  while (hb_set_next(and_gids.get(), &and_gid)) {
    hb_set_add(gid_conditions[and_gid].and_segments.get(), segment_index);
  }

  hb_codepoint_t or_gid = HB_SET_VALUE_INVALID;
  while (hb_set_next(or_gids.get(), &or_gid)) {
    hb_set_add(gid_conditions[or_gid].or_segments.get(), segment_index);
  }

  return absl::OkStatus();
}

void GroupGlyphs(SegmentationContext& context,
                 const std::vector<GlyphConditions>& gid_conditions,
                 btree_map<btree_set<segment_index_t>, btree_set<glyph_id_t>>&
                     and_glyph_groups,
                 btree_map<btree_set<segment_index_t>, btree_set<glyph_id_t>>&
                     or_glyph_groups) {
  btree_set<segment_index_t> all_segments_set;
  for (segment_index_t s = 0; s < context.segments.size(); s++) {
    all_segments_set.insert(s);
  }

  for (glyph_id_t gid = 0; gid < gid_conditions.size(); gid++) {
    const auto& condition = gid_conditions[gid];
    if (!hb_set_is_empty(condition.and_segments.get())) {
      auto set = to_btree_set(condition.and_segments.get());
      and_glyph_groups[set].insert(gid);
    }
    if (!hb_set_is_empty(condition.or_segments.get())) {
      auto set = to_btree_set(condition.or_segments.get());
      or_glyph_groups[set].insert(gid);
    }

    if (hb_set_is_empty(condition.and_segments.get()) &&
        hb_set_is_empty(condition.or_segments.get()) &&
        !hb_set_has(context.initial_closure.get(), gid) &&
        hb_set_has(context.full_closure.get(), gid)) {
      // this glyph is not activated anywhere but is needed in the full closure
      // so add it to an activation condition of any segment.
      or_glyph_groups[all_segments_set].insert(gid);
      context.unmapped_glyphs.insert(gid);
    }
  }
}

void GlyphSegmentation::GroupsToSegmentation(
    const btree_map<btree_set<segment_index_t>, btree_set<glyph_id_t>>&
        and_glyph_groups,
    const btree_map<btree_set<segment_index_t>, btree_set<glyph_id_t>>&
        or_glyph_groups,
    GlyphSegmentation& segmentation) {
  patch_id_t next_id = 0;
  std::vector<patch_id_t> segment_to_patch_id;

  // Map segments into patch ids
  for (const auto& [and_segments, glyphs] : and_glyph_groups) {
    if (and_segments.size() != 1) {
      continue;
    }

    segment_index_t segment = *and_segments.begin();
    segmentation.patches_.insert(std::pair(next_id, glyphs));
    segmentation.conditions_.push_back(
        ActivationCondition::and_patches({next_id}, next_id));

    if (segment + 1 > segment_to_patch_id.size()) {
      segment_to_patch_id.resize(segment + 1);
    }
    segment_to_patch_id[segment] = next_id++;
  }

  for (const auto& [and_segments, glyphs] : and_glyph_groups) {
    if (and_segments.size() == 1) {
      // already processed above
      continue;
    }

    btree_set<patch_id_t> and_patches;
    for (segment_index_t segment : and_segments) {
      and_patches.insert(segment_to_patch_id[segment]);
    }

    segmentation.patches_.insert(std::pair(next_id, glyphs));
    segmentation.conditions_.push_back(
        ActivationCondition::and_patches(and_patches, next_id));
    next_id++;
  }

  for (const auto& [or_segments, glyphs] : or_glyph_groups) {
    btree_set<patch_id_t> or_patches;
    for (segment_index_t segment : or_segments) {
      or_patches.insert(segment_to_patch_id[segment]);
    }
    segmentation.patches_.insert(std::pair(next_id, glyphs));
    segmentation.conditions_.push_back(
        ActivationCondition::or_patches(or_patches, next_id));
    next_id++;
  }
}

StatusOr<GlyphSegmentation> GlyphSegmentation::CodepointToGlyphSegments(
    hb_face_t* face, flat_hash_set<hb_codepoint_t> initial_segment,
    std::vector<flat_hash_set<hb_codepoint_t>> codepoint_segments) {
  SegmentationContext context(face, initial_segment, codepoint_segments);

  std::vector<GlyphConditions> gid_conditions;
  gid_conditions.resize(hb_face_get_glyph_count(context.original_face.get()));

  segment_index_t segment_index = 0;
  for (const auto& segment : context.segments) {
    TRYV(AnalyzeSegment(context, segment_index, segment.get(), gid_conditions));
    segment_index++;
  }

  btree_map<btree_set<segment_index_t>, btree_set<glyph_id_t>> and_glyph_groups;
  btree_map<btree_set<segment_index_t>, btree_set<glyph_id_t>> or_glyph_groups;
  GroupGlyphs(context, gid_conditions, and_glyph_groups, or_glyph_groups);

  GlyphSegmentation segmentation;
  segmentation.unmapped_glyphs_ = context.unmapped_glyphs;
  segmentation.init_font_glyphs_ = to_btree_set(context.initial_closure.get());

  GroupsToSegmentation(and_glyph_groups, or_glyph_groups, segmentation);

  return segmentation;
}

GlyphSegmentation::ActivationCondition
GlyphSegmentation::ActivationCondition::and_patches(
    const absl::btree_set<patch_id_t>& ids, patch_id_t activated) {
  ActivationCondition conditions;
  conditions.activated_ = activated;

  for (auto id : ids) {
    conditions.conditions_.push_back({id});
  }

  return conditions;
}

GlyphSegmentation::ActivationCondition
GlyphSegmentation::ActivationCondition::or_patches(
    const absl::btree_set<patch_id_t>& ids, patch_id_t activated) {
  ActivationCondition conditions;
  conditions.activated_ = activated;
  conditions.conditions_.push_back(ids);

  return conditions;
}

template <typename It>
void output_set_inner(const char* prefix, const char* seperator, It begin,
                      It end, std::stringstream& out) {
  bool first = true;
  while (begin != end) {
    if (!first) {
      out << ", ";
    } else {
      first = false;
    }
    out << prefix << *(begin++);
  }
}

template <typename It>
void output_set(const char* prefix, It begin, It end, std::stringstream& out) {
  if (begin == end) {
    out << "{}";
    return;
  }

  out << "{ ";
  output_set_inner(prefix, ", ", begin, end, out);
  out << " }";
}

std::string GlyphSegmentation::ToString() const {
  std::stringstream out;
  out << "initial font: ";
  output_set("gid", InitialFontGlyphs().begin(), InitialFontGlyphs().end(),
             out);
  out << std::endl;

  for (const auto& [segment_id, gids] : GidSegments()) {
    out << "p" << segment_id << ": ";
    output_set("gid", gids.begin(), gids.end(), out);
    out << std::endl;
  }

  for (const auto& condition : Conditions()) {
    bool first = true;
    out << "if (";
    for (const auto& set : condition.conditions()) {
      if (!first) {
        out << " AND ";
      } else {
        first = false;
      }

      if (set.size() > 1) {
        out << "(";
      }
      bool first_inner = true;
      for (uint32_t id : set) {
        if (!first_inner) {
          out << " OR ";
        } else {
          first_inner = false;
        }
        out << "p" << id;
      }
      if (set.size() > 1) {
        out << ")";
      }
    }
    out << ") then p" << condition.activated() << std::endl;
  }

  return out.str();
}

}  // namespace ift::encoder