#include "patch_subset/cbor/client_state.h"

#include "common/status.h"
#include "patch_subset/cbor/cbor_utils.h"
#include "patch_subset/cbor/integer_list.h"

namespace patch_subset::cbor {

using std::string;
using std::vector;

ClientState::ClientState()
    : _font_id(std::nullopt),
      _font_data(std::nullopt),
      _original_font_checksum(std::nullopt),
      _codepoint_remapping(std::nullopt),
      _codepoint_remapping_checksum(std::nullopt) {}

ClientState::ClientState(const string& font_id, const string& font_data,
                         uint64_t original_font_checksum,
                         const vector<int32_t>& codepoint_remapping,
                         uint64_t codepoint_remapping_checksum)
    : _font_id(font_id),
      _font_data(font_data),
      _original_font_checksum(original_font_checksum),
      _codepoint_remapping(codepoint_remapping),
      _codepoint_remapping_checksum(codepoint_remapping_checksum) {}

ClientState::ClientState(ClientState&& other) noexcept
    : _font_id(std::move(other._font_id)),
      _font_data(std::move(other._font_data)),
      _original_font_checksum(other._original_font_checksum),
      _codepoint_remapping(std::move(other._codepoint_remapping)),
      _codepoint_remapping_checksum(other._codepoint_remapping_checksum) {}

StatusCode ClientState::Decode(const cbor_item_t& cbor_map, ClientState& out) {
  ClientState result;
  if (!cbor_isa_map(&cbor_map) || cbor_map_is_indefinite(&cbor_map)) {
    return StatusCode::kInvalidArgument;
  }
  StatusCode sc =
      CborUtils::GetStringField(cbor_map, kFontIdFieldNumber, result._font_id);
  if (sc != StatusCode::kOk) {
    return StatusCode::kInvalidArgument;
  }
  sc = CborUtils::GetBytesField(cbor_map, kFontDataFieldNumber,
                                result._font_data);
  if (sc != StatusCode::kOk) {
    return StatusCode::kInvalidArgument;
  }
  sc = CborUtils::GetUInt64Field(cbor_map, kOriginalFontChecksumFieldNumber,
                                 result._original_font_checksum);
  if (sc != StatusCode::kOk) {
    return StatusCode::kInvalidArgument;
  }
  sc = IntegerList::GetIntegerListField(
      cbor_map, kCodepointRemappingFieldNumber, result._codepoint_remapping);
  if (sc != StatusCode::kOk) {
    return StatusCode::kInvalidArgument;
  }
  sc = CborUtils::GetUInt64Field(cbor_map,
                                 kCodepointRemappingChecksumFieldNumber,
                                 result._codepoint_remapping_checksum);
  if (sc != StatusCode::kOk) {
    return StatusCode::kInvalidArgument;
  }
  out = std::move(result);
  return StatusCode::kOk;
}

StatusCode ClientState::Encode(cbor_item_unique_ptr& out) const {
  int map_size = (_font_id.has_value() ? 1 : 0) +
                 (_font_data.has_value() ? 1 : 0) +
                 (_original_font_checksum.has_value() ? 1 : 0) +
                 (_codepoint_remapping.has_value() ? 1 : 0) +
                 (_codepoint_remapping_checksum.has_value() ? 1 : 0);
  cbor_item_unique_ptr map = make_cbor_map(map_size);
  StatusCode sc = CborUtils::SetStringField(*map, kFontIdFieldNumber, _font_id);
  if (sc != StatusCode::kOk) {
    return sc;
  }
  sc = CborUtils::SetBytesField(*map, kFontDataFieldNumber, _font_data);
  if (sc != StatusCode::kOk) {
    return sc;
  }
  sc = CborUtils::SetUInt64Field(*map, kOriginalFontChecksumFieldNumber,
                                 _original_font_checksum);
  if (sc != StatusCode::kOk) {
    return sc;
  }
  sc = IntegerList::SetIntegerListField(*map, kCodepointRemappingFieldNumber,
                                        _codepoint_remapping);
  if (sc != StatusCode::kOk) {
    return sc;
  }
  sc = CborUtils::SetUInt64Field(*map, kCodepointRemappingChecksumFieldNumber,
                                 _codepoint_remapping_checksum);
  if (sc != StatusCode::kOk) {
    return sc;
  }
  out.swap(map);
  return StatusCode::kOk;
}

StatusCode ClientState::ParseFromString(const std::string& buffer,
                                        ClientState& out) {
  cbor_item_unique_ptr item = empty_cbor_ptr();
  StatusCode sc = CborUtils::DeserializeFromBytes(buffer, item);
  if (sc != StatusCode::kOk) {
    return sc;
  }
  sc = Decode(*item, out);
  if (sc != StatusCode::kOk) {
    return sc;
  }
  return StatusCode::kOk;
}

StatusCode ClientState::SerializeToString(std::string& out) {
  cbor_item_unique_ptr item = empty_cbor_ptr();
  StatusCode sc = Encode(item);
  if (sc != StatusCode::kOk) {
    return sc;
  }
  unsigned char* buffer;
  size_t buffer_size;
  size_t written = cbor_serialize_alloc(item.get(), &buffer, &buffer_size);
  if (written == 0) {
    return StatusCode::kInternal;
  }
  out.assign(std::string((char*) buffer, written));
  free(buffer);
  return StatusCode::kOk;
}

ClientState& ClientState::SetFontId(const string& font_id) {
  _font_id.emplace(font_id);
  return *this;
}
ClientState& ClientState::ResetFontId() {
  _font_id.reset();
  return *this;
}
bool ClientState::HasFontId() const { return _font_id.has_value(); }
const string kEmptyString;
const string& ClientState::FontId() const {
  if (_font_id.has_value()) {
    return _font_id.value();
  } else {
    return kEmptyString;
  }
}

ClientState& ClientState::SetFontData(const string& font_data) {
  _font_data.emplace(font_data);
  return *this;
}
ClientState& ClientState::ResetFontData() {
  _font_data.reset();
  return *this;
}
bool ClientState::HasFontData() const { return _font_data.has_value(); }
const string& ClientState::FontData() const {
  if (_font_data.has_value()) {
    return _font_data.value();
  } else {
    return kEmptyString;
  }
}

ClientState& ClientState::SetOriginalFontChecksum(uint64_t checksum) {
  _original_font_checksum.emplace(checksum);
  return *this;
}
ClientState& ClientState::ResetOriginalFontChecksum() {
  _original_font_checksum.reset();
  return *this;
}
bool ClientState::HasOriginalFontChecksum() const {
  return _original_font_checksum.has_value();
}
uint64_t ClientState::OriginalFontChecksum() const {
  return _original_font_checksum.has_value() ? _original_font_checksum.value()
                                             : 0;
}

ClientState& ClientState::SetCodepointRemapping(
    const vector<int32_t>& codepoint_remapping) {
  _codepoint_remapping.emplace(codepoint_remapping);
  return *this;
}
ClientState& ClientState::ResetCodepointRemapping() {
  _codepoint_remapping.reset();
  return *this;
}
bool ClientState::HasCodepointRemapping() const {
  return _codepoint_remapping.has_value();
}
static const vector<int32_t> kEmptyRemappings;
const vector<int32_t>& ClientState::CodepointRemapping() const {
  if (_codepoint_remapping.has_value()) {
    return _codepoint_remapping.value();
  } else {
    return kEmptyRemappings;
  }
}

ClientState& ClientState::SetCodepointRemappingChecksum(uint64_t checksum) {
  _codepoint_remapping_checksum.emplace(checksum);
  return *this;
}
ClientState& ClientState::ResetCodepointRemappingChecksum() {
  _codepoint_remapping_checksum.reset();
  return *this;
}
bool ClientState::HasCodepointRemappingChecksum() const {
  return _codepoint_remapping_checksum.has_value();
}
uint64_t ClientState::CodepointRemappingChecksum() const {
  return _codepoint_remapping_checksum.has_value()
             ? _codepoint_remapping_checksum.value()
             : 0;
}

ClientState& ClientState::operator=(ClientState&& other) noexcept {
  _font_id = std::move(other._font_id);
  _font_data = std::move(other._font_data);
  _original_font_checksum = other._original_font_checksum;
  _codepoint_remapping = std::move(other._codepoint_remapping);
  _codepoint_remapping_checksum = other._codepoint_remapping_checksum;
  return *this;
}

bool ClientState::operator==(const ClientState& other) const {
  return _font_id == other._font_id && _font_data == other._font_data &&
         _original_font_checksum == other._original_font_checksum &&
         _codepoint_remapping == other._codepoint_remapping &&
         _codepoint_remapping_checksum == other._codepoint_remapping_checksum;
}
bool ClientState::operator!=(const ClientState& other) const {
  return !(*this == other);
}

}  // namespace patch_subset::cbor
