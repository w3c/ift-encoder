#include "ift/common/font_helper.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <variant>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "hb-ot.h"
#include "hb-subset.h"
#include "hb.h"
#include "ift/common/axis_range.h"
#include "ift/common/font_data.h"
#include "ift/common/hb_set_unique_ptr.h"
#include "ift/common/indexed_data_reader.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"

using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using absl::string_view;
using ift::common::FontData;

namespace ift::common {

static StatusOr<uint32_t> OffsetTo(const char* start, size_t length, const char* ptr) {
  uintptr_t start_val = reinterpret_cast<uintptr_t>(start);
  uintptr_t ptr_val = reinterpret_cast<uintptr_t>(ptr);
  uintptr_t end_val = start_val + length;

  if (ptr_val < start_val || ptr_val > end_val) {
    return absl::InternalError("Pointer is not within the specified block.");
  }
  uintptr_t offset = ptr_val - start_val;
  if (offset > UINT32_MAX) {
    return absl::InternalError("Offset overflows uint32_t.");
  }
  return static_cast<uint32_t>(offset);
}

size_t CompareTableOffsets::table_offset(hb_tag_t tag) const {
  if (!start || !length) {
    return 0;
  }

  hb_blob_unique_ptr table = make_hb_blob(hb_face_reference_table(face.get(), tag));
  const char* table_start = reinterpret_cast<const char*>(hb_blob_get_data(table.get(), nullptr));

  auto offset = OffsetTo(reinterpret_cast<const char*>(start), length, table_start);
  if (!offset.ok()) {
    return 0;
  }
  return *offset;
}

StatusOr<bool> FontHelper::HasLongLoca(const hb_face_t* face) {
  FontData head = TableData(face, kHead);
  if (head.size() < 52) {
    return absl::InvalidArgumentError("head table is missing or truncated.");
  }
  return (bool)head.str()[51];
}

bool FontHelper::HasWideGvar(const hb_face_t* face) {
  auto gvar = TableData(face, kGvar);
  if (gvar.empty()) {
    return false;
  }

  constexpr uint32_t gvar_flags_offset = 15;
  if (gvar.size() < gvar_flags_offset + 1) {
    return false;
  }

  return (((uint8_t)gvar.str()[gvar_flags_offset]) & 0x01);
}

namespace {

class IndexedTableReader {
 public:
  IndexedTableReader(FontData offsets_table, FontData data_table,
                     std::variant<IndexedDataReader<uint16_t, 2>,
                                  IndexedDataReader<uint32_t, 1>>
                         reader)
      : offsets_table_(std::move(offsets_table)),
        data_table_(std::move(data_table)),
        reader_(std::move(reader)) {}

  IndexedTableReader(const IndexedTableReader&) = delete;
  IndexedTableReader& operator=(const IndexedTableReader&) = delete;
  IndexedTableReader(IndexedTableReader&&) = default;
  IndexedTableReader& operator=(IndexedTableReader&&) = default;

  StatusOr<string_view> DataFor(uint32_t gid) const {
    return std::visit([gid](const auto& r) { return r.DataFor(gid); }, reader_);
  }

 private:
  FontData offsets_table_;
  FontData data_table_;
  std::variant<IndexedDataReader<uint16_t, 2>, IndexedDataReader<uint32_t, 1>>
      reader_;
};

StatusOr<IndexedTableReader> CreateGlyfReader(const hb_face_t* face) {
  FontData loca = FontHelper::TableData(face, FontHelper::kLoca);
  if (loca.empty()) {
    return absl::NotFoundError("loca table was not found.");
  }

  bool has_long_loca = TRY(FontHelper::HasLongLoca(face));
  FontData glyf = FontHelper::TableData(face, FontHelper::kGlyf);
  string_view loca_str = loca.str();
  string_view glyf_str = glyf.str();
  if (has_long_loca) {
    return IndexedTableReader(
        std::move(loca), std::move(glyf),
        IndexedDataReader<uint32_t, 1>(loca_str, glyf_str));
  }
  return IndexedTableReader(std::move(loca), std::move(glyf),
                            IndexedDataReader<uint16_t, 2>(loca_str, glyf_str));
}

StatusOr<IndexedTableReader> CreateGvarReader(const hb_face_t* face) {
  FontData gvar = FontHelper::TableData(face, FontHelper::kGvar);
  if (gvar.empty()) {
    return absl::NotFoundError("gvar not in the font.");
  }

  constexpr uint32_t glyph_count_offset = 12;
  constexpr uint32_t gvar_flags_offset = 15;
  constexpr uint32_t data_array_offset = 16;
  constexpr uint32_t gvar_offsets_table_offset = 20;

  if (gvar.size() < gvar_offsets_table_offset) {
    return absl::InvalidArgumentError("gvar table is too short.");
  }

  string_view gvar_str = gvar.str();
  uint16_t glyph_count =
      TRY(FontHelper::ReadUInt16(gvar_str.substr(glyph_count_offset)));
  uint32_t data_offset =
      TRY(FontHelper::ReadUInt32(gvar_str.substr(data_array_offset)));
  if (data_offset > gvar_str.size()) {
    return absl::InvalidArgumentError("gvar data offset exceeds table size.");
  }

  bool is_wide = (((uint8_t)gvar_str[gvar_flags_offset]) & 0x01);
  string_view data = gvar_str.substr(data_offset);
  if (is_wide) {
    string_view offsets =
        gvar_str.substr(gvar_offsets_table_offset, (glyph_count + 1) * 4);
    return IndexedTableReader(FontData(), std::move(gvar),
                              IndexedDataReader<uint32_t, 1>(offsets, data));
  }

  string_view offsets =
      gvar_str.substr(gvar_offsets_table_offset, (glyph_count + 1) * 2);
  return IndexedTableReader(FontData(), std::move(gvar),
                            IndexedDataReader<uint16_t, 2>(offsets, data));
}

}  // namespace

absl::StatusOr<string_view> FontHelper::GlyfData(const hb_face_t* face,
                                                 uint32_t gid) {
  IndexedTableReader reader = TRY(CreateGlyfReader(face));
  return reader.DataFor(gid);
}

StatusOr<string_view> FontHelper::GvarData(const hb_face_t* face,
                                           uint32_t gid) {
  IndexedTableReader reader = TRY(CreateGvarReader(face));
  return reader.DataFor(gid);
}

FontData FontHelper::CffData(hb_face_t* face, uint32_t gid) {
  hb_blob_t* data_blob = hb_subset_cff_get_charstring_data(face, gid);
  FontData data(data_blob);
  hb_blob_destroy(data_blob);
  return data;
}

FontData FontHelper::Cff2Data(hb_face_t* face, uint32_t gid) {
  hb_blob_t* data_blob = hb_subset_cff2_get_charstring_data(face, gid);
  FontData data(data_blob);
  hb_blob_destroy(data_blob);
  return data;
}

StatusOr<uint32_t> FontHelper::TotalGlyphData(hb_face_t* face,
                                              const GlyphSet& gids) {
  if (gids.empty()) {
    return 0;
  }

  flat_hash_set<hb_tag_t> tags = FontHelper::GetTags(face);

  std::optional<IndexedTableReader> glyf_reader;
  if (tags.contains(FontHelper::kGlyf)) {
    glyf_reader.emplace(TRY(CreateGlyfReader(face)));
  }

  std::optional<IndexedTableReader> gvar_reader;
  if (tags.contains(FontHelper::kGvar)) {
    gvar_reader.emplace(TRY(CreateGvarReader(face)));
  }

  bool has_cff = tags.contains(FontHelper::kCFF);
  bool has_cff2 = tags.contains(FontHelper::kCFF2);

  uint32_t total = 0;
  for (uint32_t gid : gids) {
    if (glyf_reader.has_value()) {
      total += TRY(glyf_reader->DataFor(gid)).size();
    }

    if (gvar_reader.has_value()) {
      total += TRY(gvar_reader->DataFor(gid)).size();
    }

    if (has_cff) {
      total += FontHelper::CffData(face, gid).size();
    }

    if (has_cff2) {
      total += FontHelper::Cff2Data(face, gid).size();
    }
  }

  return total;
}

StatusOr<GlyphSet> FontHelper::EmptyGlyphs(hb_face_t* face,
                                           const GlyphSet& gids) {
  GlyphSet empty_glyphs;
  if (gids.empty()) {
    return empty_glyphs;
  }

  uint32_t glyph_count = hb_face_get_glyph_count(face);
  if (*gids.max() >= glyph_count) {
    return absl::NotFoundError(
        StrCat("Glyph ", *gids.max(), " exceeds glyph count ", glyph_count));
  }

  flat_hash_set<hb_tag_t> tags = FontHelper::GetTags(face);

  std::optional<IndexedTableReader> glyf_reader;
  if (tags.contains(kGlyf)) {
    glyf_reader.emplace(TRY(CreateGlyfReader(face)));
  }

  std::optional<IndexedTableReader> gvar_reader;
  if (tags.contains(kGvar)) {
    gvar_reader.emplace(TRY(CreateGvarReader(face)));
  }

  bool has_cff = tags.contains(kCFF);
  bool has_cff2 = tags.contains(kCFF2);

  for (uint32_t gid : gids) {
    bool is_empty = true;
    if (glyf_reader.has_value()) {
      is_empty &= TRY(glyf_reader->DataFor(gid)).empty();
    }

    if (gvar_reader.has_value()) {
      is_empty &= TRY(gvar_reader->DataFor(gid)).empty();
    }

    if (is_empty && has_cff) {
      is_empty = FontHelper::CffData(face, gid).empty();
    }

    if (is_empty && has_cff2) {
      is_empty = FontHelper::Cff2Data(face, gid).empty();
    }

    if (is_empty) {
      empty_glyphs.insert(gid);
    }
  }

  return empty_glyphs;
}

Status FontHelper::Cff2GetCharstrings(hb_face_t* face,
                                      FontData& non_charstrings,
                                      FontData& charstrings) {
  FontData cff2_data = FontHelper::TableData(face, FontHelper::kCFF2);
  hb_blob_unique_ptr cff2_data_blob = cff2_data.blob();

  hb_blob_unique_ptr charstrings_index_blob =
      make_hb_blob(hb_subset_cff2_get_charstrings_index(face));
  charstrings.set(charstrings_index_blob.get());

  const char* cff2_start = cff2_data.data();
  const char* charstrings_start = charstrings.data();
  uint32_t non_charstrings_length = TRY(OffsetTo(cff2_start, cff2_data.size(), charstrings_start));
  hb_blob_unique_ptr non_charstrings_blob = make_hb_blob(
      hb_blob_create_sub_blob(cff2_data_blob.get(), 0, non_charstrings_length));
  non_charstrings.set(non_charstrings_blob.get());

  return absl::OkStatus();
}

static StatusOr<std::optional<uint32_t>> CharStringsOffset(
    hb_blob_t* all_data, hb_blob_t* charstrings_data) {
  unsigned all_data_length = 0;
  const char* cff_start = hb_blob_get_data(all_data, &all_data_length);
  if (all_data_length == 0) {
    // No CFF/CFF2 table.
    return std::nullopt;
  }

  unsigned charstrings_length = 0;
  const char* charstrings_start =
      hb_blob_get_data(charstrings_data, &charstrings_length);

  return TRY(OffsetTo(cff_start, all_data_length, charstrings_start));
}

StatusOr<std::optional<uint32_t>> FontHelper::CffCharStringsOffset(
    hb_face_t* face) {
  FontData cff_data = FontHelper::TableData(face, FontHelper::kCFF);
  hb_blob_unique_ptr cff_data_blob = cff_data.blob();
  hb_blob_unique_ptr charstrings_index_blob =
      make_hb_blob(hb_subset_cff_get_charstrings_index(face));

  return CharStringsOffset(cff_data_blob.get(), charstrings_index_blob.get());
}

StatusOr<std::optional<uint32_t>> FontHelper::Cff2CharStringsOffset(
    hb_face_t* face) {
  FontData cff_data = FontHelper::TableData(face, FontHelper::kCFF2);
  hb_blob_unique_ptr cff_data_blob = cff_data.blob();
  hb_blob_unique_ptr charstrings_index_blob =
      make_hb_blob(hb_subset_cff2_get_charstrings_index(face));

  return CharStringsOffset(cff_data_blob.get(), charstrings_index_blob.get());
}

StatusOr<uint32_t> FontHelper::GvarSharedTupleCount(const hb_face_t* face) {
  auto gvar = TableData(face, kGvar);
  if (gvar.empty()) {
    return absl::NotFoundError("gvar not in the font.");
  }

  constexpr uint32_t shared_tuple_count_offset = 6;

  if (gvar.size() < 8) {
    return absl::InvalidArgumentError("gvar table is too short.");
  }

  auto count = ReadUInt16(gvar.str().substr(shared_tuple_count_offset));
  if (!count.ok()) {
    return count.status();
  }

  return *count;
}

CodepointSet FontHelper::GidsToUnicodes(hb_face_t* face, const GlyphSet& gids) {
  auto gid_to_unicode = FontHelper::GidToUnicodeMap(face);
  CodepointSet result;
  for (uint32_t gid : gids) {
    auto unicode = gid_to_unicode.find(gid);
    if (unicode != gid_to_unicode.end()) {
      result.union_set(unicode->second);
    }
  }
  return result;
}

flat_hash_map<uint32_t, CodepointSet> FontHelper::GidToUnicodeMap(
    hb_face_t* face) {
  hb_map_t* unicode_to_gid = hb_map_create();
  hb_face_collect_nominal_glyph_mapping(face, unicode_to_gid, nullptr);

  flat_hash_map<uint32_t, CodepointSet> gid_to_unicode;
  int index = -1;
  uint32_t cp = HB_MAP_VALUE_INVALID;
  uint32_t gid = HB_MAP_VALUE_INVALID;
  while (hb_map_next(unicode_to_gid, &index, &cp, &gid)) {
    gid_to_unicode[gid].insert(cp);
  }

  hb_map_destroy(unicode_to_gid);
  return gid_to_unicode;
}

CodepointSet FontHelper::ToCodepointsSet(hb_face_t* face) {
  hb_set_unique_ptr codepoints = make_hb_set();
  hb_face_collect_unicodes(face, codepoints.get());
  hb_face_collect_variation_selectors(face, codepoints.get());
  return CodepointSet(codepoints);
}

absl::flat_hash_set<hb_tag_t> FontHelper::GetTags(hb_face_t* face) {
  absl::flat_hash_set<hb_tag_t> tag_set;
  constexpr uint32_t max_tags = 64;
  hb_tag_t table_tags[max_tags];
  unsigned table_count = max_tags;
  unsigned offset = 0;

  while (((void)hb_face_get_table_tags(face, offset, &table_count, table_tags),
          table_count)) {
    for (unsigned i = 0; i < table_count; i++) {
      hb_tag_t tag = table_tags[i];
      tag_set.insert(tag);
    }
    offset += table_count;
  }
  return tag_set;
}

std::vector<hb_tag_t> FontHelper::GetOrderedTags(hb_face_t* face) {
  std::vector<hb_tag_t> ordered_tags;
  auto tags = GetTags(face);

  std::copy(tags.begin(), tags.end(), std::back_inserter(ordered_tags));
  std::sort(ordered_tags.begin(), ordered_tags.end(),
            CompareTableOffsets(face));

  return ordered_tags;
}

void GetFeatureTagsFrom(hb_face_t* face, hb_tag_t table,
                        btree_set<hb_tag_t>& tag_set) {
  constexpr uint32_t max_tags = 32;
  hb_tag_t feature_tags[max_tags];
  unsigned tag_count = max_tags;
  unsigned offset = 0;

  while (((void)hb_ot_layout_table_get_feature_tags(face, table, offset,
                                                    &tag_count, feature_tags),
          tag_count)) {
    for (unsigned i = 0; i < tag_count; i++) {
      hb_tag_t tag = feature_tags[i];
      tag_set.insert(tag);
    }
    offset += tag_count;
  }
}

btree_set<hb_tag_t> FontHelper::GetFeatureTags(hb_face_t* face) {
  btree_set<hb_tag_t> tag_set;
  GetFeatureTagsFrom(face, kGSUB, tag_set);
  GetFeatureTagsFrom(face, kGPOS, tag_set);
  return tag_set;
}

absl::btree_set<hb_tag_t> FontHelper::GetNonDefaultFeatureTags(
    hb_face_t* face) {
  auto tag_set = GetFeatureTags(face);

  hb_subset_input_t* input = hb_subset_input_create_or_fail();
  hb_set_t* default_tags =
      hb_subset_input_set(input, HB_SUBSET_SETS_LAYOUT_FEATURE_TAG);
  hb_tag_t tag = HB_SET_VALUE_INVALID;
  while (hb_set_next(default_tags, &tag)) {
    tag_set.erase(tag);
  }
  hb_subset_input_destroy(input);

  return tag_set;
}

StatusOr<flat_hash_map<hb_tag_t, AxisRange>> FontHelper::GetDesignSpace(
    hb_face_t* face) {
  constexpr uint32_t max_axes = 32;
  hb_ot_var_axis_info_t axes[max_axes];
  unsigned axes_count = max_axes;
  unsigned offset = 0;

  flat_hash_map<hb_tag_t, AxisRange> result;

  while (((void)hb_ot_var_get_axis_infos(face, offset, &axes_count, axes),
          axes_count)) {
    for (unsigned i = 0; i < axes_count; i++) {
      auto axis = axes[i];
      auto r = AxisRange::Range(axis.min_value, axis.max_value);
      if (!r.ok()) {
        return r.status();
      }
      result[axis.tag] = *r;
    }
    offset += axes_count;
  }

  return result;
}

std::string FontHelper::ToString(hb_tag_t tag) {
  std::string tag_name;
  tag_name.resize(5);  // need 5 for the null terminator
  snprintf(tag_name.data(), 5, "%c%c%c%c", HB_UNTAG(tag));
  tag_name.resize(4);
  return tag_name;
}

hb_tag_t FontHelper::ToTag(const std::string& tag) {
  return HB_TAG(tag[0], tag[1], tag[2], tag[3]);
}

std::vector<std::string> FontHelper::ToStrings(
    const std::vector<hb_tag_t>& tags) {
  std::vector<std::string> result;
  for (hb_tag_t tag : tags) {
    result.push_back(ToString(tag));
  }
  return result;
}

std::vector<std::string> FontHelper::ToStrings(
    const btree_set<hb_tag_t>& input) {
  std::vector<std::string> result;
  for (hb_tag_t tag : input) {
    result.push_back(ToString(tag));
  }
  return result;
}

}  // namespace ift::common