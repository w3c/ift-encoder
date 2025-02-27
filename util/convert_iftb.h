#ifndef UTIL_CONVERT_IFTB_H_
#define UTIL_CONVERT_IFTB_H_

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "hb.h"
#include "util/encoder_config.pb.h"

namespace util {

absl::StatusOr<EncoderConfig> convert_iftb(absl::string_view iftb_dump,
                                           hb_face_t* face);

}

#endif  // UTIL_CONVERT_IFTB_H_
