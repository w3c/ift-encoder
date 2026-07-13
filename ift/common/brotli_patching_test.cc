#include "absl/status/status.h"
#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "ift/common/brotli_binary_diff.h"
#include "ift/common/brotli_binary_patch.h"
#include "ift/common/test_font_loader.h"

namespace ift::common {

using absl::Span;
using absl::Status;

class BrotliPatchingTest : public ::testing::Test {
 protected:
  BrotliPatchingTest()
      : loader_(TestFontLoader::Default().value()),
        diff_(new BrotliBinaryDiff()),
        patch_(new BrotliBinaryPatch()) {}

  ~BrotliPatchingTest() override {}

  void SetUp() override {
    subset_a_ = *loader_->LoadFontData("ift/common/testdata/Roboto-Regular.Meows.ttf");
    subset_b_ = *loader_->LoadFontData("ift/common/testdata/Roboto-Regular.Awesome.ttf");
    EXPECT_GT(subset_a_.size(), 0);
    EXPECT_GT(subset_b_.size(), 0);
  }

  std::unique_ptr<TestFontLoader> loader_;
  std::unique_ptr<BrotliBinaryDiff> diff_;
  std::unique_ptr<BinaryPatch> patch_;
  FontData subset_a_;
  FontData subset_b_;
};

TEST_F(BrotliPatchingTest, DiffAndPatchPatchWithEmptyBase) {
  FontData empty;
  FontData patch;
  EXPECT_EQ(diff_->Diff(empty, subset_a_, &patch), absl::OkStatus());

  EXPECT_GT(patch.size(), 0);
  EXPECT_LT(patch.size(), subset_a_.size());
  EXPECT_NE(Span<const char>(patch), Span<const char>(subset_a_));

  FontData patched;
  EXPECT_EQ(patch_->Patch(empty, patch, &patched), absl::OkStatus());
  EXPECT_EQ(Span<const char>(patched), Span<const char>(subset_a_));
}

TEST_F(BrotliPatchingTest, DiffAndPatch) {
  FontData patch;
  EXPECT_EQ(diff_->Diff(subset_a_, subset_b_, &patch), absl::OkStatus());

  EXPECT_GT(patch.size(), 0);
  EXPECT_LT(patch.size(), subset_a_.size());
  EXPECT_LT(patch.size(), subset_b_.size());
  EXPECT_NE(Span<const char>(patch), Span<const char>(subset_a_));
  EXPECT_NE(Span<const char>(patch), Span<const char>(subset_b_));

  FontData patched;
  EXPECT_EQ(patch_->Patch(subset_a_, patch, &patched), absl::OkStatus());
  EXPECT_EQ(Span<const char>(patched), Span<const char>(subset_b_));
}

TEST_F(BrotliPatchingTest, StitchingWithEmptyBase) {
  FontData empty;

  std::vector<uint8_t> sink;
  EXPECT_EQ(diff_->Diff(empty, subset_a_.str(0, 1000), 0, false, sink),
            absl::OkStatus());

  EXPECT_EQ(diff_->Diff(empty, subset_a_.str(1000), 1000, true, sink),
            absl::OkStatus());

  FontData patch;
  patch.copy(reinterpret_cast<const char*>(sink.data()), sink.size());

  EXPECT_GT(patch.size(), 0);
  EXPECT_LT(patch.size(), subset_a_.size());
  EXPECT_NE(Span<const char>(patch), Span<const char>(subset_a_));

  FontData patched;
  EXPECT_EQ(patch_->Patch(empty, patch, &patched), absl::OkStatus());
  EXPECT_EQ(Span<const char>(patched), Span<const char>(subset_a_));
}

TEST_F(BrotliPatchingTest, StitchingWithBase) {
  std::vector<uint8_t> sink;
  EXPECT_EQ(diff_->Diff(subset_a_, subset_b_.str(0, 1000), 0, false, sink),
            absl::OkStatus());

  EXPECT_EQ(diff_->Diff(subset_a_, subset_b_.str(1000), 1000, true, sink),
            absl::OkStatus());

  FontData patch;
  patch.copy(reinterpret_cast<const char*>(sink.data()), sink.size());

  EXPECT_GT(patch.size(), 0);
  EXPECT_LT(patch.size(), subset_a_.size());
  EXPECT_LT(patch.size(), subset_b_.size());
  EXPECT_NE(Span<const char>(patch), Span<const char>(subset_a_));
  EXPECT_NE(Span<const char>(patch), Span<const char>(subset_b_));

  FontData patched;
  EXPECT_EQ(patch_->Patch(subset_a_, patch, &patched), absl::OkStatus());
  EXPECT_EQ(Span<const char>(patched), Span<const char>(subset_b_));
}

}  // namespace ift::common
