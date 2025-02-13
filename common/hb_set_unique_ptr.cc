#include "common/hb_set_unique_ptr.h"

#include <cstdarg>
#include <memory>

#include "hb.h"

using absl::flat_hash_set;

namespace common {

hb_set_unique_ptr make_hb_set() {
  return hb_set_unique_ptr(hb_set_create(), &hb_set_destroy);
}

hb_set_unique_ptr make_hb_set(hb_set_t* set) {
  return hb_set_unique_ptr(set, &hb_set_destroy);
}

hb_set_unique_ptr make_hb_set(const absl::flat_hash_set<uint32_t>& int_set) {
  hb_set_unique_ptr out = make_hb_set();
  for (uint32_t v : int_set) {
    hb_set_add(out.get(), v);
  }
  return out;
}

hb_set_unique_ptr make_hb_set(int length, ...) {
  hb_set_unique_ptr result = make_hb_set();
  va_list values;
  va_start(values, length);

  for (int i = 0; i < length; i++) {
    hb_codepoint_t value = va_arg(values, hb_codepoint_t);
    hb_set_add(result.get(), value);
  }
  va_end(values);
  return result;
}

hb_set_unique_ptr make_hb_set_from_ranges(int number_of_ranges, ...) {
  va_list values;
  va_start(values, number_of_ranges);

  hb_set_unique_ptr result = make_hb_set();
  int length = number_of_ranges * 2;
  for (int i = 0; i < length; i += 2) {
    hb_codepoint_t start = va_arg(values, hb_codepoint_t);
    hb_codepoint_t end = va_arg(values, hb_codepoint_t);
    hb_set_add_range(result.get(), start, end);
  }
  va_end(values);
  return result;
}

flat_hash_set<uint32_t> to_hash_set(const hb_set_t* set) {
  flat_hash_set<uint32_t> out;
  hb_codepoint_t v = HB_SET_VALUE_INVALID;
  while (hb_set_next(set, &v)) {
    out.insert(v);
  }
  return out;
}

}  // namespace common
