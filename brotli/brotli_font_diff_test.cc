#include "brotli/brotli_font_diff.h"

#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "hb-subset.h"
#include "patch_subset/brotli_binary_patch.h"

using ::absl::Span;
using ::patch_subset::BrotliBinaryPatch;
using ::patch_subset::FontData;
using ::patch_subset::StatusCode;

namespace brotli {

void dump(const char* name, const char* data, unsigned size) {
  // TODO remove
  FILE* f = fopen(name, "w");
  fwrite(data, size, 1, f);
  fclose(f);
}

class BrotliFontDiffTest : public ::testing::Test {
 protected:
  BrotliFontDiffTest() {}

  ~BrotliFontDiffTest() override {}

  void SetUp() override {
    hb_blob_t* font_data = hb_blob_create_from_file_or_fail(
        "patch_subset/testdata/Roboto-Regular.ttf");
    ASSERT_TRUE(font_data);
    roboto = hb_face_create(font_data, 0);
    hb_blob_destroy(font_data);

    font_data = hb_blob_create_from_file_or_fail(
        "patch_subset/testdata/NotoSansJP-Regular.ttf");
    ASSERT_TRUE(font_data);
    noto_sans_jp = hb_face_create(font_data, 0);
    hb_blob_destroy(font_data);

    input = hb_subset_input_create_or_fail();
  }

  void TearDown() override {
    hb_face_destroy(roboto);
    hb_face_destroy(noto_sans_jp);
    hb_subset_input_destroy(input);
  }

  void Check(const FontData& base, const FontData& patch,
             const FontData& derived) {
    BrotliBinaryPatch patcher;
    FontData patched;
    EXPECT_EQ(StatusCode::kOk, patcher.Patch(base, patch, &patched));

    EXPECT_EQ(derived.str(), patched.str());
  }

  hb_face_t* roboto;
  hb_face_t* noto_sans_jp;
  hb_subset_input_t* input;
};

TEST_F(BrotliFontDiffTest, Diff) {
  hb_set_add_range(hb_subset_input_unicode_set(input), 0x41, 0x5A);
  hb_subset_plan_t* base_plan = hb_subset_plan_create_or_fail(roboto, input);
  hb_face_t* base_face = hb_subset_plan_execute_or_fail(base_plan);
  hb_blob_t* base_blob = hb_face_reference_blob(base_face);
  FontData base = FontData::ToFontData(base_face);
  ASSERT_TRUE(base_plan);

  hb_set_add_range(hb_subset_input_unicode_set(input), 0x61, 0x7A);
  hb_subset_plan_t* derived_plan = hb_subset_plan_create_or_fail(roboto, input);
  hb_face_t* derived_face = hb_subset_plan_execute_or_fail(derived_plan);
  hb_blob_t* derived_blob = hb_face_reference_blob(derived_face);
  FontData derived = FontData::ToFontData(derived_face);
  ASSERT_TRUE(derived_plan);

  BrotliFontDiff differ;
  FontData patch;
  differ.Diff(base_plan, base_blob, derived_plan, derived_blob, &patch);

  Check(base, patch, derived);

  hb_subset_plan_destroy(base_plan);
  hb_subset_plan_destroy(derived_plan);
  hb_blob_destroy(base_blob);
  hb_blob_destroy(derived_blob);
  hb_face_destroy(base_face);
  hb_face_destroy(derived_face);
}

TEST_F(BrotliFontDiffTest, DiffRetainGids) {
  hb_set_add_range(hb_subset_input_unicode_set(input), 0x41, 0x45);
  hb_set_add_range(hb_subset_input_unicode_set(input), 0x57, 0x59);
  hb_subset_input_set_flags(input, HB_SUBSET_FLAGS_RETAIN_GIDS);
  hb_subset_plan_t* base_plan = hb_subset_plan_create_or_fail(roboto, input);
  hb_face_t* base_face = hb_subset_plan_execute_or_fail(base_plan);
  hb_blob_t* base_blob = hb_face_reference_blob(base_face);
  FontData base = FontData::ToFontData(base_face);
  ASSERT_TRUE(base_plan);

  hb_set_add(hb_subset_input_unicode_set(input), 0x47);
  hb_subset_plan_t* derived_plan = hb_subset_plan_create_or_fail(roboto, input);
  hb_face_t* derived_face = hb_subset_plan_execute_or_fail(derived_plan);
  hb_blob_t* derived_blob = hb_face_reference_blob(derived_face);
  FontData derived = FontData::ToFontData(derived_face);
  ASSERT_TRUE(derived_plan);

  BrotliFontDiff differ;
  FontData patch;
  differ.Diff(base_plan, base_blob, derived_plan, derived_blob, &patch);

  Check(base, patch, derived);

  hb_subset_plan_destroy(base_plan);
  hb_subset_plan_destroy(derived_plan);
  hb_face_destroy(base_face);
  hb_face_destroy(derived_face);
  hb_blob_destroy(base_blob);
  hb_blob_destroy(derived_blob);
}

// TODO(garretrieger): diff where base is not a subset of derived.

TEST_F(BrotliFontDiffTest, LongLoca) {
  hb_set_add_range(hb_subset_input_glyph_set(input), 1000, 5000);
  hb_set_add_range(hb_subset_input_glyph_set(input), 8000, 10000);
  hb_subset_plan_t* base_plan =
      hb_subset_plan_create_or_fail(noto_sans_jp, input);
  hb_face_t* base_face = hb_subset_plan_execute_or_fail(base_plan);
  hb_blob_t* base_blob = hb_face_reference_blob(base_face);
  FontData base = FontData::ToFontData(base_face);
  ASSERT_TRUE(base_plan);

  hb_set_add_range(hb_subset_input_glyph_set(input), 500, 750);
  hb_set_add_range(hb_subset_input_glyph_set(input), 11000, 11100);
  hb_subset_plan_t* derived_plan =
      hb_subset_plan_create_or_fail(noto_sans_jp, input);
  hb_face_t* derived_face = hb_subset_plan_execute_or_fail(derived_plan);
  hb_blob_t* derived_blob = hb_face_reference_blob(derived_face);
  FontData derived = FontData::ToFontData(derived_face);
  ASSERT_TRUE(derived_plan);

  BrotliFontDiff differ;
  FontData patch;
  differ.Diff(base_plan, base_blob, derived_plan, derived_blob, &patch);

  Check(base, patch, derived);

  hb_subset_plan_destroy(base_plan);
  hb_subset_plan_destroy(derived_plan);
  hb_face_destroy(base_face);
  hb_face_destroy(derived_face);
  hb_blob_destroy(base_blob);
  hb_blob_destroy(derived_blob);
}

}  // namespace brotli
