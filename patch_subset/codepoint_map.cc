#include "patch_subset/codepoint_map.h"

#include "common/logging.h"
#include "hb.h"
#include "patch_subset/hb_set_unique_ptr.h"

namespace patch_subset {

void CodepointMap::Clear() {
  encode_map.clear();
  decode_map.clear();
}

void CodepointMap::AddMapping(hb_codepoint_t from, hb_codepoint_t to) {
  encode_map[from] = to;
  decode_map[to] = from;
}

void CodepointMap::FromProto(const CompressedListProto& proto) {
  Clear();

  int index = 0;
  int last_cp = 0;
  for (int delta : proto.deltas()) {
    int cp = last_cp + delta;
    AddMapping(cp, index++);
    last_cp = cp;
  }
}

void CodepointMap::FromVector(const vector<int32_t>& ints) {
  Clear();
  int index = 0;
  for (int32_t cp : ints) {
    AddMapping(cp, index++);
  }
}

StatusCode CodepointMap::ToProto(CompressedListProto* proto) const {
  // A CodepopointRemappingProto encodes a mapping as a list such that:
  //
  // codepoint_ordering[encoded_value] = decoded_value
  //
  // Integers in proto bufs are variable length encoded. So to reduce the
  // number of bytes required to serialize this list, it is written
  // as a series of deltas between the value at the current index and the
  // value at the previous index of the list.

  int last_cp = 0;
  for (unsigned int i = 0; i < encode_map.size(); i++) {
    hb_codepoint_t cp_for_index = i;
    StatusCode result = Decode(&cp_for_index);
    if (result != StatusCode::kOk) {
      return result;
    }

    int delta = cp_for_index - last_cp;
    last_cp = cp_for_index;

    proto->add_deltas(delta);
  }

  return StatusCode::kOk;
}

StatusCode CodepointMap::ToVector(vector<int32_t>* ints) const {
  ints->resize(encode_map.size());
  ints->clear();
  for (unsigned int i = 0; i < encode_map.size(); i++) {
    hb_codepoint_t cp_for_index = i;
    StatusCode result = Decode(&cp_for_index);
    if (result != StatusCode::kOk) {
      return result;
    }
    ints->push_back(cp_for_index);
  }
  return StatusCode::kOk;
}

StatusCode ApplyMappingTo(
    const std::unordered_map<hb_codepoint_t, hb_codepoint_t>& mapping,
    hb_codepoint_t* cp) {
  auto new_cp = mapping.find(*cp);
  if (new_cp == mapping.end()) {
    LOG(WARNING)
        << "Encountered codepoint that is unspecified in the remapping: " << cp;
    return StatusCode::kInvalidArgument;
  }

  *cp = new_cp->second;
  return StatusCode::kOk;
}

StatusCode ApplyMappingTo(
    const std::unordered_map<hb_codepoint_t, hb_codepoint_t>& mapping,
    hb_set_t* codepoints) {
  hb_set_unique_ptr new_codepoints = make_hb_set();

  for (hb_codepoint_t cp = HB_SET_VALUE_INVALID;
       hb_set_next(codepoints, &cp);) {
    hb_codepoint_t new_cp = cp;
    StatusCode result = ApplyMappingTo(mapping, &new_cp);
    if (result != StatusCode::kOk) {
      return result;
    }
    hb_set_add(new_codepoints.get(), new_cp);
  }

  hb_set_clear(codepoints);
  hb_set_union(codepoints, new_codepoints.get());

  return StatusCode::kOk;
}

StatusCode CodepointMap::Encode(hb_set_t* codepoints) const {
  return ApplyMappingTo(encode_map, codepoints);
}

StatusCode CodepointMap::Encode(hb_codepoint_t* cp) const {
  return ApplyMappingTo(encode_map, cp);
}

StatusCode CodepointMap::Decode(hb_set_t* codepoints) const {
  return ApplyMappingTo(decode_map, codepoints);
}

StatusCode CodepointMap::Decode(hb_codepoint_t* cp) const {
  return ApplyMappingTo(decode_map, cp);
}

void CodepointMap::IntersectWithMappedCodepoints(hb_set_t* codepoints) const {
  hb_set_unique_ptr mapped_codepoints = make_hb_set();
  for (auto entry : encode_map) {
    hb_set_add(mapped_codepoints.get(), entry.first);
  }

  hb_set_intersect(codepoints, mapped_codepoints.get());
}

}  // namespace patch_subset
