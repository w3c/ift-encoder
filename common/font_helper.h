#ifndef COMMON_FONT_HELPER_H_
#define COMMON_FONT_HELPER_H_

#include <cstdint>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/axis_range.h"
#include "common/font_data.h"
#include "hb.h"

namespace common {

struct CompareTableOffsets {
  hb_face_t* face;
  CompareTableOffsets(hb_face_t* f) { face = f; }

  uint32_t table_offset(hb_tag_t tag) const {
    hb_blob_t* font = hb_face_reference_blob(face);
    hb_blob_t* table = hb_face_reference_table(face, tag);

    const uint8_t* font_ptr = (const uint8_t*)hb_blob_get_data(font, nullptr);
    const uint8_t* table_ptr = (const uint8_t*)hb_blob_get_data(table, nullptr);
    uint32_t offset = table_ptr - font_ptr;

    hb_blob_destroy(font);
    hb_blob_destroy(table);

    return offset;
  }

  bool operator()(hb_tag_t a, hb_tag_t b) const {
    return table_offset(a) < table_offset(b);
  }
};

class FontHelper {
 public:
  constexpr static hb_tag_t kIFT = HB_TAG('I', 'F', 'T', ' ');
  constexpr static hb_tag_t kIFTB = HB_TAG('I', 'F', 'T', 'B');
  constexpr static hb_tag_t kLoca = HB_TAG('l', 'o', 'c', 'a');
  constexpr static hb_tag_t kGlyf = HB_TAG('g', 'l', 'y', 'f');
  constexpr static hb_tag_t kHead = HB_TAG('h', 'e', 'a', 'd');
  constexpr static hb_tag_t kGvar = HB_TAG('g', 'v', 'a', 'r');
  constexpr static hb_tag_t kCFF = HB_TAG('C', 'F', 'F', ' ');
  constexpr static hb_tag_t kCFF2 = HB_TAG('C', 'F', 'F', '2');
  constexpr static hb_tag_t kGSUB = HB_TAG('G', 'S', 'U', 'B');
  constexpr static hb_tag_t kGPOS = HB_TAG('G', 'P', 'O', 'S');

  static void WriteUInt32(uint32_t value, std::string& out) {
    WriteUInt<32>(value, out);
  }

  static void WriteUInt16(uint32_t value, std::string& out) {
    WriteUInt<16>(value, out);
  }

  static void WriteUInt8(uint32_t value, std::string& out) {
    WriteUInt<8>(value, out);
  }

  static absl::StatusOr<uint32_t> ReadUInt32(absl::string_view value) {
    return ReadUInt<32, uint32_t>(value);
  }

  static absl::StatusOr<uint16_t> ReadUInt16(absl::string_view value) {
    return ReadUInt<16, uint16_t>(value);
  }

  static absl::StatusOr<uint8_t> ReadUInt8(absl::string_view value) {
    return ReadUInt<8, uint8_t>(value);
  }

  static absl::StatusOr<absl::string_view> GlyfData(const hb_face_t* face,
                                                    uint32_t gid);

  static absl::StatusOr<absl::string_view> GvarData(const hb_face_t* face,
                                                    uint32_t gid);

  static absl::StatusOr<uint32_t> GvarSharedTupleCount(const hb_face_t* face);
  static absl::StatusOr<uint64_t> GvarSharedTupleChecksum(hb_face_t* face);

  static absl::StatusOr<absl::string_view> Loca(const hb_face_t* face) {
    auto result = FontHelper::TableData(face, kLoca).str();
    if (result.empty()) {
      return absl::NotFoundError("loca table was not found.");
    }
    return result;
  }

  static FontData TableData(const hb_face_t* face, hb_tag_t tag) {
    hb_blob_t* blob = hb_face_reference_table(face, tag);
    FontData result(blob);
    hb_blob_destroy(blob);
    return result;
  }

  static FontData BuildFont(
      const absl::flat_hash_map<hb_tag_t, std::string> tables) {
    hb_face_t* builder = hb_face_builder_create();
    for (const auto& e : tables) {
      hb_blob_t* blob =
          hb_blob_create(e.second.data(), e.second.size(),
                         HB_MEMORY_MODE_READONLY, nullptr, nullptr);
      hb_face_builder_add_table(builder, e.first, blob);
      hb_blob_destroy(blob);
    }

    hb_blob_t* blob = hb_face_reference_blob(builder);
    FontData result(blob);
    hb_blob_destroy(blob);
    hb_face_destroy(builder);
    return result;
  }

  static absl::flat_hash_map<uint32_t, uint32_t> GidToUnicodeMap(
      hb_face_t* face);

  static absl::flat_hash_set<hb_tag_t> GetTags(hb_face_t* face);
  static std::vector<hb_tag_t> GetOrderedTags(hb_face_t* face);

  static absl::btree_set<hb_tag_t> GetFeatureTags(hb_face_t* face);
  static absl::btree_set<hb_tag_t> GetNonDefaultFeatureTags(hb_face_t* face);

  static absl::StatusOr<absl::flat_hash_map<hb_tag_t, AxisRange>>
  GetDesignSpace(hb_face_t* face);

  static void ApplyIftbTableOrdering(hb_face_t* face);

  static std::vector<std::string> ToStrings(const std::vector<hb_tag_t>& input);
  static std::vector<std::string> ToStrings(
      const absl::btree_set<hb_tag_t>& input);
  static std::string ToString(hb_tag_t tag);
  static hb_tag_t ToTag(const std::string& tag);

 private:
  template <int num_bits, typename uint_type_t>
  static void WriteUInt(uint_type_t value, std::string& out) {
    constexpr int num_bytes = num_bits / 8;
    int shift = num_bits - 8;
    for (int i = 0; i < num_bytes; i++) {
      out.push_back(
          (uint8_t)((uint_type_t)(value >> shift) & (uint_type_t)0x000000FFu));
      shift -= 8;
    }
  }

  template <unsigned num_bits, typename uint_type_t>
  static absl::StatusOr<uint_type_t> ReadUInt(absl::string_view value) {
    unsigned num_bytes = num_bits / 8;
    if (value.size() < num_bytes) {
      return absl::InvalidArgumentError(
          absl::StrCat("Need at least ", num_bytes));
    }

    const uint8_t* bytes = (const uint8_t*)value.data();
    unsigned shift = num_bits - 8;
    uint_type_t result = 0;
    for (unsigned i = 0; i < num_bytes; i++) {
      result += (((uint_type_t)bytes[i]) << shift);
      shift -= 8;
    }

    return result;
  }
};

}  // namespace common

#endif  // COMMON_FONT_HELPER_H_