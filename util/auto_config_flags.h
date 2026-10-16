#ifndef UTIL_AUTO_CONFIG_FLAGS_H_
#define UTIL_AUTO_CONFIG_FLAGS_H_

#include <string>

#include "absl/flags/declare.h"

ABSL_DECLARE_FLAG(int, auto_config_quality);
ABSL_DECLARE_FLAG(std::string, auto_config_primary_script);
ABSL_DECLARE_FLAG(bool, auto_config_unicode_range);

#endif  // UTIL_AUTO_CONFIG_FLAGS_H_
