#include "ift/config/config_compiler.h"

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"
#include "ift/config/segmentation_plan.pb.h"
#include "ift/encoder/compiler.h"

using ift::encoder::Compiler;

namespace ift::config {

TEST(ConfigCompilerTest, ConfigureOverrideUrlTemplatePrefix) {
  Compiler compiler;
  SegmentationPlan plan;
  std::string prefix_str = "https://example.com/";
  plan.mutable_advanced_settings()->set_override_url_template_prefix(
      prefix_str);

  absl::Status status = ConfigCompiler::Configure(plan, compiler);
  ASSERT_TRUE(status.ok()) << status;

  std::vector<uint8_t> expected(prefix_str.begin(), prefix_str.end());
  EXPECT_EQ(compiler.override_url_template_prefix(), expected);
}

}  // namespace ift::config
