#ifndef IFT_ENCODER_GLYPH_SEGMENTATION_H_
#define IFT_ENCODER_GLYPH_SEGMENTATION_H_

#include <cstdint>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "common/hb_set_unique_ptr.h"
#include "hb.h"
#include "ift/encoder/condition.h"
#include "util/encoder_config.pb.h"

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
     * AND ... AND inersects(patch_n).
     */
    static ActivationCondition and_segments(
        const absl::btree_set<segment_index_t>& ids, patch_id_t activated);

    /*
     * Constructs a condition that activates when the input intersects
     * (patch_1) OR ... OR inersects(patch_n).
     */
    static ActivationCondition or_segments(
        const absl::btree_set<segment_index_t>& ids, patch_id_t activated,
        bool is_fallback = false);

    /*
     * Constructs a condition that activates when the input intersects:
     * (s1 OR ..) AND (si OR ...) AND ...
     */
    static ActivationCondition composite_condition(
        absl::Span<const absl::btree_set<segment_index_t>> groups,
        patch_id_t activated);

    /*
     * This condition is activated if every set of patch ids intersects the
     * input subset definition. ie. input subset def intersects {p_1, p_2} AND
     * input subset def intersects {...} AND ...
     *     which is effectively: (p_1 OR p_2) AND ...
     */
    const absl::Span<const absl::btree_set<segment_index_t>> conditions()
        const {
      return conditions_;
    }

    bool IsFallback() const { return is_fallback_; }

    /*
     * Populates out with the set of patch ids that are part of this condition
     * (excluding the activated patch)
     */
    void TriggeringSegments(hb_set_t* out) const {
      for (auto g : conditions_) {
        for (auto segment_id : g) {
          hb_set_add(out, segment_id);
        }
      }
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
      return conditions_ == other.conditions_ && activated_ == other.activated_;
    }

   private:
    ActivationCondition() : conditions_(), activated_(0) {}

    bool is_fallback_ = false;
    bool is_exclusive_ = false;
    std::vector<absl::btree_set<segment_index_t>> conditions_;
    patch_id_t activated_;
  };

  GlyphSegmentation(absl::btree_set<hb_codepoint_t> init_font_codepoints,
                    absl::btree_set<glyph_id_t> init_font_glyphs,
                    absl::btree_set<glyph_id_t> unmapped_glyphs)
      : init_font_codepoints_(init_font_codepoints),
        init_font_glyphs_(init_font_glyphs),
        unmapped_glyphs_(unmapped_glyphs) {}

  /*
   * Converts a list of activation conditions into a list of condition entries
   * which are used by the encoder to specify conditions.
   */
  static absl::StatusOr<std::vector<Condition>>
  ActivationConditionsToConditionEntries(
      absl::Span<const ActivationCondition> conditions,
      const absl::flat_hash_map<segment_index_t,
                                absl::flat_hash_set<hb_codepoint_t>>& segments);

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
  const std::vector<absl::btree_set<hb_codepoint_t>>& Segments() const {
    return segments_;
  }

  /*
   * The list of glyphs in each patch. The key in the map is an id used to
   * identify the patch within the activation conditions.
   */
  const absl::btree_map<patch_id_t, absl::btree_set<glyph_id_t>>& GidSegments()
      const {
    return patches_;
  }

  /*
   * These glyphs where unable to be grouped into patches due to complex
   * interactions.
   *
   * TODO(garretrieger): instead of treating them separately generate a catch
   * all patch that contains the unmapped glyphs.
   */
  const absl::btree_set<glyph_id_t>& UnmappedGlyphs() const {
    return unmapped_glyphs_;
  };

  /*
   * These glyphs should be included in the initial font.
   */
  const absl::btree_set<glyph_id_t>& InitialFontGlyphs() const {
    return init_font_glyphs_;
  };

  /*
   * These codepoints should be included in the initial font.
   */
  const absl::btree_set<hb_codepoint_t>& InitialFontCodepoints() const {
    return init_font_codepoints_;
  };

  EncoderConfig ToConfigProto() const;

  static absl::Status GroupsToSegmentation(
      const absl::btree_map<absl::btree_set<segment_index_t>,
                            absl::btree_set<glyph_id_t>>& and_glyph_groups,
      const absl::btree_map<absl::btree_set<segment_index_t>,
                            absl::btree_set<glyph_id_t>>& or_glyph_groups,
      const absl::btree_set<segment_index_t>& fallback_group,
      GlyphSegmentation& segmentation);

  void CopySegments(const std::vector<common::hb_set_unique_ptr>& segments);

 private:
  absl::btree_set<hb_codepoint_t> init_font_codepoints_;
  absl::btree_set<glyph_id_t> init_font_glyphs_;
  absl::btree_set<glyph_id_t> unmapped_glyphs_;
  absl::btree_set<ActivationCondition> conditions_;
  std::vector<absl::btree_set<hb_codepoint_t>> segments_;
  absl::btree_map<patch_id_t, absl::btree_set<glyph_id_t>> patches_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_GLYPH_SEGMENTATION_H_