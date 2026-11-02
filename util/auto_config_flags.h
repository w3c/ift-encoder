#ifndef UTIL_AUTO_CONFIG_FLAGS_H_
#define UTIL_AUTO_CONFIG_FLAGS_H_

#include <optional>
#include <string>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/flags/declare.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "hb.h"
#include "ift/common/data_file_resolver.h"
#include "ift/common/int_set.h"
#include "ift/config/auto_segmenter_config.h"
#include "ift/config/segmenter_config.pb.h"

ABSL_DECLARE_FLAG(int, auto_config_quality);
ABSL_DECLARE_FLAG(std::string, auto_config_primary_script);
ABSL_DECLARE_FLAG(bool, auto_config_unicode_range);
ABSL_DECLARE_FLAG(std::vector<std::string>,
                  auto_config_additional_default_features);
ABSL_DECLARE_FLAG(std::vector<std::string>, auto_config_default_codepoints);

namespace ift::util {

absl::StatusOr<ift::common::CodepointSet> ParseCodepointsFlag(
    absl::Span<const std::string> items);

absl::StatusOr<absl::btree_set<hb_tag_t>> ParseFeatureTagsFlag(
    absl::Span<const std::string> items);

absl::StatusOr<ift::config::SegmenterConfig>
GenerateAutoSegmenterConfigFromFlags(
    hb_face_t* font, const ift::common::DataFileResolver& resolver,
    std::optional<ift::config::TargetMode> target_mode_override = std::nullopt);

}  // namespace ift::util

#endif  // UTIL_AUTO_CONFIG_FLAGS_H_
