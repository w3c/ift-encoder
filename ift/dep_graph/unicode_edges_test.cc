#include "ift/dep_graph/unicode_edges.h"
#include "ift/common/bazel_data_file_resolver.h"

#include <memory>

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "ift/common/font_data.h"
#include "ift/common/font_helper.h"

namespace ift::dep_graph {

using ift::common::FontHelper;
using ift::common::FontData;
using ift::common::hb_face_unique_ptr;
using ift::common::BazelDataFileResolver;
using ::testing::Contains;
using ::testing::Not;

hb_face_unique_ptr from_file(const char* filename) {
  hb_blob_t* blob = hb_blob_create_from_file_or_fail(filename);
  assert(blob);
  FontData result(blob);
  hb_blob_destroy(blob);
  return result.face();
}

TEST(UnicodeEdgesTest, ComputeUnicodeDependencyEdges_Roboto) {
  auto face = from_file("ift/common/testdata/Roboto-Regular.ttf");
  ASSERT_TRUE(face);

  auto resolver = *BazelDataFileResolver::CreateForTest();
  auto edges = UnicodeEdges::ComputeUnicodeDependencyEdges(face.get(), *resolver);
  ASSERT_TRUE(edges.ok()) << edges.status();

  // U+00C1 (Á) decomposes to U+0041 (A) and U+0301 (◌́)
  hb_codepoint_t A_acute = 0x00C1;
  hb_codepoint_t A = 0x0041;
  hb_codepoint_t acute = 0x0301;

  auto unicodes = FontHelper::ToCodepointsSet(face.get());
  ASSERT_TRUE(unicodes.contains(A_acute));

  // Check decomposition
  auto decomp_it = edges->decomposition.find(A_acute);
  ASSERT_NE(decomp_it, edges->decomposition.end());
  EXPECT_TRUE(decomp_it->second.contains(A));
  EXPECT_TRUE(decomp_it->second.contains(acute));

  // Check composition
  auto comp_it = edges->composition.find(A);
  ASSERT_NE(comp_it, edges->composition.end());
  EXPECT_THAT(comp_it->second, Contains(UnicodeConjunctiveEdge {
    .other_source = acute,
    .dest = A_acute,
  }));

  // There should also be an edge from acute
  comp_it = edges->composition.find(acute);
  ASSERT_NE(comp_it, edges->composition.end());
  EXPECT_THAT(comp_it->second, Contains(UnicodeConjunctiveEdge {
    .other_source = A,
    .dest = A_acute,
  }));
}

TEST(UnicodeEdgesTest, ComputeUnicodeDependencyEdges_UVS) {
  auto face = from_file("ift/common/testdata/NotoSansJP-Regular.ttf");
  ASSERT_TRUE(face);

  auto resolver = *BazelDataFileResolver::CreateForTest();
  auto edges = UnicodeEdges::ComputeUnicodeDependencyEdges(face.get(), *resolver);
  ASSERT_TRUE(edges.ok()) << edges.status();

  // Check a specific known mapping: U+4FAE with U+FE00 -> U+FA30
  hb_codepoint_t base_u = 0x4FAE;
  hb_codepoint_t vs_u = 0xFE00;

  auto dest_gid = edges->unicode_to_gid.find(0xFA30);
  ASSERT_NE(dest_gid, edges->unicode_to_gid.end());

  auto it = edges->variation_selector.find(base_u);
  ASSERT_NE(it, edges->variation_selector.end()) << "U+4FAE not found in variation_selector";
  EXPECT_THAT(it->second, Contains(VariationSelectorEdge {
    .unicode = vs_u,
    .gid = dest_gid->second,
  }));

  // Check the reverse mapping
  auto rev_it = edges->gid_to_vs.find(dest_gid->second);
  ASSERT_NE(rev_it, edges->gid_to_vs.end());
  EXPECT_FALSE(rev_it->second.contains(0xFA30));
  EXPECT_TRUE(rev_it->second.contains(0xFE00));
}

TEST(UnicodeEdgesTest, ComputeUnicodeDependencyEdges_CompositionExclusion) {
  auto face = from_file("ift/common/testdata/Roboto-Regular.ttf");
  ASSERT_TRUE(face);

  auto resolver = *BazelDataFileResolver::CreateForTest();
  auto edges = UnicodeEdges::ComputeUnicodeDependencyEdges(face.get(), *resolver);
  ASSERT_TRUE(edges.ok()) << edges.status();

  // U+2126 (OHM SIGN) decomposes to U+03A9 (GREEK CAPITAL LETTER OMEGA)
  // and is in the Full_Composition_Exclusion list (as a singleton).
  hb_codepoint_t ohm_sign = 0x2126;
  hb_codepoint_t omega = 0x03A9;

  auto unicodes = ift::common::FontHelper::ToCodepointsSet(face.get());
  ASSERT_TRUE(unicodes.contains(ohm_sign)) << "U+2126 not in Roboto-Regular.ttf";

  // Check decomposition still works
  auto decomp_it = edges->decomposition.find(ohm_sign);
  ASSERT_NE(decomp_it, edges->decomposition.end());
  EXPECT_TRUE(decomp_it->second.contains(omega));

  // Check composition does NOT contain ohm_sign
  auto comp_it = edges->composition.find(omega);
  if (comp_it != edges->composition.end()) {
    for (const auto& edge : comp_it->second) {
      EXPECT_NE(edge.dest, ohm_sign) << "Composition edge to U+2126 should be excluded";
    }
  }
}

}  // namespace ift::dep_graph
