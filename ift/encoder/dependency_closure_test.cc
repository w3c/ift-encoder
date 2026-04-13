#include "ift/encoder/dependency_closure.h"

#include <memory>
#include <vector>

#include "gtest/gtest.h"
#include "ift/common/font_data.h"
#include "ift/common/font_helper.h"
#include "ift/common/int_set.h"
#include "ift/config/common.pb.h"
#include "ift/dep_graph/node.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/init_subset_defaults.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"
#include "ift/freq/probability_bound.h"

using ift::config::PATCH;

using absl::btree_set;
using absl::flat_hash_map;
using absl::Status;
using ift::common::CodepointSet;
using ift::common::FontData;
using ift::common::FontHelper;
using ift::common::GlyphSet;
using ift::common::hb_face_unique_ptr;
using ift::common::make_hb_font;
using ift::common::SegmentSet;
using ift::dep_graph::Node;
using ift::freq::ProbabilityBound;

namespace ift::encoder {

class DependencyClosureTest : public ::testing::Test {
 protected:
  DependencyClosureTest()
      : face(from_file("ift/common/testdata/Roboto-Regular.ttf")),
        double_nested_face(
            from_file("ift/common/testdata/double-nested-components.ttf")),
        noto_sans_jp(from_file("ift/common/testdata/NotoSansJP-Regular.ttf")),
        noto_sans_jp_vf(
            from_file("ift/common/testdata/NotoSansJP-VF.cmap14.ttf")),
        roboto_vf(from_file("ift/common/testdata/Roboto[wdth,wght].ttf")),
        closure_cache(face.get()),
        segmentation_info(*RequestedSegmentationInformation::Create(
            segments, WithDefaultFeatures(), closure_cache, PATCH)),
        dependency_closure(
            *DependencyClosure::Create(segmentation_info.get(), face.get())) {}

  static SubsetDefinition WithDefaultFeatures() {
    SubsetDefinition def;
    AddInitSubsetDefaults(def);
    return def;
  }

  static SubsetDefinition WithDefaultFeatures(CodepointSet codepoints) {
    SubsetDefinition def;
    def.codepoints = codepoints;
    AddInitSubsetDefaults(def);
    return def;
  }

  static hb_face_unique_ptr from_file(const char* filename) {
    hb_blob_t* blob = hb_blob_create_from_file_or_fail(filename);
    if (!blob) {
      assert(false);
    }
    FontData result(blob);
    hb_blob_destroy(blob);
    return result.face();
  }

  std::vector<Segment> segments = {
      /* 0 */ {{'a'}, ProbabilityBound::Zero()},
      /* 1 */ {{'f'}, ProbabilityBound::Zero()},
      /* 2 */ {{'i'}, ProbabilityBound::Zero()},
      /* 3 */ {{'q'}, ProbabilityBound::Zero()},
      /* 4 */ {{'A'}, ProbabilityBound::Zero()},
      /* 5 */ {{0xC1 /* Aacute */}, ProbabilityBound::Zero()},
  };

  void Reconfigure(SubsetDefinition new_init,
                   std::vector<Segment> new_segments) {
    segmentation_info = *RequestedSegmentationInformation::Create(
        new_segments, new_init, closure_cache, PATCH);
    dependency_closure =
        *DependencyClosure::Create(segmentation_info.get(), face.get());
  }

  void Reconfigure(hb_face_t* new_face, SubsetDefinition new_init,
                   std::vector<Segment> new_segments) {
    closure_cache = GlyphClosureCache(new_face);
    segmentation_info = *RequestedSegmentationInformation::Create(
        new_segments, new_init, closure_cache, PATCH);
    dependency_closure =
        *DependencyClosure::Create(segmentation_info.get(), new_face);
  }

  Status RejectedAnalysis(segment_index_t segment) {
    GlyphSet and_gids;
    GlyphSet or_gids;
    GlyphSet exclusive_gids;

    if (TRY(dependency_closure->AnalyzeSegment({segment}, and_gids, or_gids,
                                               exclusive_gids)) ==
        DependencyClosure::ACCURATE) {
      return absl::InternalError(
          "Dependency closure analysis should have been rejected.");
    }
    return absl::OkStatus();
  }

  Status CompareAnalysis(SegmentSet segments) {
    GlyphSet and_gids;
    GlyphSet or_gids;
    GlyphSet exclusive_gids;

    if (TRY(dependency_closure->AnalyzeSegment(segments, and_gids, or_gids,
                                               exclusive_gids)) !=
        DependencyClosure::ACCURATE) {
      return absl::InternalError(
          "Dependency closure analysis rejected unexpectedly.");
    }

    GlyphSet expected_and_gids;
    GlyphSet expected_or_gids;
    GlyphSet expected_exclusive_gids;
    TRYV(closure_cache.AnalyzeSegment(*segmentation_info, segments,
                                      expected_and_gids, expected_or_gids,
                                      expected_exclusive_gids));

    std::string message;
    bool success = true;
    if (!CompareGids("AND", and_gids, expected_and_gids, message)) {
      success = false;
    }
    if (!CompareGids("OR", or_gids, expected_or_gids, message)) {
      success = false;
    }
    if (!CompareGids("EXCLUSIVE", exclusive_gids, expected_exclusive_gids,
                     message)) {
      success = false;
    }

    if (!success) {
      return absl::InvalidArgumentError(message);
    }
    return absl::OkStatus();
  }

  bool CompareGids(absl::string_view name, const GlyphSet& actual,
                   const GlyphSet& expected, std::string& message) {
    std::string op = " == ";
    bool result = true;
    if (actual != expected) {
      op = " != ";
      result = false;
    }

    absl::StrAppend(&message, name, " glyphs: actual ", actual.ToString(), op,
                    "expected ", expected.ToString(), "\n");
    return result;
  }

  hb_face_unique_ptr face;
  hb_face_unique_ptr double_nested_face;
  hb_face_unique_ptr noto_sans_jp;
  hb_face_unique_ptr noto_sans_jp_vf;
  hb_face_unique_ptr roboto_vf;
  GlyphClosureCache closure_cache;
  std::unique_ptr<RequestedSegmentationInformation> segmentation_info;
  std::unique_ptr<DependencyClosure> dependency_closure;
};

TEST_F(DependencyClosureTest, AddsToSets) {
  GlyphSet and_gids{101};
  GlyphSet or_gids{102};
  GlyphSet exclusive_gids{103};

  auto r = dependency_closure->AnalyzeSegment({0}, and_gids, or_gids,
                                              exclusive_gids);
  ASSERT_TRUE(r.ok()) << r.status();
  ASSERT_EQ(*r, DependencyClosure::ACCURATE);

  ASSERT_EQ(and_gids, (GlyphSet{101}));
  ASSERT_EQ(or_gids, (GlyphSet{102}));
  ASSERT_EQ(exclusive_gids, (GlyphSet{69, 103}));
}

TEST_F(DependencyClosureTest, ExtractAllGlyphConditions) {
  Reconfigure(WithDefaultFeatures(),
              {
                  /* 0 */ {{'a'}, ProbabilityBound::Zero()},
                  /* 1 */ {{'f'}, ProbabilityBound::Zero()},
                  /* 2 */ {{'i'}, ProbabilityBound::Zero()},
                  /* 3 */ {{'q'}, ProbabilityBound::Zero()},
                  /* 4 */ {{'A'}, ProbabilityBound::Zero()},
                  /* 5 */ {{0xC1 /* Aacute */}, ProbabilityBound::Zero()},
              });

  auto r = dependency_closure->ExtractAllGlyphConditions();
  ASSERT_TRUE(r.ok()) << r.status();
  const auto& conditions = *r;

  // f (gid 74) is in segment 1
  ASSERT_EQ(conditions.at(74).ToString(), "if (s1) then p0");

  // i (gid 77) is in segment 2
  ASSERT_EQ(conditions.at(77).ToString(), "if (s2) then p0");

  // A (gid 37) is in segment 4, but also needed by Aacute in segment 5
  // so it's s4 OR s5
  ASSERT_EQ(conditions.at(37).ToString(), "if ((s4 OR s5)) then p0");

  // Aacute (gid 117) is in segment 5
  ASSERT_EQ(conditions.at(117).ToString(), "if (s5) then p0");
}

TEST_F(DependencyClosureTest, ExtractAllGlyphConditions_InitFont) {
  // Move 'f' to init font
  Reconfigure(face.get(), WithDefaultFeatures({'f'}),
              {
                  /* 0 */ {{'a'}, ProbabilityBound::Zero()},
                  /* 1 */ {{'i'}, ProbabilityBound::Zero()},
              });

  auto r = dependency_closure->ExtractAllGlyphConditions();
  ASSERT_TRUE(r.ok()) << r.status();
  const auto& conditions = *r;

  // 'f' is in init font, so it's not in the result map
  ASSERT_FALSE(conditions.contains(74));

  // i (gid 77) is in segment 1
  ASSERT_EQ(conditions.at(77).ToString(), "if (s1) then p0");
}

TEST_F(DependencyClosureTest, ExtractAllGlyphConditions_Composite) {
  SubsetDefinition aalt;
  aalt.feature_tags.insert(HB_TAG('a', 'a', 'l', 't'));
  SubsetDefinition jp78;
  jp78.feature_tags.insert(HB_TAG('j', 'p', '7', '8'));

  Reconfigure(noto_sans_jp_vf.get(), {},
              {
                  /* 0 */ {{0x6717}, ProbabilityBound::Zero()},
                  /* 1 */ {{0x7891}, ProbabilityBound::Zero()},
                  /* 2 */ {{0x798f}, ProbabilityBound::Zero()},
                  /* 3 */ {{0x6406}, ProbabilityBound::Zero()},
                  /* 4 */ {{0xe0100}, ProbabilityBound::Zero()},
                  /* 5 */ {{0xfe00}, ProbabilityBound::Zero()},
                  /* 6 */ {aalt, ProbabilityBound::Zero()},
                  /* 7 */ {jp78, ProbabilityBound::Zero()},
              });

  auto r = dependency_closure->ExtractAllGlyphConditions();
  ASSERT_TRUE(r.ok()) << r.status();
  const auto& conditions = *r;

  // g8 is reached via one of two cmap14 entries (U+7891 + U+FE00) or (U+7891 +
  // U+E0100).
  ASSERT_EQ(conditions.at(8).ToString(), "if (s1 AND (s4 OR s5)) then p0");

  // g9 is reached via a single subst of U+6406 which is gated on either
  // aalt or jp78
  ASSERT_EQ(conditions.at(9).ToString(), "if (s3 AND (s6 OR s7)) then p0");
}

TEST_F(DependencyClosureTest, ExtractAllGlyphConditions_PhaseIsolation) {
  SubsetDefinition ccmp;
  ccmp.feature_tags = {HB_TAG('c', 'c', 'm', 'p')};

  Reconfigure({},
              {
                  /* 0 */ {{0x132 /* IJ */}, ProbabilityBound::Zero()},
                  /* 1 */ {{0xCD /* Iacute */}, ProbabilityBound::Zero()},
                  /* 2 */ {{0x301 /* acutecomb */}, ProbabilityBound::Zero()},
                  /* 3 */ {{ccmp}, ProbabilityBound::Zero()},
              });

  auto r = dependency_closure->ExtractAllGlyphConditions();
  ASSERT_TRUE(r.ok()) << r.status();
  const auto& conditions = *r;

  // Iacute is reachable via the graph from IJ -glyf-> I -GSUB-> Iacute,
  // but IJ shouldn't be in the conditions as the path requires walking glyf
  // before GSUB edges.
  EXPECT_EQ(conditions.at(652 /* Iacute */).ToString(), "if (s1) then p0");
}

TEST_F(DependencyClosureTest, ExtractAllGlyphConditions_FullFont) {
  CodepointSet unicodes = FontHelper::ToCodepointsSet(face.get());
  btree_set<hb_tag_t> features = FontHelper::GetFeatureTags(face.get());

  std::vector<Segment> segments;
  flat_hash_map<hb_codepoint_t, segment_index_t> cp_to_seg;
  flat_hash_map<hb_tag_t, segment_index_t> layout_to_seg;
  for (hb_codepoint_t cp : unicodes) {
    cp_to_seg[cp] = segments.size();
    segments.push_back({{cp}, ProbabilityBound::Zero()});
  }
  for (hb_tag_t feature : features) {
    layout_to_seg[feature] = segments.size();
    SubsetDefinition f;
    f.feature_tags.insert(feature);
    segments.push_back({f, ProbabilityBound::Zero()});
  }

  Reconfigure({}, segments);

  auto conditions = dependency_closure->ExtractAllGlyphConditions();
  ASSERT_TRUE(conditions.ok()) << conditions.status();

  uint32_t parenleft = cp_to_seg.at('(');
  uint32_t parenright = cp_to_seg.at(')');

  EXPECT_EQ(
      conditions->at(12 /* parenleft */).ToString(),
      // '(' is accesible from either '(' or ')' due to unicode mirroring.
      absl::StrCat("if ((s", parenleft, " OR s", parenright, ")) then p0"));

  // small caps AE is accesible via numerous pathways and forms a complex
  // composite condition (smcp, c2sc, and glyph component substitutions)
  uint32_t AE = cp_to_seg.at(0xC6);
  uint32_t ae = cp_to_seg.at(0xE6);
  uint32_t AEacute = cp_to_seg.at(0x1FC);
  uint32_t aeacute = cp_to_seg.at(0x1FD);
  uint32_t smcp = layout_to_seg.at(HB_TAG('s', 'm', 'c', 'p'));
  uint32_t c2sc = layout_to_seg.at(HB_TAG('c', '2', 's', 'c'));
  EXPECT_EQ(
      conditions->at(627 /* small caps AE */).ToString(),
      absl::StrCat("if ((s", AE, " OR s", ae, " OR s", AEacute, " OR s",
                   aeacute, ") ", "AND (s", AE, " OR s", AEacute, " OR s", smcp,
                   ") ", "AND (s", ae, " OR s", aeacute, " OR s", c2sc, ") ",
                   "AND (s", c2sc, " OR s", smcp, ")) then p0"));
}

TEST_F(DependencyClosureTest, ExtractAllGlyphConditions_PhaseCycle) {
  // This is a case where's there's a cycle in the graph unless you
  // process phase by phase.
  //
  // Cycle: AE -GSUB-> AEacute -glyf-> AE
  SubsetDefinition ccmp;
  ccmp.feature_tags = {HB_TAG('c', 'c', 'm', 'p')};
  Reconfigure({},
              {
                  /* 0 */ {{0xc6 /* AE */}, ProbabilityBound::Zero()},
                  /* 1 */ {{0x301 /* acutecomb */}, ProbabilityBound::Zero()},
                  /* 2 */ {{ccmp}, ProbabilityBound::Zero()},
              });

  auto r = dependency_closure->ExtractAllGlyphConditions();
  ASSERT_TRUE(r.ok()) << r.status();
  const auto& conditions = *r;

  EXPECT_EQ("if (s0) then p0", conditions.at(129 /* AE */).ToString());
  EXPECT_EQ("if (s0 AND s1 AND s2) then p0",
            conditions.at(117 /* acute */).ToString());
}

TEST_F(DependencyClosureTest, ExtractAllGlyphConditions_BidiCycle) {
  Reconfigure(face.get(), WithDefaultFeatures(),
              {
                  /* 0 */ {{0x0029 /* ) */}, ProbabilityBound::Zero()},
                  /* 1 */ {{0x0028 /* ( */}, ProbabilityBound::Zero()},
              });

  auto r = dependency_closure->ExtractAllGlyphConditions();
  ASSERT_TRUE(r.ok()) << r.status();
  const auto& conditions = *r;

  // GID 11 is '(', GID 12 is ')' in Roboto
  // Both should be reached if either segment is present due to the cycle.
  EXPECT_EQ(conditions.at(12).ToString(), "if ((s0 OR s1)) then p0");
  EXPECT_EQ(conditions.at(13).ToString(), "if ((s0 OR s1)) then p0");
}

TEST_F(DependencyClosureTest, Exclusive) {
  Status s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({3});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({0, 3});
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, Liga) {
  // Basic liga case split between should be able to be analyzed
  Status s = CompareAnalysis({1});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({2});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({1, 2});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({1, 2, 3});
  ASSERT_TRUE(s.ok()) << s;

  // One half of the liga is in the init font
  Reconfigure(face.get(), WithDefaultFeatures({'f'}),
              {
                  {{'a'}, ProbabilityBound::Zero()},
                  {{'i'}, ProbabilityBound::Zero()},
              });
  s = CompareAnalysis({1});
  ASSERT_TRUE(s.ok()) << s;

  // Should also work when the liga feature is in a segment
  SubsetDefinition liga;
  liga.feature_tags = {HB_TAG('l', 'i', 'g', 'a')};

  Reconfigure({}, {
                      {liga, ProbabilityBound::Zero()},
                      {{'f'}, ProbabilityBound::Zero()},
                      {{'i'}, ProbabilityBound::Zero()},
                  });

  s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({1});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({2});
  ASSERT_TRUE(s.ok()) << s;

  s = CompareAnalysis({0, 2});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({1, 2});
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, OverlappingSegments) {
  // One half of the liga is in the init font
  Reconfigure(face.get(), WithDefaultFeatures({}),
              {
                  {{'f'}, ProbabilityBound::Zero()},
                  {{'i'}, ProbabilityBound::Zero()},
                  {{'f', 'i'}, ProbabilityBound::Zero()},
              });

  Status s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;

  s = CompareAnalysis({1});
  ASSERT_TRUE(s.ok()) << s;

  s = RejectedAnalysis(2);
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, UnicodeToGid_ExcludesInitFont) {
  // 0x7528 and 0x2F64 share the same glyph
  Reconfigure(noto_sans_jp.get(), {0x7528},
              {
                  {{0x2F64}, ProbabilityBound::Zero()},
              });
  Status s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, Disjunctive_Components) {
  // This test font has the graph
  // a -> c -> d
  // b -> c
  //   -> e
  //
  // In this case when analyzing segment 0 (a), d will appear to
  // have no extra incoming edges and with a naive implementation
  // would be classified as exclusive, where it should be disjunctive
  // since it's reachable via segment 1 (through b -> c -> d).
  Reconfigure(double_nested_face.get(), {},
              {
                  {{'a'}, ProbabilityBound::Zero()},
                  {{'b'}, ProbabilityBound::Zero()},
              });
  Status s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({1});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({0, 1});
  ASSERT_TRUE(s.ok()) << s;

  Reconfigure(double_nested_face.get(), {'a'},
              {
                  {{'b'}, ProbabilityBound::Zero()},
              });
  s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;

  Reconfigure(double_nested_face.get(), {'b'},
              {
                  {{'a'}, ProbabilityBound::Zero()},
              });
  s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, CodepointNotInFont) {
  Reconfigure(double_nested_face.get(), {},
              {
                  {{'A'}, ProbabilityBound::Zero()},
              });
  Status s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, SingleSubst) {
  SubsetDefinition c2sc;
  c2sc.feature_tags.insert(HB_TAG('c', '2', 's', 'c'));

  Reconfigure(face.get(), {},
              {
                  /* 0 */ {{'a'}, ProbabilityBound::Zero()},
                  /* 1 */ {{'b'}, ProbabilityBound::Zero()},
                  /* 2 */ {{'A'}, ProbabilityBound::Zero()},
                  /* 3 */ {{0x1FC /* AEacute */}, ProbabilityBound::Zero()},
                  /* 4 */ {c2sc, ProbabilityBound::Zero()},
              });

  Status s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;

  // c2sc not in init font which A passes through.
  // can still be analyzed in most cases.
  s = CompareAnalysis({2});
  ASSERT_TRUE(s.ok()) << s;
  // rejected due to possible non-disjunctive interactions via c2sc + A
  s = RejectedAnalysis(3);
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({4});
  ASSERT_TRUE(s.ok()) << s;

  // With c2sc in the init font, we can still analyze the single subst's
  Reconfigure(face.get(), c2sc,
              {
                  /* 0 */ {{'a'}, ProbabilityBound::Zero()},
                  /* 1 */ {{'b'}, ProbabilityBound::Zero()},
                  /* 2 */ {{'A'}, ProbabilityBound::Zero()},
                  /* 3 */ {{0x1FC /* AEacute */}, ProbabilityBound::Zero()},
              });

  s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({2});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({3});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({2, 3});
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, Disjunctive) {
  // disable features so those don't create disallowed edges.
  Reconfigure({}, segments);

  // Here 'A' is a component of 'Aacute' so 'A's condition is s4 or s5
  Status s = CompareAnalysis({4});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({5});
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, DisjunctivePartialInInitFont) {
  // One half of a disjunctive dep is in the init font.
  Reconfigure({'A'}, {{{0xC1}, ProbabilityBound::Zero()}});
  Status s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;

  Reconfigure({0xC1}, {{{'A'}, ProbabilityBound::Zero()}});
  s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, AlreadyInInitFont) {
  // Analysis of something already in the init font is a noop.
  Reconfigure({'A'}, {
                         {{'A'}, ProbabilityBound::Zero()},
                         {{0xC1}, ProbabilityBound::Zero()},
                     });
  Status s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, SegmentOutOfBounds) {
  Status s = CompareAnalysis({10});
  ASSERT_TRUE(absl::IsInvalidArgument(s)) << s;
}

TEST_F(DependencyClosureTest, Rejected_LookAheadGlyphs) {
  Reconfigure(WithDefaultFeatures({}), {
                                           {{'i'}, ProbabilityBound::Zero()},
                                           {{0x485}, ProbabilityBound::Zero()},
                                       });

  Status s = RejectedAnalysis(0);
  ASSERT_TRUE(s.ok()) << s;

  // glyph for 0x485 is part of a lookahead on a chain context sub
  // so analysis should be rejected.
  s = RejectedAnalysis(1);
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, Rejected_InitFontContext) {
  Reconfigure(WithDefaultFeatures({'i'}),
              {
                  {{0x300 /* gravecomb */}, ProbabilityBound::Zero()},
                  {{0x485}, ProbabilityBound::Zero()},
              });

  // Gravecomb doesn't pass through any contextual dependencies, but
  // it shows up as a context glyph for the init font subset.
  // Thus it should be rejected.
  Status s = RejectedAnalysis(0);
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, Noop_Features) {
  SubsetDefinition features;
  features.feature_tags.insert(HB_TAG('a', 'b', 'c', 'd'));
  Reconfigure({}, {
                      {{'a'}, ProbabilityBound::Zero()},
                      {features, ProbabilityBound::Zero()},
                  });

  Status s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;

  s = CompareAnalysis({1});
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, UVS) {
  Reconfigure(noto_sans_jp.get(), {},
              {
                  {{0x4fae}, ProbabilityBound::Zero()},
                  {{0xfe00}, ProbabilityBound::Zero()},
              });

  Status s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;

  s = CompareAnalysis({1});
  ASSERT_TRUE(s.ok()) << s;

  s = CompareAnalysis({0, 1});
  ASSERT_TRUE(s.ok()) << s;

  Reconfigure(noto_sans_jp.get(), {},
              {
                  {{0x4fae, 0xfe00}, ProbabilityBound::Zero()},
              });
  s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;

  Reconfigure(noto_sans_jp.get(), {0x4fae},
              {
                  {{0xfe00}, ProbabilityBound::Zero()},
              });
  s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;

  Reconfigure(noto_sans_jp.get(), {0xfe00},
              {
                  {{0x4fae}, ProbabilityBound::Zero()},
              });
  s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, UvsAndFeatures_ConflictingConjunctiveConditions) {
  SubsetDefinition aalt;
  aalt.feature_tags.insert(HB_TAG('a', 'a', 'l', 't'));
  SubsetDefinition jp78;
  jp78.feature_tags.insert(HB_TAG('j', 'p', '7', '8'));

  Reconfigure(noto_sans_jp_vf.get(), {},
              {
                  /* 0 */ {{0x6717}, ProbabilityBound::Zero()},
                  /* 1 */ {{0x7891}, ProbabilityBound::Zero()},
                  /* 2 */ {{0x798f}, ProbabilityBound::Zero()},
                  /* 3 */ {{0x6406}, ProbabilityBound::Zero()},
                  /* 4 */ {{0xe0100}, ProbabilityBound::Zero()},
                  /* 5 */ {{0xfe00}, ProbabilityBound::Zero()},
                  /* 6 */ {aalt, ProbabilityBound::Zero()},
                  /* 7 */ {jp78, ProbabilityBound::Zero()},
              });

  // Some of these are rejected since there are multiple conflicting
  // ways of reaching their glyphs (eg. via UVS and layout features)
  Status s = RejectedAnalysis(0);
  ASSERT_TRUE(s.ok()) << s;
  s = RejectedAnalysis(1);
  ASSERT_TRUE(s.ok()) << s;
  s = RejectedAnalysis(3);
  ASSERT_TRUE(s.ok()) << s;
  s = RejectedAnalysis(4);
  ASSERT_TRUE(s.ok()) << s;
  s = RejectedAnalysis(5);
  ASSERT_TRUE(s.ok()) << s;
  s = RejectedAnalysis(6);
  ASSERT_TRUE(s.ok()) << s;
  s = RejectedAnalysis(7);
  ASSERT_TRUE(s.ok()) << s;

  // s2 can be analyzed since it doesn't have multiple conjunctive conditions
  s = CompareAnalysis({2});
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, Rejected_FullySatisfiedContext) {
  Reconfigure(WithDefaultFeatures({}),
              {
                  {{'i', 0x300 /* gravecomb */}, ProbabilityBound::Zero()},
              });

  // The segment contains everything needed to activated the i + gravecomb
  // contextual lookup, but should still be rejected on account of passing
  // through a contextual lookup.
  Status s = RejectedAnalysis(0);
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, BidiMirroring) {
  // Test that the dep graph analysis accounts for bidi mirroring in the
  // harfbuzz closure
  Reconfigure({}, {
                      {{0x2264 /* less equal */}, ProbabilityBound::Zero()},
                      {{0x2265 /* greater equal */}, ProbabilityBound::Zero()},
                  });

  Status s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;

  s = CompareAnalysis({1});
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, SegmentsChanged) {
  // Test that the dep graph analysis accounts for bidi mirroring in the
  // harfbuzz closure
  Reconfigure({}, {
                      {{'a'}, ProbabilityBound::Zero()},
                      {{'b'}, ProbabilityBound::Zero()},
                  });

  Status s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({1});
  ASSERT_TRUE(s.ok()) << s;

  s = segmentation_info->ReassignInitSubset(closure_cache, {'a'});
  ASSERT_TRUE(s.ok()) << s;

  s = dependency_closure->SegmentsChanged(true, SegmentSet::all());
  ASSERT_TRUE(s.ok()) << s;

  s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({1});
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, SegmentsThatInteractWith_Nodes) {
  SubsetDefinition aalt;
  aalt.feature_tags.insert(HB_TAG('a', 'a', 'l', 't'));
  SubsetDefinition jp78;
  jp78.feature_tags.insert(HB_TAG('j', 'p', '7', '8'));
  Reconfigure(noto_sans_jp_vf.get(), {},
              {
                  /* 0 */ {{0x6717}, ProbabilityBound::Zero()},
                  /* 1 */ {{0x7891}, ProbabilityBound::Zero()},
                  /* 2 */ {{0x798f}, ProbabilityBound::Zero()},
                  /* 3 */ {{0x6406}, ProbabilityBound::Zero()},
                  /* 4 */ {{0xe0100}, ProbabilityBound::Zero()},
                  /* 5 */ {{0xfe00}, ProbabilityBound::Zero()},
                  /* 6 */ {aalt, ProbabilityBound::Zero()},
                  /* 7 */ {jp78, ProbabilityBound::Zero()},
              });

  auto s =
      dependency_closure->SegmentsThatInteractWith({Node::Unicode(0x6717)});
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, (SegmentSet{0, 4, 5}));

  s = dependency_closure->SegmentsThatInteractWith(
      {Node::Glyph(6 /* U+6717 UVS alt */)});
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, (SegmentSet{0, 4, 5}));

  s = dependency_closure->SegmentsThatInteractWith(
      {Node::Unicode(0x798f), Node::Feature(HB_TAG('j', 'p', '7', '8'))});
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, (SegmentSet{2, 5, 7}));
}

TEST_F(DependencyClosureTest, SegmentsThatInteractWith_SubsetDef) {
  SubsetDefinition aalt;
  aalt.feature_tags.insert(HB_TAG('a', 'a', 'l', 't'));

  Reconfigure(noto_sans_jp_vf.get(), {},
              {
                  /* 0 */ {{0x6406}, ProbabilityBound::Zero()},
                  /* 1 */ {{0x640f}, ProbabilityBound::Zero()},
                  /* 2 */ {aalt, ProbabilityBound::Zero()},
              });

  SubsetDefinition query_def;
  query_def.codepoints = {0x6406};
  query_def.feature_tags = {HB_TAG('a', 'a', 'l', 't')};

  auto s = dependency_closure->SegmentsThatInteractWith(query_def);
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, (SegmentSet{0, 2}));
}

TEST_F(DependencyClosureTest, SegmentsThatInteractWith) {
  Reconfigure(WithDefaultFeatures(), {
                                         {{'a'}, ProbabilityBound::Zero()},
                                         {{'b'}, ProbabilityBound::Zero()},
                                         {{'f'}, ProbabilityBound::Zero()},
                                         {{'i'}, ProbabilityBound::Zero()},
                                     });

  auto s = dependency_closure->SegmentsThatInteractWith(GlyphSet{69 /* a */});
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, (SegmentSet{0}));

  s = dependency_closure->SegmentsThatInteractWith(GlyphSet{70 /* b */});
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, (SegmentSet{1}));

  s = dependency_closure->SegmentsThatInteractWith(GlyphSet{69, 70});
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, (SegmentSet{0, 1}));

  s = dependency_closure->SegmentsThatInteractWith(GlyphSet{74 /* f */});
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, (SegmentSet{2}));

  s = dependency_closure->SegmentsThatInteractWith(GlyphSet{444});
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, (SegmentSet{2, 3}));
}

TEST_F(DependencyClosureTest, SegmentsThatInteractWith_Context) {
  Reconfigure(WithDefaultFeatures(),
              {
                  /* 0 */ {{'x'}, ProbabilityBound::Zero()},
                  /* 1 */ {{'q'}, ProbabilityBound::Zero()},
                  /* 2 */ {{'i'}, ProbabilityBound::Zero()},
                  /* 3 */ {{0x300 /* gravecomb */}, ProbabilityBound::Zero()},
              });

  ASSERT_EQ(segmentation_info->FullClosure(),
            (GlyphSet{0, 77, 85, 92, 141, 168, 609}));

  auto s = dependency_closure->SegmentsThatInteractWith(
      GlyphSet{609 /* dotlessi */});
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, (SegmentSet{2, 3}));
}

TEST_F(DependencyClosureTest, SegmentsThatInteractWith_FeaturesInContext) {
  SubsetDefinition ccmp;
  ccmp.feature_tags = {HB_TAG('c', 'c', 'm', 'p')};
  Reconfigure({}, {
                      {{'x'}, ProbabilityBound::Zero()},
                      {{'q'}, ProbabilityBound::Zero()},
                      {{'i'}, ProbabilityBound::Zero()},
                      {{0x300 /* gravecomb */}, ProbabilityBound::Zero()},
                      {{ccmp}, ProbabilityBound::Zero()},
                  });

  ASSERT_EQ(segmentation_info->FullClosure(),
            (GlyphSet{0, 77, 85, 92, 141, 168, 609}));

  auto s = dependency_closure->SegmentsThatInteractWith(
      GlyphSet{609 /* dotlessi */});
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, (SegmentSet{2, 3, 4}));
}

TEST_F(DependencyClosureTest, SegmentsThatInteractWith_InitFontContext) {
  Reconfigure(WithDefaultFeatures({'i'}),
              {
                  {{'x'}, ProbabilityBound::Zero()},
                  {{'q'}, ProbabilityBound::Zero()},
                  {{0x300 /* gravecomb */}, ProbabilityBound::Zero()},
              });

  ASSERT_EQ(segmentation_info->FullClosure(),
            (GlyphSet{0, 77, 85, 92, 141, 168, 609}));

  auto s = dependency_closure->SegmentsThatInteractWith(
      GlyphSet{609 /* dotlessi */});
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, (SegmentSet{2}));
}

TEST_F(DependencyClosureTest,
       SegmentsThatInteractWith_FeaturesAndInitFontContext) {
  SubsetDefinition ccmp;
  ccmp.feature_tags = {HB_TAG('c', 'c', 'm', 'p')};
  Reconfigure({'i'}, {
                         {{'x'}, ProbabilityBound::Zero()},
                         {{'q'}, ProbabilityBound::Zero()},
                         {{0x300 /* gravecomb */}, ProbabilityBound::Zero()},
                         {ccmp, ProbabilityBound::Zero()},
                     });

  ASSERT_EQ(segmentation_info->FullClosure(),
            (GlyphSet{0, 77, 85, 92, 141, 168, 609}));

  auto s = dependency_closure->SegmentsThatInteractWith(
      GlyphSet{609 /* dotlessi */});
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, (SegmentSet{2, 3}));
}

TEST_F(DependencyClosureTest, SegmentsThatInteractWith_LayoutFeatures) {
  SubsetDefinition aalt;
  aalt.feature_tags.insert(HB_TAG('a', 'a', 'l', 't'));
  SubsetDefinition jp78;
  jp78.feature_tags.insert(HB_TAG('j', 'p', '7', '8'));
  Reconfigure(noto_sans_jp_vf.get(), {},
              {
                  /* 0 */ {{0x6406}, ProbabilityBound::Zero()},
                  /* 1 */ {{0x640f}, ProbabilityBound::Zero()},
                  /* 2 */ {aalt, ProbabilityBound::Zero()},
                  /* 3 */ {jp78, ProbabilityBound::Zero()},
              });

  auto s =
      dependency_closure->SegmentsThatInteractWith(GlyphSet{1 /* U+6406 */});
  ASSERT_TRUE(s.ok()) << s.status();
  // g1 is only mapped from cmap, so only segment s0 interacts with it.
  ASSERT_EQ(*s, (SegmentSet{0}));

  s = dependency_closure->SegmentsThatInteractWith(
      GlyphSet{9 /* single sub of U+6406 */});
  ASSERT_TRUE(s.ok()) << s.status();
  // The key result here is that for s1 isn't considered to be in the
  // interaction group despite both interacting with s2 and s3 (likewise for
  // s0).
  ASSERT_EQ(*s, (SegmentSet{0, 2, 3}));
}

TEST_F(DependencyClosureTest, InitFontFeatureConjunction) {
  SubsetDefinition init;
  init.codepoints = {0x30}; /* zero */
  init.gids = {20};

  SubsetDefinition s0{8320};
  SubsetDefinition s1{};
  s1.feature_tags.insert(HB_TAG('s', 'u', 'b', 's'));

  Reconfigure(roboto_vf.get(), init,
              {
                  /* 0 */ {s0, ProbabilityBound::Zero()},
                  /* 1 */ {s1, ProbabilityBound::Zero()},
              });

  Status s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;

  s = CompareAnalysis({1});
  ASSERT_TRUE(s.ok()) << s;

  s = CompareAnalysis({0, 1});
  ASSERT_TRUE(s.ok()) << s;
}

}  // namespace ift::encoder

// TODO(garretrieger): missing tests
// - CFF seac test.
// - COLRv1 font tests.

// TODO(garretrieger) more tests (once functionality is available):
// - partial invalidation tests including w/ "accurate" indices.
// - Test case with a feature segment + otherwise disjunctive GSUB (eg. smcp
// single sub)
// - case where init font makes a conjunctive thing exclusive (eg. UVS and/or
// liga).
// - liga
// - UVS, including when mixed with init font.
