#include "ift/encoder/unicode_range_compiler.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "hb-ot.h"
#include "hb-subset.h"
#include "hb.h"
#include "ift/common/font_data.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/common/woff2.h"
#include "ift/encoder/init_subset_defaults.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"

using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using absl::StrFormat;
using ift::common::CodepointSet;
using ift::common::FontData;
using ift::common::GlyphSet;
using ift::common::hb_face_unique_ptr;
using ift::common::IntSet;
using ift::common::make_hb_face;
using ift::common::Woff2;

namespace ift::encoder {

Status UnicodeRangeCompiler::SetInitSubsetFromDef(
    const SubsetDefinition& init_subset) {
  if (!init_subset_.Empty()) {
    return absl::FailedPreconditionError("Base subset has already been set.");
  }
  init_subset_ = init_subset;
  return absl::OkStatus();
}

Status UnicodeRangeCompiler::AddGlyphDataPatch(uint32_t patch_id,
                                               const IntSet& gids) {
  if (glyph_data_patches_.contains(patch_id)) {
    return absl::FailedPreconditionError(
        StrCat("Glyph data patch with id ", patch_id, " already exists."));
  }
  if (gids.empty()) {
    return absl::InvalidArgumentError(
        StrCat("Glyph data patch ", patch_id, " is empty."));
  }
  GlyphSet glyph_set;
  glyph_set.union_set(gids);
  glyph_data_patches_[patch_id] = std::move(glyph_set);
  return absl::OkStatus();
}

Status UnicodeRangeCompiler::AddSegment(segment_index_t segment_id,
                                        const SubsetDefinition& segment) {
  if (segments_.contains(segment_id)) {
    return absl::FailedPreconditionError(
        StrCat("Segment with id ", segment_id, " already exists."));
  }
  if (!segment.feature_tags.empty()) {
    return absl::InvalidArgumentError(
        StrCat("Segment ", segment_id,
               " contains layout features, which are not supported in "
               "UNICODE_RANGE mode."));
  }
  segments_[segment_id] = segment;
  return absl::OkStatus();
}

Status UnicodeRangeCompiler::AddGlyphPatchCondition(segment_index_t segment_id,
                                                    uint32_t patch_id) {
  glyph_patch_conditions_.emplace_back(segment_id, patch_id);
  return absl::OkStatus();
}

std::string UnicodeRangeCompiler::FormatUnicodeRange(
    const CodepointSet& codepoints) {
  std::vector<std::string> parts;
  std::optional<uint32_t> range_start;
  uint32_t range_end = 0;

  auto flush_range = [&]() {
    if (!range_start.has_value()) {
      return;
    }
    if (*range_start == range_end) {
      parts.push_back(StrFormat("U+%X", *range_start));
    } else {
      parts.push_back(StrFormat("U+%X-%X", *range_start, range_end));
    }
    range_start = std::nullopt;
  };

  for (uint32_t cp : codepoints) {
    if (!range_start.has_value()) {
      range_start = cp;
      range_end = cp;
    } else if (cp == range_end + 1) {
      range_end = cp;
    } else {
      flush_range();
      range_start = cp;
      range_end = cp;
    }
  }
  flush_range();

  return absl::StrJoin(parts, ", ");
}

static std::optional<std::string> GetNameEntryUtf8(hb_face_t* face,
                                                   hb_ot_name_id_t name_id,
                                                   hb_language_t language) {
  unsigned int text_size = 0;
  unsigned int len =
      hb_ot_name_get_utf8(face, name_id, language, &text_size, nullptr);
  if (len == 0) {
    return std::nullopt;
  }
  std::string buffer(len + 1, '\0');
  unsigned int buffer_size = buffer.size();
  hb_ot_name_get_utf8(face, name_id, language, &buffer_size, buffer.data());
  buffer.resize(buffer_size);
  return buffer;
}

std::string UnicodeRangeCompiler::ExtractFontFamily(hb_face_t* face) {
  if (face == nullptr) {
    return "font";
  }
  hb_language_t en_lang = hb_language_from_string("en", -1);
  for (hb_ot_name_id_t name_id :
       {HB_OT_NAME_ID_TYPOGRAPHIC_FAMILY, HB_OT_NAME_ID_FONT_FAMILY}) {
    for (hb_language_t lang : {en_lang, HB_LANGUAGE_INVALID}) {
      auto name = GetNameEntryUtf8(face, name_id, lang);
      if (name.has_value() && !name->empty()) {
        return *name;
      }
    }
  }
  return "font";
}

StatusOr<UnicodeRangeCompiler::Encoding> UnicodeRangeCompiler::Compile() const {
  if (face_.get() == nullptr) {
    return absl::FailedPreconditionError("Input font face is not set.");
  }

  hb_face_unique_ptr preprocessed_face =
      make_hb_face(hb_subset_preprocess(face_.get()));

  SubsetDefinition base_subset = init_subset_;
  AddInitSubsetDefaults(base_subset);

  std::string family =
      font_family_.empty() ? ExtractFontFamily(face_.get()) : font_family_;
  std::string ext = woff2_encode_ ? ".woff2" : ".ttf";
  std::string format_str = woff2_encode_ ? "woff2" : "truetype";

  Encoding result;
  for (const auto& [segment_id, patch_id] : glyph_patch_conditions_) {
    auto seg_it = segments_.find(segment_id);
    if (seg_it == segments_.end()) {
      return absl::InvalidArgumentError(
          StrCat("Segment id ", segment_id, " not found."));
    }
    const SubsetDefinition& segment = seg_it->second;
    if (segment.codepoints.empty()) {
      return absl::InvalidArgumentError(
          StrCat("Segment ", segment_id, " has no codepoints."));
    }

    auto patch_it = glyph_data_patches_.find(patch_id);
    if (patch_it == glyph_data_patches_.end()) {
      return absl::InvalidArgumentError(
          StrCat("Glyph patch id ", patch_id, " not found."));
    }
    const GlyphSet& patch_gids = patch_it->second;

    SubsetDefinition subset_def = base_subset;
    subset_def.Union(segment);
    subset_def.gids.union_set(patch_gids);

    hb_subset_input_t* input = hb_subset_input_create_or_fail();
    if (!input) {
      return absl::InternalError("Failed to create subset input.");
    }
    subset_def.ConfigureInput(input, preprocessed_face.get());

    hb_face_unique_ptr subset_face =
        make_hb_face(hb_subset_or_fail(preprocessed_face.get(), input));
    hb_subset_input_destroy(input);
    if (!subset_face.get()) {
      return absl::InternalError(
          StrCat("Failed to create font subset for patch ", patch_id, "."));
    }

    FontData subset_data(subset_face.get());
    if (woff2_encode_) {
      subset_data = TRY(Woff2::EncodeWoff2(
          subset_data.str(), /*glyf_transform=*/true, brotli_quality_));
    }

    std::string filename = StrCat(font_name_, ".", patch_id, ext);
    if (result.subsets.contains(filename)) {
      return absl::InvalidArgumentError(
          StrCat("Duplicate subset for patch id ", patch_id, "."));
    }
    result.subsets.emplace(filename, std::move(subset_data));

    if (!result.css.empty()) {
      absl::StrAppend(&result.css, "\n");
    }
    absl::StrAppend(&result.css, "@font-face {\n", "  font-family: '", family,
                    "';\n", "  src: url('", filename, "') format('", format_str,
                    "');\n",
                    "  unicode-range: ", FormatUnicodeRange(segment.codepoints),
                    ";\n", "}\n");
  }

  return result;
}

}  // namespace ift::encoder
