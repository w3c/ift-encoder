#ifndef UTIL_AUTO_SEGMENTER_CONFIG_H_
#define UTIL_AUTO_SEGMENTER_CONFIG_H_

#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "hb.h"
#include "util/segmenter_config.pb.h"

namespace util {

class AutoSegmenterConfig {
 public:
  // Analyzes the provided font face and generates an appropriate segmenter
  // configuration.
  //
  // primary_script: an optional name of a script or language frequency data
  //                 file (e.g., "Script_cyrillic", "Language_fr").
  //                 Defaults to "Script_latin" if not provided.
  static absl::StatusOr<SegmenterConfig> GenerateConfig(
      hb_face_t* face,
      std::optional<std::string> primary_script = std::nullopt,
      std::optional<int> quality_level = std::nullopt);

  // Returns the base script for a given language.
  // For example, "Language_fr" -> "Script_latin".
  static absl::StatusOr<std::string> GetBaseScriptForLanguage(
      absl::string_view language);

 private:
  AutoSegmenterConfig() = delete;
};

}  // namespace util

#endif  // UTIL_AUTO_SEGMENTER_CONFIG_H_
