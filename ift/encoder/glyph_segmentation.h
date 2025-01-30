#ifndef IFT_ENCODER_GLYPH_SEGMENTATION_H_
#define IFT_ENCODER_GLYPH_SEGMENTATION_H_

#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "hb.h"

namespace ift::encoder {

/*
 * Describes how the glyphs in a font should be segmented into glyph keyed
 * patches.
 *
 * A segmentation describes the groups of glyphs belong to each patch as well as
 * the conditions under which those patches should be loaded. This gaurantees
 * that the produced set of patches and conditions will satisfy the "glyph
 * closure requirement", which is:
 *
 * The set of glyphs contained in patches loaded for a font subset definition (a
 * set of Unicode codepoints and a set of layout feature tags) through the patch
 * map tables must be a superset of those in the glyph closure of the font
 * subset definition.
 */
class GlyphSegmentation {
 public:
  typedef uint32_t segment_index_t;
  typedef uint32_t patch_id_t;
  typedef uint32_t glyph_id_t;

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
        const absl::btree_set<patch_id_t>& ids, patch_id_t activated);

    /*
     * This condition is activated if every set of patch ids intersects the
     * input subset definition. ie. input subset def intersects {p_1, p_2} AND
     * input subset def intersects {...} AND ...
     *     which is effectively: (p_1 OR p_2) AND ...
     */
    const std::vector<absl::btree_set<patch_id_t>>& conditions() const {
      return conditions_;
    }

    /*
     * The patch to load when the condition is satisfied.
     */
    patch_id_t activated() const { return activated_; }

   private:
    ActivationCondition() : conditions_(), activated_(0) {}

    std::vector<absl::btree_set<patch_id_t>> conditions_;
    patch_id_t activated_;
  };

  /*
   * Analyzes a set of codepoint segments using a subsetter closure and computes
   * a GlyphSegmentation which will satisfy the "glyph closure requirement" for
   * the provided font face.
   *
   * initial_segment is the set of codepoints that will be placed into the
   * initial ift font.
   */
   // TODO(garretrieger): also support optional feature segments.
  static absl::StatusOr<GlyphSegmentation> CodepointToGlyphSegments(
      hb_face_t* face, absl::flat_hash_set<hb_codepoint_t> initial_segment,
      std::vector<absl::flat_hash_set<hb_codepoint_t>> codepoint_segments);

  /*
   * Returns a human readable string representation of this segmentation and
   * associated activation conditions.
   */
  std::string ToString() const;

  /*
   * The list of all conditions of how the various patches in this segmentation
   * are activated.
   */
  const std::vector<ActivationCondition>& Conditions() const {
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

 private:
  absl::btree_set<glyph_id_t> init_font_glyphs_;
  absl::btree_set<glyph_id_t> unmapped_glyphs_;
  std::vector<ActivationCondition> conditions_;
  absl::btree_map<patch_id_t, absl::btree_set<glyph_id_t>> patches_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_GLYPH_SEGMENTATION_H_