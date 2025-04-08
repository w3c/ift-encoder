#include "ift/table_keyed_diff.h"

#include <string>

#include "absl/container/flat_hash_set.h"
#include "common/font_data.h"
#include "common/font_helper.h"
#include "common/font_helper_macros.h"
#include "hb.h"

using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::Status;
using common::FontData;
using common::FontHelper;

namespace ift {

Status TableKeyedDiff::Diff(const FontData& font_base,
                            const FontData& font_derived,
                            FontData* patch /* OUT */) const {
  hb_face_t* face_base = font_base.reference_face();
  hb_face_t* face_derived = font_derived.reference_face();

  auto base_tags = FontHelper::GetTags(face_base);
  auto derived_tags = FontHelper::GetTags(face_derived);
  auto diff_tags = TagsToDiff(base_tags, derived_tags);

  flat_hash_map<std::string, std::pair<uint32_t, FontData>> patches;
  flat_hash_set<hb_tag_t> new_tables;
  flat_hash_set<hb_tag_t> unchanged_tables;

  for (std::string tag : diff_tags) {
    hb_tag_t t = HB_TAG(tag[0], tag[1], tag[2], tag[3]);
    bool in_base = base_tags.contains(t);
    bool in_derived = derived_tags.contains(t);

    if (in_base && !in_derived) {
      continue;
    }

    FontData base_table;
    if (!replaced_tags_.contains(tag)) {
      if (in_base) {
        base_table = FontHelper::TableData(face_base, t);
      } else {
        new_tables.insert(t);
      }
    }

    FontData derived_table = FontHelper::TableData(face_derived, t);
    if (base_table == derived_table) {
      // If table is unchanged then no diff is needed.
      unchanged_tables.insert(t);
      continue;
    }

    FontData table_patch;
    auto sc = binary_diff_.Diff(base_table, derived_table, &table_patch);
    if (!sc.ok()) {
      hb_face_destroy(face_base);
      hb_face_destroy(face_derived);
      return sc;
    }

    patches[tag] = std::pair(derived_table.size(), std::move(table_patch));
  }

  hb_face_destroy(face_base);
  hb_face_destroy(face_derived);

  for (hb_tag_t t : unchanged_tables) {
    std::string tag = FontHelper::ToString(t);
    auto it = diff_tags.find(tag);
    if (it != diff_tags.end()) {
      diff_tags.erase(it);
    }
  }

  // Serialize to the binary format
  std::string data;
  FontHelper::WriteUInt32(HB_TAG('i', 'f', 't', 'k'), data);
  FontHelper::WriteUInt32(0, data);  // reserved

  this->base_compat_id_.WriteTo(data);

  // Write offsets to table patches.
  WRITE_UINT16(diff_tags.size(), data,
               "Exceeded max number of tables (0xFFFF).");

  // Skip past all of the offsets (there's patchesCount + 1 of them)
  uint32_t current_offset = data.size() + (diff_tags.size() + 1) * 4;

  for (std::string tag : diff_tags) {
    FontHelper::WriteUInt32(current_offset, data);
    uint32_t min_size = 9;
    current_offset += min_size;

    auto it = patches.find(tag);
    if (it != patches.end()) {
      current_offset += it->second.second.size();
    }
  }
  FontHelper::WriteUInt32(current_offset, data);

  // Write out table patches
  for (std::string tag : diff_tags) {
    hb_tag_t t = HB_TAG(tag[0], tag[1], tag[2], tag[3]);

    FontHelper::WriteUInt32(t, data);

    auto it = patches.find(tag);
    if (it == patches.end()) {
      // no data signals removal.
      WRITE_UINT8(0b00000010, data, "");
      FontHelper::WriteUInt32(0, data);
      continue;
    }

    FontData& patch_data = it->second.second;

    if (replaced_tags_.contains(tag) || new_tables.contains(t)) {
      WRITE_UINT8(0b00000001, data, "");
    } else {
      WRITE_UINT8(0b00000000, data, "");
    }

    // max uncompressed length
    FontHelper::WriteUInt32(it->second.first, data);
    data += patch_data.string();
  }

  patch->copy(data);

  return absl::OkStatus();
}

void TableKeyedDiff::AddAllMatching(const flat_hash_set<hb_tag_t>& tags,
                                    btree_set<std::string>& result) const {
  for (const uint32_t& t : tags) {
    std::string tag = FontHelper::ToString(t);
    if (!excluded_tags_.contains(tag)) {
      result.insert(tag);
    }
  }
}

btree_set<std::string> TableKeyedDiff::TagsToDiff(
    const absl::flat_hash_set<hb_tag_t>& before,
    const absl::flat_hash_set<hb_tag_t>& after) const {
  btree_set<std::string> result;
  AddAllMatching(before, result);
  AddAllMatching(after, result);
  return result;
}

}  // namespace ift
