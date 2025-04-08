#include "brotli/brotli_font_diff.h"

#include "absl/types/span.h"
#include "common/brotli_binary_patch.h"
#include "common/int_set.h"
#include "gtest/gtest.h"
#include "hb-subset.h"

namespace brotli {

using absl::Span;
using absl::Status;
using common::BrotliBinaryPatch;
using common::FontData;
using common::IntSet;

const std::string kTestDataDir = "common/testdata/";

/*
  for debugging:
void dump(const char* name, const char* data, unsigned size) {
  FILE* f = fopen(name, "w");
  fwrite(data, size, 1, f);
  fclose(f);
}
*/

class BrotliFontDiffTest : public ::testing::Test {
 protected:
  BrotliFontDiffTest() {}

  ~BrotliFontDiffTest() override {}

  void SetUp() override {
    hb_blob_t* font_data = hb_blob_create_from_file_or_fail(
        (kTestDataDir + "Roboto-Regular.ttf").c_str());
    ASSERT_TRUE(font_data);
    roboto = hb_face_create(font_data, 0);
    hb_blob_destroy(font_data);

    font_data = hb_blob_create_from_file_or_fail(
        (kTestDataDir + "NotoSansJP-Regular.ttf").c_str());
    ASSERT_TRUE(font_data);
    noto_sans_jp = hb_face_create(font_data, 0);
    hb_blob_destroy(font_data);

    input = hb_subset_input_create_or_fail();

    immutable_tables = IntSet{};
    custom_tables =
        IntSet{HB_TAG('g', 'l', 'y', 'f'), HB_TAG('l', 'o', 'c', 'a'),
               HB_TAG('h', 'm', 't', 'x'), HB_TAG('v', 'm', 't', 'x')};
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
    EXPECT_EQ(absl::OkStatus(), patcher.Patch(base, patch, &patched));
    // for debugging:
    // dump("derived.ttf", derived.data(), derived.size());
    // dump("patched.ttf", patched.data(), patched.size());

    EXPECT_EQ(derived.str(), patched.str());
  }

  void SortTables(hb_face_t* face, hb_face_t* subset) {
    BrotliFontDiff::SortForDiff(immutable_tables, custom_tables, face, subset);
  }

  IntSet immutable_tables;
  IntSet custom_tables;

  hb_face_t* roboto;
  hb_face_t* noto_sans_jp;
  hb_subset_input_t* input;
};

TEST_F(BrotliFontDiffTest, Diff) {
  hb_set_add_range(hb_subset_input_unicode_set(input), 0x41, 0x5A);
  hb_subset_plan_t* base_plan = hb_subset_plan_create_or_fail(roboto, input);
  hb_face_t* base_face = hb_subset_plan_execute_or_fail(base_plan);
  SortTables(roboto, base_face);
  hb_blob_t* base_blob = hb_face_reference_blob(base_face);
  FontData base(base_face);
  ASSERT_TRUE(base_plan);

  hb_set_add_range(hb_subset_input_unicode_set(input), 0x61, 0x7A);
  hb_subset_plan_t* derived_plan = hb_subset_plan_create_or_fail(roboto, input);
  hb_face_t* derived_face = hb_subset_plan_execute_or_fail(derived_plan);
  SortTables(roboto, derived_face);
  hb_blob_t* derived_blob = hb_face_reference_blob(derived_face);
  FontData derived(derived_face);
  ASSERT_TRUE(derived_plan);

  BrotliFontDiff differ(immutable_tables, custom_tables);
  FontData patch;
  ASSERT_EQ(
      differ.Diff(base_plan, base_blob, derived_plan, derived_blob, &patch),
      absl::OkStatus());

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
  SortTables(roboto, base_face);
  hb_blob_t* base_blob = hb_face_reference_blob(base_face);
  FontData base(base_face);
  ASSERT_TRUE(base_plan);

  hb_set_add(hb_subset_input_unicode_set(input), 0x47);
  hb_subset_plan_t* derived_plan = hb_subset_plan_create_or_fail(roboto, input);
  hb_face_t* derived_face = hb_subset_plan_execute_or_fail(derived_plan);
  SortTables(roboto, derived_face);
  hb_blob_t* derived_blob = hb_face_reference_blob(derived_face);
  FontData derived(derived_face);
  ASSERT_TRUE(derived_plan);

  BrotliFontDiff differ(immutable_tables, custom_tables);
  FontData patch;
  ASSERT_EQ(
      differ.Diff(base_plan, base_blob, derived_plan, derived_blob, &patch),
      absl::OkStatus());

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
  SortTables(noto_sans_jp, base_face);
  hb_blob_t* base_blob = hb_face_reference_blob(base_face);
  FontData base(base_face);
  ASSERT_TRUE(base_plan);

  hb_set_add_range(hb_subset_input_glyph_set(input), 500, 750);
  hb_set_add_range(hb_subset_input_glyph_set(input), 11000, 11100);
  hb_subset_plan_t* derived_plan =
      hb_subset_plan_create_or_fail(noto_sans_jp, input);
  hb_face_t* derived_face = hb_subset_plan_execute_or_fail(derived_plan);
  SortTables(noto_sans_jp, derived_face);
  hb_blob_t* derived_blob = hb_face_reference_blob(derived_face);
  FontData derived(derived_face);
  ASSERT_TRUE(derived_plan);

  BrotliFontDiff differ(immutable_tables, custom_tables);
  FontData patch;
  ASSERT_EQ(
      differ.Diff(base_plan, base_blob, derived_plan, derived_blob, &patch),
      absl::OkStatus());

  Check(base, patch, derived);

  hb_subset_plan_destroy(base_plan);
  hb_subset_plan_destroy(derived_plan);
  hb_face_destroy(base_face);
  hb_face_destroy(derived_face);
  hb_blob_destroy(base_blob);
  hb_blob_destroy(derived_blob);
}

TEST_F(BrotliFontDiffTest, ShortToLongLoca) {
  hb_set_add_range(hb_subset_input_glyph_set(input), 1000, 1200);
  hb_subset_plan_t* base_plan =
      hb_subset_plan_create_or_fail(noto_sans_jp, input);
  hb_face_t* base_face = hb_subset_plan_execute_or_fail(base_plan);
  SortTables(noto_sans_jp, base_face);
  hb_blob_t* base_blob = hb_face_reference_blob(base_face);
  FontData base(base_face);
  ASSERT_TRUE(base_plan);

  hb_set_add_range(hb_subset_input_glyph_set(input), 500, 750);
  hb_set_add_range(hb_subset_input_glyph_set(input), 1000, 5000);
  hb_set_add_range(hb_subset_input_glyph_set(input), 8000, 10000);
  hb_set_add_range(hb_subset_input_glyph_set(input), 11000, 11100);
  hb_subset_plan_t* derived_plan =
      hb_subset_plan_create_or_fail(noto_sans_jp, input);
  hb_face_t* derived_face = hb_subset_plan_execute_or_fail(derived_plan);
  SortTables(noto_sans_jp, derived_face);
  hb_blob_t* derived_blob = hb_face_reference_blob(derived_face);
  FontData derived(derived_face);
  ASSERT_TRUE(derived_plan);

  BrotliFontDiff differ(immutable_tables, custom_tables);
  FontData patch;
  ASSERT_EQ(
      differ.Diff(base_plan, base_blob, derived_plan, derived_blob, &patch),
      absl::OkStatus());

  Check(base, patch, derived);

  hb_subset_plan_destroy(base_plan);
  hb_subset_plan_destroy(derived_plan);
  hb_face_destroy(base_face);
  hb_face_destroy(derived_face);
  hb_blob_destroy(base_blob);
  hb_blob_destroy(derived_blob);
}

TEST_F(BrotliFontDiffTest, WithImmutableTables) {
  hb_subset_input_set_flags(input, HB_SUBSET_FLAGS_RETAIN_GIDS);
  hb_set_add(hb_subset_input_set(input, HB_SUBSET_SETS_NO_SUBSET_TABLE_TAG),
             HB_TAG('G', 'S', 'U', 'B'));
  hb_set_add(hb_subset_input_set(input, HB_SUBSET_SETS_NO_SUBSET_TABLE_TAG),
             HB_TAG('G', 'P', 'O', 'S'));

  immutable_tables.insert(HB_TAG('G', 'S', 'U', 'B'));
  immutable_tables.insert(HB_TAG('G', 'P', 'O', 'S'));

  hb_set_add_range(hb_subset_input_unicode_set(input), 0x41, 0x5A);
  hb_subset_plan_t* base_plan = hb_subset_plan_create_or_fail(roboto, input);
  hb_face_t* base_face = hb_subset_plan_execute_or_fail(base_plan);
  SortTables(roboto, base_face);
  hb_blob_t* base_blob = hb_face_reference_blob(base_face);
  FontData base(base_face);
  ASSERT_TRUE(base_plan);

  hb_set_add_range(hb_subset_input_unicode_set(input), 0x61, 0x7A);
  hb_subset_plan_t* derived_plan = hb_subset_plan_create_or_fail(roboto, input);
  hb_face_t* derived_face = hb_subset_plan_execute_or_fail(derived_plan);
  SortTables(roboto, derived_face);
  hb_blob_t* derived_blob = hb_face_reference_blob(derived_face);
  FontData derived(derived_face);
  ASSERT_TRUE(derived_plan);

  BrotliFontDiff differ(immutable_tables, custom_tables);
  FontData patch;
  ASSERT_EQ(
      differ.Diff(base_plan, base_blob, derived_plan, derived_blob, &patch),
      absl::OkStatus());

  Check(base, patch, derived);

  hb_subset_plan_destroy(base_plan);
  hb_subset_plan_destroy(derived_plan);
  hb_blob_destroy(base_blob);
  hb_blob_destroy(derived_blob);
  hb_face_destroy(base_face);
  hb_face_destroy(derived_face);
}

}  // namespace brotli
