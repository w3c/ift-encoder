#ifndef IFT_CONFIG_CONFIG_COMPILER_H_
#define IFT_CONFIG_CONFIG_COMPILER_H_

#include "absl/status/status.h"
#include "ift/config/segmentation_plan.pb.h"
#include "ift/encoder/compiler.h"
#include "ift/encoder/unicode_range_compiler.h"

namespace ift::config {

class ConfigCompiler {
 public:
  // Configures the compiler based on the provided segmentation plan.
  static absl::Status Configure(const SegmentationPlan& plan,
                                encoder::Compiler& compiler);
  static absl::Status Configure(const SegmentationPlan& plan,
                                encoder::UnicodeRangeCompiler& compiler);

 private:
  ConfigCompiler() = delete;
};

}  // namespace ift::config

#endif  // IFT_CONFIG_CONFIG_COMPILER_H_
