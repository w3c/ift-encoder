#ifndef IFT_ENCODER_GLYPH_SEGMENTATION_H_
#define IFT_ENCODER_GLYPH_SEGMENTATION_H_

#include <cstdint>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/status/status.h"
#include "hb.h"

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
 * segmentation should have an associated set of patches and conditions which will
 * satisfy the "glyph closure requirement", which is:
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
  // TODO(garretrieger): merge this with Encoder::Condition they are basically
  // identical.
  class ActivationCondition {
   public:
    /*
     * Constructs a condition that activates when the input intersects(patch_1)
     * AND ... AND inersects(patch_n).
     */
    static ActivationCondition and_patches(
        const absl::btree_set<patch_id_t>& ids, patch_id_t activated);

    /*
     * Constructs a condition that activates when the input intersects(patch_1)
     * OR ... OR inersects(patch_n).
     */
    static ActivationCondition or_patches(
        const absl::btree_set<patch_id_t>& ids, patch_id_t activated,
        bool is_fallback = false);

    /*
     * This condition is activated if every set of patch ids intersects the
     * input subset definition. ie. input subset def intersects {p_1, p_2} AND
     * input subset def intersects {...} AND ...
     *     which is effectively: (p_1 OR p_2) AND ...
     */
    const std::vector<absl::btree_set<patch_id_t>>& conditions() const {
      return conditions_;
    }

    bool IsFallback() const { return is_fallback_; }

    /*
     * Populates out with the set of patch ids that are part of this condition
     * (excluding the activated patch)
     */
    void TriggeringPatches(hb_set_t* out) const {
      for (auto g : conditions_) {
        for (auto patch_id : g) {
          hb_set_add(out, patch_id);
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

    bool IsExclusive() const {
      if (conditions_.size() == 1) {
        const auto& ids = conditions_.at(0);
        if (ids.size() == 1) {
          return *ids.begin() == activated_;
        }
      }
      return false;
    }

    bool operator<(const ActivationCondition& other) const;

    bool operator==(const ActivationCondition& other) const {
      return conditions_ == other.conditions_ && activated_ == other.activated_;
    }

   private:
    ActivationCondition() : conditions_(), activated_(0) {}

    bool is_fallback_ = false;
    std::vector<absl::btree_set<patch_id_t>> conditions_;
    patch_id_t activated_;
  };


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

  static absl::Status GroupsToSegmentation(
      const absl::btree_map<absl::btree_set<segment_index_t>,
                            absl::btree_set<glyph_id_t>>& and_glyph_groups,
      const absl::btree_map<absl::btree_set<segment_index_t>,
                            absl::btree_set<glyph_id_t>>& or_glyph_groups,
      const absl::btree_set<segment_index_t>& fallback_group,
      std::vector<segment_index_t>& patch_id_to_segment_index,
      GlyphSegmentation& segmentation);

  // TODO(garretrieger): the output conditions need to also capture the base
  // codepoint segmentations since those
  //                     form the base conditions which composite conditions are
  //                     built up from.
  absl::btree_set<glyph_id_t> init_font_glyphs_;
  absl::btree_set<glyph_id_t> unmapped_glyphs_;
  absl::btree_set<ActivationCondition> conditions_;
  absl::btree_map<patch_id_t, absl::btree_set<glyph_id_t>> patches_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_GLYPH_SEGMENTATION_H_