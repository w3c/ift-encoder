#include "util/auto_config_flags.h"

#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "hb.h"
#include "ift/common/bazel_data_file_resolver.h"
#include "ift/common/font_helper.h"
#include "ift/common/int_set.h"
#include "ift/config/load_codepoints.h"

namespace ift::util {
namespace {

using ::testing::ElementsAre;
using ::testing::UnorderedElementsAre;

TEST(AutoConfigFlagsTest, ParseCodepointsFlag_Empty) {
  auto result = ParseCodepointsFlag({});
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_TRUE(result->empty());

  result = ParseCodepointsFlag({""});
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_TRUE(result->empty());
}

TEST(AutoConfigFlagsTest, ParseCodepointsFlag_SingleValues) {
  auto result = ParseCodepointsFlag({"0x41", "0X42", "U+0043", "u+0044", "69"});
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_THAT(result->to_vector(), ElementsAre(0x41, 0x42, 0x43, 0x44, 69));
}

TEST(AutoConfigFlagsTest, ParseCodepointsFlag_Ranges) {
  auto result = ParseCodepointsFlag({"0x30-0x32", "U+0100..U+0102", "65-67"});
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_THAT(result->to_vector(),
              ElementsAre(0x30, 0x31, 0x32, 65, 66, 67, 0x100, 0x101, 0x102));
}

TEST(AutoConfigFlagsTest, ParseCodepointsFlag_CommaSeparatedAndWhitespace) {
  auto result = ParseCodepointsFlag(
      {" 0x41 , 0x42 ", " 0x50 - 0x52 , U+60..U+61 "});
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_THAT(result->to_vector(),
              ElementsAre(0x41, 0x42, 0x50, 0x51, 0x52, 0x60, 0x61));
}

TEST(AutoConfigFlagsTest, ParseCodepointsFlag_InvalidFormats) {
  // Invalid hex
  EXPECT_EQ(ParseCodepointsFlag({"0x"}).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ParseCodepointsFlag({"0xZZ"}).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ParseCodepointsFlag({"U+G"}).status().code(),
            absl::StatusCode::kInvalidArgument);

  // Invalid decimal
  EXPECT_EQ(ParseCodepointsFlag({"12a"}).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ParseCodepointsFlag({"-5"}).status().code(),
            absl::StatusCode::kInvalidArgument);

  // Out of range (> 0x10FFFF)
  EXPECT_EQ(ParseCodepointsFlag({"0x110000"}).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ParseCodepointsFlag({"1114112"}).status().code(),
            absl::StatusCode::kInvalidArgument);

  // Inverted range
  EXPECT_EQ(ParseCodepointsFlag({"0x50-0x40"}).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ParseCodepointsFlag({"100..50"}).status().code(),
            absl::StatusCode::kInvalidArgument);

  // Malformed range
  EXPECT_EQ(ParseCodepointsFlag({"0x40-"}).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ParseCodepointsFlag({"-0x40"}).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ParseCodepointsFlag({"0x40.."}).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(AutoConfigFlagsTest, ParseFeatureTagsFlag_Valid) {
  auto result = ParseFeatureTagsFlag({"smcp", "dlig"});
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_THAT(
      *result,
      UnorderedElementsAre(HB_TAG('s', 'm', 'c', 'p'), HB_TAG('d', 'l', 'i', 'g')));

  // Comma separated with whitespace
  result = ParseFeatureTagsFlag({"smcp, dlig", "cv01"});
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_THAT(
      *result,
      UnorderedElementsAre(HB_TAG('s', 'm', 'c', 'p'), HB_TAG('d', 'l', 'i', 'g'),
                           HB_TAG('c', 'v', '0', '1')));
}

TEST(AutoConfigFlagsTest, ParseFeatureTagsFlag_InvalidLength) {
  EXPECT_EQ(ParseFeatureTagsFlag({"a"}).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ParseFeatureTagsFlag({"abc"}).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ParseFeatureTagsFlag({"abcde"}).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(AutoConfigFlagsTest, GenerateAutoSegmenterConfigFromFlags) {
  auto resolver = ift::common::BazelDataFileResolver::CreateForTest();
  ASSERT_TRUE(resolver.ok()) << resolver.status();

  auto font_data =
      ift::config::LoadFile("ift/common/testdata/Roboto-Regular.ttf");
  ASSERT_TRUE(font_data.ok()) << font_data.status();
  auto face = font_data->face();

  absl::SetFlag(&FLAGS_auto_config_quality, 5);
  absl::SetFlag(&FLAGS_auto_config_primary_script, "Script_latin");
  absl::SetFlag(&FLAGS_auto_config_default_codepoints,
                std::vector<std::string>{"0x20", "0x41-0x43"});
  absl::SetFlag(&FLAGS_auto_config_additional_default_features,
                std::vector<std::string>{"smcp"});

  auto config = GenerateAutoSegmenterConfigFromFlags(face.get(), **resolver);
  ASSERT_TRUE(config.ok()) << config.status();

  EXPECT_THAT(config->initial_segment().codepoints().values(),
              UnorderedElementsAre(0x20, 0x41, 0x42, 0x43));
  EXPECT_THAT(config->initial_segment().features().values(),
              UnorderedElementsAre("smcp"));
}

}  // namespace
}  // namespace ift::util
