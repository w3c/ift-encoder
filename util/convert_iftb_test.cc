#include "util/convert_iftb.h"

#include <google/protobuf/text_format.h>

#include <string>

#include "absl/container/flat_hash_set.h"
#include "common/font_data.h"
#include "common/sparse_bit_set.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Eq;

using absl::flat_hash_set;
using common::FontData;
using common::hb_blob_unique_ptr;
using common::hb_face_unique_ptr;
using common::make_hb_blob;
using common::SparseBitSet;
using google::protobuf::TextFormat;

namespace util {

class ConvertIftbTest : public ::testing::Test {
 protected:
  ConvertIftbTest() : face(common::make_hb_face(nullptr)) {
    hb_blob_unique_ptr blob =
        common::make_hb_blob(hb_blob_create_from_file_or_fail(
            "util/testdata/convert-iftb-sample.txt"));
    assert(blob.get());

    FontData blob_data(blob.get());
    sample_input = blob_data.string();

    blob = common::make_hb_blob(hb_blob_create_from_file_or_fail(
        "util/testdata/Roboto-Regular.Awesome.ttf"));
    assert(blob.get());
    blob_data.set(blob.get());
    face = blob_data.face();
  }

  std::string sample_input;
  hb_face_unique_ptr face;
};

TEST_F(ConvertIftbTest, BasicConversion) {
  auto config = convert_iftb(sample_input, face.get());
  ASSERT_TRUE(config.ok()) << config.status();

  std::string expected_config =
      "codepoint_sets {\n"
      "  key: 0\n"
      "  value {\n"
      "    values: 115\n"  // s
      "  }\n"
      "}\n"
      "codepoint_sets {\n"
      "  key: 1\n"
      "  value {\n"
      "    values: 65\n"   // A
      "    values: 101\n"  // e
      "    values: 109\n"  // m
      "  }\n"
      "}\n"
      "codepoint_sets {\n"
      "  key: 2\n"
      "  value {\n"
      "    values: 111\n"  // o
      "    values: 119\n"  // w
      "  }\n"
      "}\n"
      "glyph_patches {\n"
      "  key: 0\n"
      "  value {\n"
      "    values: 0\n"
      "    values: 5\n"
      "  }\n"
      "}\n"
      "glyph_patches {\n"
      "  key: 1\n"
      "  value {\n"
      "    values: 1\n"
      "    values: 2\n"
      "    values: 3\n"
      "  }\n"
      "}\n"
      "glyph_patches {\n"
      "  key: 2\n"
      "  value {\n"
      "    values: 4\n"
      "    values: 6\n"
      "  }\n"
      "}\n"
      "glyph_patch_conditions {\n"
      "  required_codepoint_sets {\n"
      "    values: 1\n"
      "  }\n"
      "  activated_patch: 1\n"
      "}\n"
      "glyph_patch_conditions {\n"
      "  required_codepoint_sets {\n"
      "    values: 2\n"
      "  }\n"
      "  activated_patch: 2\n"
      "}\n"
      "initial_codepoint_sets {\n"
      "  values: 0\n"
      "}\n"
      "non_glyph_codepoint_set_groups {\n"
      "  values: 1\n"
      "  values: 2\n"
      "}\n";

  std::string config_string;
  TextFormat::PrintToString(*config, &config_string);

  ASSERT_EQ(config_string, expected_config);
}

}  // namespace util
