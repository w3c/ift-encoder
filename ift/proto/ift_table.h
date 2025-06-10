#ifndef IFT_PROTO_IFT_TABLE_H_
#define IFT_PROTO_IFT_TABLE_H_

#include <cstdint>

#include "absl/status/statusor.h"
#include "common/compat_id.h"
#include "common/font_data.h"
#include "hb.h"
#include "ift/proto/patch_map.h"

namespace ift::proto {

/*
 * Abstract representation of a IFT table. Used to load, construct, and/or
 * modify IFT tables in fonts.
 */
class IFTTable {
 public:
  friend void PrintTo(const IFTTable& table, std::ostream* os);

  // TODO(garretrieger): add a separate extension id as well (like w/ URL
  // templates).
  common::CompatId GetId() const;

  const PatchMap& GetPatchMap() const { return patch_map_; }
  PatchMap& GetPatchMap() { return patch_map_; }

  const absl::Span<const uint8_t> GetUrlTemplate() const {
    return url_template_;
  }

  void SetUrlTemplate(absl::Span<const uint8_t> value) {
    url_template_.insert(url_template_.begin(), value.begin(), value.end());
  }

  void SetId(common::CompatId compat_id) { id_ = compat_id; }

  bool operator==(const IFTTable& other) const {
    return url_template_ == other.url_template_ && id_ == other.id_ &&
           patch_map_ == other.patch_map_;
  }

  /*
   * Adds an encoded 'IFT ' table built from this IFT table to the font pointed
   * to by face. By default this will maintain the physical orderng of tables
   * already present in the font. If extension entries are present then an
   * extension table (IFTX) will also be added.
   */
  static absl::StatusOr<common::FontData> AddToFont(
      hb_face_t* face, const IFTTable& main,
      std::optional<const IFTTable*> extension);

 private:
  /*
   * Adds an the provided 'IFT ' (and optionally an 'IFTX') tables to by face.
   */
  static absl::StatusOr<common::FontData> AddToFont(
      hb_face_t* face, absl::string_view ift_table,
      std::optional<absl::string_view> iftx_table);

  /*
   * Converts this abstract representation to the a serialized format.
   * Either format 1 or 2:
   * https://w3c.github.io/IFT/Overview.html#patch-map-table
   */
  absl::StatusOr<std::string> Serialize() const;

  std::vector<uint8_t> url_template_;
  common::CompatId id_;
  PatchMap patch_map_;
};

}  // namespace ift::proto

#endif  // IFT_PROTO_IFT_TABLE_H_
