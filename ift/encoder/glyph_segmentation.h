#ifndef IFT_ENCODER_GLYPH_SEGMENTATION_H_
#define IFT_ENCODER_GLYPH_SEGMENTATION_H_

#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "common/int_set.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"
#include "util/segmentation_plan.pb.h"

namespace ift::encoder {

/*
 * Describes how the glyphs in a font should be segmented into glyph keyed
 * patches.
 *
 * A segmentation describes the groups of glyphs belong to each patch as well as
 * the conditions under which those patches should be loaded. A properly formed
 * segmentation should have an associated set of patches and conditions which
 * will satisfy the "glyph closure requirement", which is:
 *
 * The set of glyphs contained in patches loaded for a font subset definition (a
 * set of Unicode codepoints and a set of layout feature tags) through the patch
 * map tables must be a superset of those in the glyph closure of the font
 * subset definition.
 */
class GlyphSegmentation {
 public:
  GlyphSegmentation(SubsetDefinition init_font_segment,
                    common::GlyphSet init_font_glyph_closure,
                    common::GlyphSet unmapped_glyphs)
      : init_font_segment_(init_font_segment),
        init_font_glyph_closure_(init_font_glyph_closure),
        unmapped_glyphs_(unmapped_glyphs) {}

  /*
   * Returns a human readable string representation of this segmentation and
   * associated activation conditions.
   */
  std::string ToString() const;

  /*
   * The list of all conditions of how the various patches in this segmentation
   * are activated.
   */
  const absl::btree_set<ActivationCondition>& Conditions() const {
    return conditions_;
  }

  /*
   * The list of codepoint segmentations that are utilized as part of
   * Conditions().
   *
   * Segment indices in conditions refer to a set of codepoints here.
   */
  const std::vector<SubsetDefinition>& Segments() const { return segments_; }

  /*
   * The list of glyphs in each patch. The key in the map is an id used to
   * identify the patch within the activation conditions.
   */
  const absl::btree_map<patch_id_t, common::GlyphSet>& GidSegments() const {
    return patches_;
  }

  /*
   * These glyphs where unable to be grouped into patches due to complex
   * interactions.
   *
   * TODO(garretrieger): instead of treating them separately generate a catch
   * all patch that contains the unmapped glyphs.
   */
  const common::GlyphSet& UnmappedGlyphs() const { return unmapped_glyphs_; };

  /*
   * These glyphs should be included in the initial font.
   */
  const common::GlyphSet& InitialFontGlyphClosure() const {
    return init_font_glyph_closure_;
  };

  /*
   * These codepoints should be included in the initial font.
   */
  const SubsetDefinition& InitialFontSegment() const {
    return init_font_segment_;
  };

  static void SubsetDefinitionToSegment(const SubsetDefinition& def,
                                        SegmentProto& segment_proto);

  SegmentationPlan ToSegmentationPlanProto() const;

  static absl::Status GroupsToSegmentation(
      const absl::btree_map<common::SegmentSet, common::GlyphSet>&
          and_glyph_groups,
      const absl::btree_map<common::SegmentSet, common::GlyphSet>&
          or_glyph_groups,
      const absl::btree_map<segment_index_t, common::GlyphSet>&
          exclusive_glyph_groups,
      const common::SegmentSet& fallback_group,
      GlyphSegmentation& segmentation);

  void CopySegments(const std::vector<SubsetDefinition>& segments);

 private:
  SubsetDefinition init_font_segment_;
  common::GlyphSet init_font_glyph_closure_;
  common::GlyphSet unmapped_glyphs_;
  absl::btree_set<ActivationCondition> conditions_;
  std::vector<SubsetDefinition> segments_;
  absl::btree_map<patch_id_t, common::GlyphSet> patches_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_GLYPH_SEGMENTATION_H_