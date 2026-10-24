#include "absl/status/status.h"
#include "absl/types/span.h"
#include "brotli/brotli_arena.h"
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

TEST_F(BrotliPatchingTest, DiffAndPatchWithSmallerDerived) {
  // Regression test for unsigned underflow in reserve() when derived < base.
  // Load a larger font as base and smaller subset as derived.
  FontData full_font =
      *loader_->LoadFontData("ift/common/testdata/Roboto-Regular.ttf");
  EXPECT_GT(full_font.size(), subset_a_.size());

  FontData patch;
  EXPECT_EQ(diff_->Diff(full_font, subset_a_, &patch), absl::OkStatus());

  EXPECT_GT(patch.size(), 0);
  EXPECT_LT(patch.size(), full_font.size());

  FontData patched;
  EXPECT_EQ(patch_->Patch(full_font, patch, &patched), absl::OkStatus());
  EXPECT_EQ(Span<const char>(patched), Span<const char>(subset_a_));
}

TEST(BrotliArenaTest, BumpAllocationAndAlignment) {
  brotli::BrotliArena arena(1024);
  EXPECT_EQ(arena.offset(), 0);
  EXPECT_EQ(arena.capacity(), 1024);

  constexpr size_t kAlign = brotli::BrotliArena::kAlignment;

  void* p1 = brotli::BrotliArena::Alloc(&arena, 10);
  ASSERT_NE(p1, nullptr);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p1) % kAlign, 0);
  // Header + aligned payload
  size_t expected_p1_offset = kAlign + ((10 + kAlign - 1) & ~(kAlign - 1));
  EXPECT_EQ(arena.offset(), expected_p1_offset);

  void* p2 = brotli::BrotliArena::Alloc(&arena, 24);
  ASSERT_NE(p2, nullptr);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p2) % kAlign, 0);
  size_t expected_p2_offset =
      expected_p1_offset + kAlign + ((24 + kAlign - 1) & ~(kAlign - 1));
  EXPECT_EQ(arena.offset(), expected_p2_offset);

  // LIFO free p2 then p1
  brotli::BrotliArena::Free(&arena, p2);
  EXPECT_EQ(arena.offset(), expected_p1_offset);

  brotli::BrotliArena::Free(&arena, p1);
  EXPECT_EQ(arena.offset(), 0);
}

TEST(BrotliArenaTest, ResetAndReallocation) {
  brotli::BrotliArena arena(1024);
  void* p1 = brotli::BrotliArena::Alloc(&arena, 64);
  EXPECT_GT(arena.offset(), 0);

  arena.Reset();
  EXPECT_EQ(arena.offset(), 0);

  void* p2 = brotli::BrotliArena::Alloc(&arena, 64);
  EXPECT_EQ(p1, p2);  // Reuses the exact same backing memory address
  EXPECT_GT(arena.offset(), 0);

  arena.Reset();
  EXPECT_EQ(arena.offset(), 0);
}

TEST(BrotliArenaTest, OversizedAllocationFallsBackToMalloc) {
  brotli::BrotliArena arena(256);
  // Allocate more than capacity
  void* p = brotli::BrotliArena::Alloc(&arena, 1024);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(arena.offset(), 0);  // Arena was bypassed

  // Freeing fallback allocation does not corrupt arena
  brotli::BrotliArena::Free(&arena, p);
  EXPECT_EQ(arena.offset(), 0);
}

TEST_F(BrotliPatchingTest, DiffReusesArenaAcrossMultipleCalls) {
  EXPECT_EQ(diff_->arena().offset(), 0);

  FontData patch1;
  EXPECT_EQ(diff_->Diff(subset_a_, subset_b_, &patch1), absl::OkStatus());
  EXPECT_EQ(diff_->arena().offset(), 0);  // Cleaned up after call

  FontData patch2;
  EXPECT_EQ(diff_->Diff(subset_a_, subset_b_, &patch2), absl::OkStatus());
  EXPECT_EQ(diff_->arena().offset(), 0);  // Cleaned up after call

  // Output must be bit-for-bit identical across calls
  EXPECT_EQ(Span<const char>(patch1), Span<const char>(patch2));

  // Also test with quality 11 (uses H10 binary tree hasher)
  BrotliBinaryDiff diff11(11);
  EXPECT_EQ(diff11.arena().offset(), 0);

  FontData patch11_1;
  EXPECT_EQ(diff11.Diff(subset_a_, subset_b_, &patch11_1), absl::OkStatus());
  EXPECT_EQ(diff11.arena().offset(), 0);

  FontData patch11_2;
  EXPECT_EQ(diff11.Diff(subset_a_, subset_b_, &patch11_2), absl::OkStatus());
  EXPECT_EQ(diff11.arena().offset(), 0);

  EXPECT_EQ(Span<const char>(patch11_1), Span<const char>(patch11_2));
}

}  // namespace ift::common
