#include "ift/encoder/unicode_range_compiler.h"

#include <memory>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "hb.h"
#include "ift/common/bazel_data_file_resolver.h"
#include "ift/common/font_data.h"
#include "ift/common/font_helper.h"
#include "ift/common/int_set.h"
#include "ift/common/test_font_loader.h"
#include "ift/common/woff2.h"
#include "ift/config/config_compiler.h"
#include "ift/config/segmentation_plan.pb.h"
#include "ift/encoder/closure_glyph_segmenter.h"

using ift::common::BazelDataFileResolver;
using ift::common::CodepointSet;
using ift::common::FontData;
using ift::common::FontHelper;
using ift::common::hb_face_unique_ptr;
using ift::common::Woff2;
using ift::config::CLOSURE_AND_VALIDATE_DEP_GRAPH;
using ift::config::ConfigCompiler;
using ift::config::FIND_CONDITIONS;
using ift::config::SegmentationPlan;

namespace ift::encoder {

class UnicodeRangeCompilerTest : public ::testing::Test {
 protected:
  UnicodeRangeCompilerTest()
      : roboto(ift::common::make_hb_face(nullptr)),
        noto_sans_jp(ift::common::make_hb_face(nullptr)) {
    auto loader = ift::common::TestFontLoader::Default().value();
    roboto =
        loader->LoadFace("ift/common/testdata/Roboto-Regular.ttf").value();
    noto_sans_jp =
        loader->LoadFace("ift/common/testdata/NotoSansJP-Regular.subset.ttf")
            .value();
    resolver = *BazelDataFileResolver::CreateForTest();
  }

  hb_face_unique_ptr roboto;
  hb_face_unique_ptr noto_sans_jp;
  std::shared_ptr<ift::common::DataFileResolver> resolver;
};

TEST_F(UnicodeRangeCompilerTest, FormatUnicodeRange) {
  EXPECT_EQ(UnicodeRangeCompiler::FormatUnicodeRange({}), "");
  EXPECT_EQ(UnicodeRangeCompiler::FormatUnicodeRange({'A'}), "U+41");
  EXPECT_EQ(UnicodeRangeCompiler::FormatUnicodeRange({'A', 'B', 'C', 'D'}),
            "U+41-44");
  EXPECT_EQ(UnicodeRangeCompiler::FormatUnicodeRange(
                {0x20, 0x41, 0x42, 0x43, 0x61, 0x1F600, 0x1F601}),
            "U+20, U+41-43, U+61, U+1F600-1F601");
}

TEST_F(UnicodeRangeCompilerTest, ExtractFontFamily) {
  EXPECT_EQ(UnicodeRangeCompiler::ExtractFontFamily(roboto.get()), "Roboto");
  EXPECT_EQ(UnicodeRangeCompiler::ExtractFontFamily(noto_sans_jp.get()),
            "Noto Sans JP Thin");
}

TEST_F(UnicodeRangeCompilerTest, CompileFromSegmentationPlan) {
  ClosureGlyphSegmenter segmenter(1, 1, FIND_CONDITIONS,
                                  CLOSURE_AND_VALIDATE_DEP_GRAPH, resolver,
                                  ift::config::UNICODE_RANGE);
  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {'a'}, {{'f'}, {'i'}, {'b', 'c', 'd'}});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  SegmentationPlan plan = segmentation->ToSegmentationPlanProto();

  UnicodeRangeCompiler compiler;
  compiler.SetFace(roboto.get());
  compiler.SetFontName("Roboto-Regular");
  compiler.SetBrotliQuality(1);
  ASSERT_TRUE(ConfigCompiler::Configure(plan, compiler).ok());

  auto encoding = compiler.Compile();
  ASSERT_TRUE(encoding.ok()) << encoding.status();

  EXPECT_EQ(encoding->css,
            R"(@font-face {
  font-family: 'Roboto';
  src: url('Roboto-Regular.0.woff2') format('woff2');
  unicode-range: U+66, U+69;
}

@font-face {
  font-family: 'Roboto';
  src: url('Roboto-Regular.1.woff2') format('woff2');
  unicode-range: U+62-64;
}
)");

  ASSERT_EQ(encoding->subsets.size(), 2u);
  ASSERT_TRUE(encoding->subsets.contains("Roboto-Regular.0.woff2"));
  ASSERT_TRUE(encoding->subsets.contains("Roboto-Regular.1.woff2"));

  // Decode each WOFF2 subset and verify its codepoints include 'a' (from
  // initial_segment) plus that subset's segment codepoints, and that subset 0
  // includes the fi and ffi ligature glyphs.
  FontData subset_0_ttf =
      *Woff2::DecodeWoff2(encoding->subsets.at("Roboto-Regular.0.woff2").str());
  hb_face_unique_ptr face_0 = subset_0_ttf.face();
  CodepointSet cps_0 = FontHelper::ToCodepointsSet(face_0.get());
  // Subset 0 retains 'a' (initial_segment), 'f', 'i', and the 'fi'/'ffi'
  // ligature glyphs from patch 0 (which are also mapped in Roboto's cmap to
  // U+FB01 and U+FB03).
  EXPECT_EQ(cps_0, (CodepointSet{'a', 'f', 'i', 0xFB01, 0xFB03}));
  // .notdef, 'a', 'f', 'i', 'fi', 'ffi'
  EXPECT_EQ(hb_face_get_glyph_count(face_0.get()), 6u);

  FontData subset_1_ttf =
      *Woff2::DecodeWoff2(encoding->subsets.at("Roboto-Regular.1.woff2").str());
  hb_face_unique_ptr face_1 = subset_1_ttf.face();
  CodepointSet cps_1 = FontHelper::ToCodepointsSet(face_1.get());
  EXPECT_EQ(cps_1, (CodepointSet{'a', 'b', 'c', 'd'}));
  // .notdef, 'a', 'b', 'c', 'd'
  EXPECT_EQ(hb_face_get_glyph_count(face_1.get()), 5u);
}

TEST_F(UnicodeRangeCompilerTest, FontFamilyOverrideAndUncompressedOutput) {
  UnicodeRangeCompiler compiler;
  compiler.SetFace(roboto.get());
  compiler.SetFontName("myfont");
  compiler.SetFontFamily("Custom Family");
  compiler.SetWoff2Encode(false);

  SubsetDefinition seg0;
  seg0.codepoints = {'a', 'b'};
  ASSERT_TRUE(compiler.AddSegment(0, seg0).ok());
  ASSERT_TRUE(compiler.AddGlyphDataPatch(0, {69, 70}).ok());
  ASSERT_TRUE(compiler.AddGlyphPatchCondition(0, 0).ok());

  auto encoding = compiler.Compile();
  ASSERT_TRUE(encoding.ok()) << encoding.status();

  EXPECT_EQ(encoding->css,
            R"(@font-face {
  font-family: 'Custom Family';
  src: url('myfont.0.ttf') format('truetype');
  unicode-range: U+61-62;
}
)");
  ASSERT_EQ(encoding->subsets.size(), 1u);
  ASSERT_TRUE(encoding->subsets.contains("myfont.0.ttf"));

  hb_face_unique_ptr face_0 = encoding->subsets.at("myfont.0.ttf").face();
  EXPECT_EQ(FontHelper::ToCodepointsSet(face_0.get()),
            (CodepointSet{'a', 'b'}));
}

TEST_F(UnicodeRangeCompilerTest, RejectsInvalidPlan) {
  // Non-exclusive condition
  {
    SegmentationPlan plan;
    (*plan.mutable_segments())[0].mutable_codepoints()->add_values('a');
    (*plan.mutable_segments())[1].mutable_codepoints()->add_values('b');
    (*plan.mutable_glyph_patches())[0].add_values(69);
    auto* cond = plan.add_glyph_patch_conditions();
    auto* req = cond->add_required_segments();
    req->add_values(0);
    req->add_values(1);
    cond->set_activated_patch(0);

    UnicodeRangeCompiler compiler;
    EXPECT_TRUE(
        absl::IsInvalidArgument(ConfigCompiler::Configure(plan, compiler)));
  }

  // Non-glyph (table-keyed) segmentation
  {
    SegmentationPlan plan;
    plan.add_non_glyph_codepoint_segmentation()->add_values('a');

    UnicodeRangeCompiler compiler;
    EXPECT_TRUE(
        absl::IsInvalidArgument(ConfigCompiler::Configure(plan, compiler)));
  }
}

}  // namespace ift::encoder
