#include "ift/encoder/dependency_closure.h"
#include <memory>
#include <vector>

#include "common/int_set.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/init_subset_defaults.h"
#include "ift/encoder/segment.h"
#include "common/font_data.h"
#include "gtest/gtest.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"
#include "ift/freq/probability_bound.h"
#include "util/common.pb.h"
#include "ift/encoder/requested_segmentation_information.h"

using absl::Status;
using common::hb_face_unique_ptr;
using common::CodepointSet;
using common::SegmentSet;
using common::FontData;
using common::GlyphSet;
using ift::freq::ProbabilityBound;

namespace ift::encoder {

class DependencyClosureTest : public ::testing::Test {
 protected:
  DependencyClosureTest() :
    face(from_file("common/testdata/Roboto-Regular.ttf")),
    double_nested_face(from_file("common/testdata/double-nested-components.ttf")),
    noto_sans_jp(from_file("common/testdata/NotoSansJP-Regular.ttf")),
    closure_cache(face.get()),
    segmentation_info(segments, WithDefaultFeatures(), closure_cache, PATCH),
    dependency_closure(*DependencyClosure::Create(&segmentation_info, face.get()))
  {}

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

  void Reconfigure(SubsetDefinition new_init, std::vector<Segment> new_segments) {
    segmentation_info = RequestedSegmentationInformation(new_segments, new_init, closure_cache, PATCH);
    dependency_closure = *DependencyClosure::Create(&segmentation_info, face.get());
  }

  void Reconfigure(hb_face_t* new_face, SubsetDefinition new_init, std::vector<Segment> new_segments) {
    closure_cache = GlyphClosureCache(new_face);
    segmentation_info = RequestedSegmentationInformation(new_segments, new_init, closure_cache, PATCH);
    dependency_closure = *DependencyClosure::Create(&segmentation_info, new_face);
  }

  Status RejectedAnalysis(segment_index_t segment) {
    GlyphSet and_gids;
    GlyphSet or_gids;
    GlyphSet exclusive_gids;

    if (TRY(dependency_closure->AnalyzeSegment({segment}, and_gids, or_gids, exclusive_gids)) == DependencyClosure::ACCURATE) {
      return absl::InternalError("Dependency closure analysis should have been rejected.");
    }
    return absl::OkStatus();
  }

  Status CompareAnalysis(SegmentSet segments) {
    GlyphSet and_gids;
    GlyphSet or_gids;
    GlyphSet exclusive_gids;

    if (TRY(dependency_closure->AnalyzeSegment(segments, and_gids, or_gids, exclusive_gids)) != DependencyClosure::ACCURATE) {
      return absl::InternalError("Dependency closure analysis rejected unexpectedly.");
    }

    GlyphSet expected_and_gids;
    GlyphSet expected_or_gids;
    GlyphSet expected_exclusive_gids;
    TRYV(closure_cache.AnalyzeSegment(segmentation_info, segments, expected_and_gids, expected_or_gids, expected_exclusive_gids));

    std::string message;
    bool success = true;
    if (!CompareGids("AND", and_gids, expected_and_gids, message)) {
      success = false;
    }
    if (!CompareGids("OR", or_gids, expected_or_gids, message)) {
      success = false;
    }
    if (!CompareGids("EXCLUSIVE", exclusive_gids, expected_exclusive_gids, message)) {
      success = false;
    }

    if (!success) {
      return absl::InvalidArgumentError(message);
    }
    return absl::OkStatus();
  }

  bool CompareGids(absl::string_view name, const GlyphSet& actual, const GlyphSet& expected, std::string& message) {
    std::string op = " == ";
    bool result = true;
    if (actual != expected) {
      op = " != ";
      result = false;
    }

    absl::StrAppend(&message,
      name, " glyphs: actual ", actual.ToString(), op, "expected ", expected.ToString(), "\n");
    return result;
  }

  hb_face_unique_ptr face;
  hb_face_unique_ptr double_nested_face;
  hb_face_unique_ptr noto_sans_jp;
  GlyphClosureCache closure_cache;
  RequestedSegmentationInformation segmentation_info;
  std::unique_ptr<DependencyClosure> dependency_closure;
};

TEST_F(DependencyClosureTest, AddsToSets) {
    GlyphSet and_gids {101};
    GlyphSet or_gids {102};
    GlyphSet exclusive_gids {103};

    auto r = dependency_closure->AnalyzeSegment({0}, and_gids, or_gids, exclusive_gids);
    ASSERT_TRUE(r.ok()) << r.status();
    ASSERT_EQ(*r, DependencyClosure::ACCURATE);

    ASSERT_EQ(and_gids, (GlyphSet {101}));
    ASSERT_EQ(or_gids, (GlyphSet {102}));
    ASSERT_EQ(exclusive_gids, (GlyphSet {69, 103}));
}

TEST_F(DependencyClosureTest, Exclusive) {
  Status s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({3});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({0, 3});
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, LigaSatisfied) {
  // As a special case if a segment fully satisifies it's liga requirements,
  // then we can accurately analyze it.

  // f or i on it's own is rejected, since liga is unsatisfied.
  Status s = RejectedAnalysis(1);
  ASSERT_TRUE(s.ok()) << s;
  s = RejectedAnalysis(2);
  ASSERT_TRUE(s.ok()) << s;

  // when f and i are both in a segment then we can analyze it.
  s = CompareAnalysis({1, 2});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({1, 2, 3});
  ASSERT_TRUE(s.ok()) << s;

  // One half of the liga is in the init font
  Reconfigure(face.get(), WithDefaultFeatures({'f'}), {
    {{'a'}, ProbabilityBound::Zero()},
    {{'i'}, ProbabilityBound::Zero()},
  });
  s = CompareAnalysis({1});
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, UnicodeToGid_ExcludesInitFont) {
  // 0x7528 and 0x2F64 share the same glyph
  Reconfigure(noto_sans_jp.get(), {0x7528}, {
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
  Reconfigure(double_nested_face.get(), {}, {
    {{'a'}, ProbabilityBound::Zero()},
    {{'b'}, ProbabilityBound::Zero()},
  });
  Status s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({1});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({0, 1});
  ASSERT_TRUE(s.ok()) << s;

  Reconfigure(double_nested_face.get(), {'a'}, {
    {{'b'}, ProbabilityBound::Zero()},
  });
  s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;

  Reconfigure(double_nested_face.get(), {'b'}, {
    {{'a'}, ProbabilityBound::Zero()},
  });
  s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, CodepointNotInFont) {
  Reconfigure(double_nested_face.get(), {}, {
    {{'A'}, ProbabilityBound::Zero()},
  });
  Status s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, SingleSubst) {
  SubsetDefinition c2sc;
  c2sc.feature_tags.insert(HB_TAG('c', '2', 's', 'c'));

  Reconfigure(face.get(), {}, {
    {{'a'}, ProbabilityBound::Zero()},
    {{'b'}, ProbabilityBound::Zero()},
    {{'A'}, ProbabilityBound::Zero()},
    {{0x1FC /* AEacute */}, ProbabilityBound::Zero()},
    {c2sc, ProbabilityBound::Zero()},
    // TODO XXX add composite glyph AE instead of B?
  });

  Status s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;

   // s2sc not in init font which A passes through.
  s = RejectedAnalysis(2);
  ASSERT_TRUE(s.ok()) << s;
  s = RejectedAnalysis(3);
  ASSERT_TRUE(s.ok()) << s;
  s = RejectedAnalysis(4);
  ASSERT_TRUE(s.ok()) << s;

  // With c2sc in the init font, we can now analyze the single subst's
  Reconfigure(face.get(), c2sc, {
    {{'a'}, ProbabilityBound::Zero()},
    {{'b'}, ProbabilityBound::Zero()},
    {{'A'}, ProbabilityBound::Zero()},
    {{0x1FC /* AEacute */}, ProbabilityBound::Zero()},
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

TEST_F(DependencyClosureTest, Rejected) {
  Status s = RejectedAnalysis(1);
  ASSERT_TRUE(s.ok()) << s;
  s = RejectedAnalysis(2);
  ASSERT_TRUE(s.ok()) << s;
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
  Reconfigure(WithDefaultFeatures({'i'}), {
    {{0x300 /* gravecomb */}, ProbabilityBound::Zero()},
    {{0x485}, ProbabilityBound::Zero()},
  });

  // Gravecomb doesn't pass through any contextual dependencies, but
  // it shows up as a context glyph for the init font subset.
  // Thus it should be rejected.
  Status s = RejectedAnalysis(0);
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, Rejected_Features) {
  SubsetDefinition features;
  features.feature_tags.insert(HB_TAG('a', 'b', 'c', 'd'));
  Reconfigure({}, {
    {{'a'}, ProbabilityBound::Zero()},
    {features, ProbabilityBound::Zero()},
  });

  Status s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;

  s = RejectedAnalysis(1);
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, Rejected_UVS) {
  Reconfigure(noto_sans_jp.get(), {}, {
    {{0x4fae}, ProbabilityBound::Zero()},
    {{0xfe00}, ProbabilityBound::Zero()},
  });

  // UVS isn't supported yet.
  Status s = RejectedAnalysis(0);
  ASSERT_TRUE(s.ok()) << s;

  s = RejectedAnalysis(1);
  ASSERT_TRUE(s.ok()) << s;

  Reconfigure(noto_sans_jp.get(), {0x4fae}, {
    {{0xfe00}, ProbabilityBound::Zero()},
  });
  s = RejectedAnalysis(0);
  ASSERT_TRUE(s.ok()) << s;

  Reconfigure(noto_sans_jp.get(), {0xfe00}, {
    {{0x4fae}, ProbabilityBound::Zero()},
  });
  s = RejectedAnalysis(0);
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

  segmentation_info.ReassignInitSubset(closure_cache, {'a'});
  s = dependency_closure->SegmentsChanged(true, SegmentSet::all());
  ASSERT_TRUE(s.ok()) << s;

  s = CompareAnalysis({0});
  ASSERT_TRUE(s.ok()) << s;
  s = CompareAnalysis({1});
  ASSERT_TRUE(s.ok()) << s;
}

TEST_F(DependencyClosureTest, SegmentsThatInteractWith) {
  Reconfigure(WithDefaultFeatures(), {
    {{'a'}, ProbabilityBound::Zero()},
    {{'b'}, ProbabilityBound::Zero()},
    {{'f'}, ProbabilityBound::Zero()},
    {{'i'}, ProbabilityBound::Zero()},
  });

  auto s = dependency_closure->SegmentsThatInteractWith({69 /* a */});
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, (SegmentSet {0}));

  s = dependency_closure->SegmentsThatInteractWith({70 /* b */});
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, (SegmentSet {1}));

  s = dependency_closure->SegmentsThatInteractWith({69, 70});
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, (SegmentSet {0, 1}));

  s = dependency_closure->SegmentsThatInteractWith({74 /* f */});
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, (SegmentSet {2}));

  s = dependency_closure->SegmentsThatInteractWith({444});
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, (SegmentSet {2, 3}));
}

TEST_F(DependencyClosureTest, SegmentsThatInteractWith_Context) {
  Reconfigure(WithDefaultFeatures(), {
    {{'x'}, ProbabilityBound::Zero()},
    {{'q'}, ProbabilityBound::Zero()},
    {{'i'}, ProbabilityBound::Zero()},
    {{0x300 /* gravecomb */}, ProbabilityBound::Zero()},
  });

  ASSERT_EQ(segmentation_info.FullClosure(), (GlyphSet {0, 77, 85, 92, 141, 168, 609}));

  auto s = dependency_closure->SegmentsThatInteractWith({609 /* dotlessi */});
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, (SegmentSet {2, 3}));
}

TEST_F(DependencyClosureTest, SegmentsThatInteractWith_InitFontContext) {
  Reconfigure(WithDefaultFeatures({'i'}), {
    {{'x'}, ProbabilityBound::Zero()},
    {{'q'}, ProbabilityBound::Zero()},
    {{0x300 /* gravecomb */}, ProbabilityBound::Zero()},
  });

  ASSERT_EQ(segmentation_info.FullClosure(), (GlyphSet {0, 77, 85, 92, 141, 168, 609}));

  auto s = dependency_closure->SegmentsThatInteractWith({609 /* dotlessi */});
  ASSERT_TRUE(s.ok()) << s.status();
  ASSERT_EQ(*s, (SegmentSet {2}));
}

// TODO XXXX SegmentsThatInteract with features involved.

}  // namespace ift::encoder

// TODO XXXX CFF seac test.
// TODO XXXX allow liga if context is satisfied, reject otherwise.
// TODO XXXX contextual always rejected (since recursion is hard to reason about).

// TODO(garretrieger) more tests (once functionality is available):
// - Test case exposing the current exclusive check failure (exclusive glyph reachable via intermediate).
// - Test case with a feature segment + otherwise disjunctive GSUB (eg. smcp single sub)
// - support for disjunctive GSUB types.
// - case where init font makes a conjunctive thing exclusive (eg. UVS and/or liga).
// - liga
// - UVS, including when mixed with init font.
// - COLRv1 font tests.
