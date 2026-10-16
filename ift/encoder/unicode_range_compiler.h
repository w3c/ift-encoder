#ifndef IFT_ENCODER_UNICODE_RANGE_COMPILER_H_
#define IFT_ENCODER_UNICODE_RANGE_COMPILER_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "hb.h"
#include "ift/common/font_data.h"
#include "ift/common/int_set.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"

namespace ift::encoder {

// Compiles a font and a UNICODE_RANGE segmentation plan into a set of
// standalone WOFF2 (or OpenType) font subsets and a CSS stylesheet with
// @font-face unicode-range rules.
class UnicodeRangeCompiler {
 public:
  struct Encoding {
    std::string css;
    absl::btree_map<std::string, ift::common::FontData> subsets;
  };

  UnicodeRangeCompiler() : face_(ift::common::make_hb_face(nullptr)) {}

  UnicodeRangeCompiler(const UnicodeRangeCompiler&) = delete;
  UnicodeRangeCompiler(UnicodeRangeCompiler&&) = delete;
  UnicodeRangeCompiler& operator=(const UnicodeRangeCompiler&) = delete;
  UnicodeRangeCompiler& operator=(UnicodeRangeCompiler&&) = delete;

  void SetFace(hb_face_t* face) { face_.reset(hb_face_reference(face)); }
  void SetWoff2Encode(bool value) { woff2_encode_ = value; }
  void SetBrotliQuality(uint32_t quality) { brotli_quality_ = quality; }
  void SetFontName(absl::string_view font_name) {
    font_name_ = std::string(font_name);
  }
  void SetFontFamily(absl::string_view font_family) {
    font_family_ = std::string(font_family);
  }

  absl::Status SetInitSubsetFromDef(const SubsetDefinition& init_subset);
  absl::Status AddGlyphDataPatch(uint32_t patch_id,
                                 const ift::common::IntSet& gids);
  absl::Status AddSegment(segment_index_t segment_id,
                          const SubsetDefinition& segment);
  absl::Status AddGlyphPatchCondition(segment_index_t segment_id,
                                      uint32_t patch_id);

  absl::StatusOr<Encoding> Compile() const;

  static std::string FormatUnicodeRange(
      const ift::common::CodepointSet& codepoints);
  static std::string ExtractFontFamily(hb_face_t* face);

 private:
  ift::common::hb_face_unique_ptr face_;
  SubsetDefinition init_subset_;
  absl::btree_map<segment_index_t, SubsetDefinition> segments_;
  absl::btree_map<uint32_t, ift::common::GlyphSet> glyph_data_patches_;
  std::vector<std::pair<segment_index_t, uint32_t>> glyph_patch_conditions_;
  std::string font_name_ = "out";
  std::string font_family_;
  bool woff2_encode_ = true;
  uint32_t brotli_quality_ = 11;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_UNICODE_RANGE_COMPILER_H_
