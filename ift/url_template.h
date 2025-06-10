#ifndef IFT_URL_TEMPLATE_H_
#define IFT_URL_TEMPLATE_H_

#include <cstdint>

#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace ift {

/*
 * Implementation of IFT URL template substitution.
 */
class URLTemplate {
 public:
  static absl::StatusOr<std::string> PatchToUrl(
      absl::Span<const uint8_t> url_template, uint32_t patch_idx);
};

}  // namespace ift

#endif  // IFT_URL_TEMPLATE_H_
