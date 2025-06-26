#ifndef IFT_ENCODER_GLYPH_SEGMENTATION_H_
#define IFT_ENCODER_GLYPH_SEGMENTATION_H_

#include <cstdint>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/types/span.h"
#include "common/int_set.h"
#include "ift/encoder/subset_definition.h"
#include "ift/proto/patch_map.h"
#include "util/segmentation_plan.pb.h"

namespace ift::encoder {

typedef uint32_t segment_index_t;
typedef uint32_t patch_id_t;
typedef uint32_t glyph_id_t;

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
  /*
   * The conditions under which a group of glyphs should be laoded.
   */
  class ActivationCondition {
   public:
    /*
     * Constructs a condition that activates when the input intersects(patch_1)
     * AND ... AND inersects(patch_n).
     */
    static ActivationCondition exclusive_segment(segment_index_t index,
                                                 patch_id_t activated);

    /*
     * Constructs a condition that activates when the input intersects(patch_1)
     * AND ... AND inersects(segment_n).
     */
    static ActivationCondition and_segments(const common::SegmentSet& ids,
                                            patch_id_t activated);

    /*
     * Constructs a condition that activates when the input intersects
     * (segment_1) OR ... OR inersects(segment_n).
     */
    static ActivationCondition or_segments(const common::SegmentSet& ids,
                                           patch_id_t activated,
                                           bool is_fallback = false);

    /*
     * Constructs a condition that activates when the input intersects:
     * (s1 OR ..) AND (si OR ...) AND ...
     */
    static ActivationCondition composite_condition(
        absl::Span<const common::SegmentSet> groups, patch_id_t activated);

    /*
     * This condition is activated if every set of segments intersects the
     * input subset definition. ie. input subset def intersects {s_1, s_2} AND
     * input subset def intersects {...} AND ...
     *     which is effectively: (s_1 OR s_2) AND ...
     */
    const absl::Span<const common::SegmentSet> conditions() const {
      return conditions_;
    }

    bool IsFallback() const { return is_fallback_; }

    /*
     * Populates out with the set of patch ids that are part of this condition
     * (excluding the activated patch)
     */
    common::SegmentSet TriggeringSegments() const {
      common::SegmentSet out;
      for (auto g : conditions_) {
        for (auto segment_id : g) {
          out.insert(segment_id);
        }
      }
      return out;
    }

    /*
     * Returns a human readable string representation of this condition.
     */
    std::string ToString() const;

    /*
     * The patch to load when the condition is satisfied.
     */
    patch_id_t activated() const { return activated_; }

    bool IsExclusive() const { return is_exclusive_; }

    bool IsUnitary() const {
      return conditions().size() == 1 && conditions().at(0).size() == 1;
    }

    ActivationConditionProto ToConfigProto() const;

    bool operator<(const ActivationCondition& other) const;

    bool operator==(const ActivationCondition& other) const {
      return conditions_ == other.conditions_ &&
             activated_ == other.activated_ &&
             is_fallback_ == other.is_fallback_ &&
             is_exclusive_ == other.is_exclusive_;
    }

    bool operator!=(const ActivationCondition& other) const {
      return !(*this == other);
    }

   private:
    ActivationCondition() : conditions_(), activated_(0) {}

    bool is_fallback_ = false;
    bool is_exclusive_ = false;
    // Represents:
    // (s_1_1 OR s_1_2 OR ...) AND (s_2_1 OR ...) ...
    std::vector<common::SegmentSet> conditions_;
    patch_id_t activated_;
  };

  GlyphSegmentation(SubsetDefinition init_font_segment,
                    common::GlyphSet init_font_glyphs,
                    common::GlyphSet unmapped_glyphs)
      : init_font_segment_(init_font_segment),
        init_font_glyphs_(init_font_glyphs),
        unmapped_glyphs_(unmapped_glyphs) {}

  /*
   * Converts a list of activation conditions into a list of condition entries
   * which are used by the encoder to specify conditions.
   */
  static absl::StatusOr<std::vector<proto::PatchMap::Entry>>
  ActivationConditionsToPatchMapEntries(
      absl::Span<const ActivationCondition> conditions,
      const absl::flat_hash_map<segment_index_t, SubsetDefinition>& segments);

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
  const common::GlyphSet& InitialFontGlyphs() const {
    return init_font_glyphs_;
  };

  /*
   * These codepoints should be included in the initial font.
   */
  const SubsetDefinition& InitialFontSegment() const {
    return init_font_segment_;
  };

  SegmentationPlan ToSegmentationPlanProto() const;

  static absl::Status GroupsToSegmentation(
      const absl::btree_map<common::SegmentSet, common::GlyphSet>&
          and_glyph_groups,
      const absl::btree_map<common::SegmentSet, common::GlyphSet>&
          or_glyph_groups,
      const common::SegmentSet& fallback_group,
      GlyphSegmentation& segmentation);

  void CopySegments(const std::vector<SubsetDefinition>& segments);

 private:
  SubsetDefinition init_font_segment_;
  common::GlyphSet init_font_glyphs_;
  common::GlyphSet unmapped_glyphs_;
  absl::btree_set<ActivationCondition> conditions_;
  std::vector<SubsetDefinition> segments_;
  absl::btree_map<patch_id_t, common::GlyphSet> patches_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_GLYPH_SEGMENTATION_H_