#include "util/auto_config_flags.h"

#include <string>

#include "absl/flags/flag.h"

ABSL_FLAG(
    int, auto_config_quality, 0,
    "The quality level to use when generating a segmenter config. A value of 0 "
    "means auto pick. Valid values are 1-8.");

ABSL_FLAG(
    std::string, auto_config_primary_script, "",
    "When auto_config is enabled this sets the primary script or "
    "language frequency data file the font is expected to be used with. "
    "This is used to signal the merger to prioritize performance with the "
    "specified script/language. Concretely: initial font merging will be "
    "done using the primary script/language frequency data. Additionally, "
    "when multiple scripts overlap (eg. like with CJK) merging will "
    "prioritize optimizing against the primary script/language instead of "
    "all overlapping scripts equally.");
