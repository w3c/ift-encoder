#include "util/auto_config_flags.h"

#include <string>

#include "absl/flags/flag.h"

ABSL_FLAG(
    int, auto_config_quality, 0,
    "The quality level to use when generating a segmenter config. A value of 0 "
    "means auto pick. Valid values are 1-8.");

ABSL_FLAG(
    std::string, auto_config_primary_script, "Script_latin",
    "When auto_config is enabled this sets the primary script or "
    "language frequency data file to use. "
    "The primary script is eligible to have codepoints moved to the init font. "
    "For CJK primary script can be used to specialize against a specific "
    "language/script.");
