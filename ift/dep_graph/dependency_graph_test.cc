#include "ift/dep_graph/dependency_graph.h"
#include <memory>

#include "common/font_data.h"
#include "common/int_set.h"
#include "ift/dep_graph/traversal.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"
#include "ift/freq/probability_bound.h"

#include "gtest/gtest.h"

using absl::flat_hash_map;
using common::hb_face_unique_ptr;
using common::CodepointSet;
using common::FontData;
using common::GlyphSet;
using ift::encoder::glyph_id_t;
using ift::encoder::GlyphClosureCache;
using ift::encoder::RequestedSegmentationInformation;
using ift::encoder::Segment;
using ift::encoder::SubsetDefinition;
using ift::freq::ProbabilityBound;

namespace ift::dep_graph {

class DependencyGraphTest : public ::testing::Test {
 protected:
  DependencyGraphTest() :
  face(from_file("common/testdata/Roboto-Regular.ttf")),
  closure_cache(face.get()),
  segmentation_info(*RequestedSegmentationInformation::Create(segments, WithDefaultFeatures({}), closure_cache, PATCH)),
  graph(*DependencyGraph::Create(segmentation_info.get(), face.get()))
  {}

  static hb_face_unique_ptr from_file(const char* filename) {
    hb_blob_t* blob = hb_blob_create_from_file_or_fail(filename);
    if (!blob) {
      assert(false);
    }
    FontData result(blob);
    hb_blob_destroy(blob);
    return result.face();
  }

  static SubsetDefinition WithDefaultFeatures(SubsetDefinition def) {
        AddInitSubsetDefaults(def);
    return def;
  }

  public:
  void Reconfigure(SubsetDefinition new_init, std::vector<Segment> new_segments) {
    segmentation_info = *RequestedSegmentationInformation::Create(new_segments, new_init, closure_cache, PATCH);
    graph = *DependencyGraph::Create(segmentation_info.get(), face.get());
  }

  private:
  std::vector<Segment> segments = {
    /* 0 */ {{'a'}, ProbabilityBound::Zero()},
    /* 1 */ {{'f'}, ProbabilityBound::Zero()},
    /* 2 */ {{'i'}, ProbabilityBound::Zero()},
  };

  hb_face_unique_ptr face;
  GlyphClosureCache closure_cache;

  public:
  std::unique_ptr<RequestedSegmentationInformation> segmentation_info;
  DependencyGraph graph;
};

TEST_F(DependencyGraphTest, InitFontTraversal) {
  Reconfigure(WithDefaultFeatures({'f', 'i'}), {
    {{'a'}, ProbabilityBound::Zero()},
    {{'b'}, ProbabilityBound::Zero()},
  });

  GlyphSet all_g = GlyphSet::all();
  CodepointSet all_u = CodepointSet::all();
  auto r = graph.ClosureTraversal({Node::InitFont()}, &all_g, &all_u);
  ASSERT_TRUE(r.ok()) << r.status();
  const auto& traversal = *r;

  ASSERT_TRUE(traversal.ReachedGlyphs().contains(74 /* f */));
  ASSERT_TRUE(traversal.ReachedGlyphs().contains(77 /* i */));
  ASSERT_TRUE(traversal.ReachedGlyphs().contains(444 /* fi */));
  ASSERT_TRUE(traversal.ReachedGlyphs().contains(446 /* fi */));

  r = graph.ClosureTraversal({Node::InitFont()}, &segmentation_info->FullClosure(), &segmentation_info->FullDefinition().codepoints);
  ASSERT_TRUE(r.ok()) << r.status();
  const auto& traversal_scoped = *r;
  ASSERT_EQ(traversal_scoped.ReachedGlyphs(), (GlyphSet {
    74 /* f */,
    77 /* i */,
    444 /* fi */,
    446 /* ffi */,
  }));
}

TEST_F(DependencyGraphTest, ContextGlyphs) {
  SubsetDefinition init = WithDefaultFeatures({});
  init.feature_tags.insert(HB_TAG('f', 'r', 'a', 'c'));

  Reconfigure(init, {
    {{'i'}, ProbabilityBound::Zero()},
    {{0x300 /* gravecomb */}, ProbabilityBound::Zero()},

    {{'1'}, ProbabilityBound::Zero()},
    {{0x2044 /* fraction */}, ProbabilityBound::Zero()},
  });

  auto r = graph.ClosureTraversal({
    Node::Segment(0),
    Node::Segment(1),
    Node::Segment(2),
    Node::Segment(3),
  });
  ASSERT_TRUE(r.ok()) << r.status();
  const auto& traversal = *r;

  ASSERT_EQ(segmentation_info->FullClosure(), (GlyphSet {
    0,
    21,  /* one */
    77,  /* i */
    122, /* superscript one */
    141, /* dotlessi */
    168, /* gravecomb */
    404, /* fraction */
    454, /* one for fraction */
    609  /* dotlessi wrapper */
  }));

  ASSERT_EQ(traversal.ContextGlyphs(), (GlyphSet {
    168, /* gravecomb */
    404, /* fraction */
    454, /* one for fraction */
  }));

  ASSERT_EQ(traversal.ContextPerGlyph(), (flat_hash_map<glyph_id_t, GlyphSet> {
    {454, { 404, 454 }},
    {609, { 168 }},
  }));
}

TEST_F(DependencyGraphTest, ContextGlyphTraversal) {
  Reconfigure(WithDefaultFeatures({'i'}), {
    {{0x300 /* gravecomb */}, ProbabilityBound::Zero()},
  });

  auto r = graph.ClosureTraversal({
    Node::Segment(0),
  });
  ASSERT_TRUE(r.ok()) << r.status();
  const auto& traversal = *r;

  // Gravecomb interacts with 'i' as only a context glyph, so it's
  // own traversal is just it's self.
  ASSERT_EQ(traversal.ReachedGlyphs(), (GlyphSet {168 /* gravecomb */}));
  ASSERT_EQ(traversal.ContextGlyphs(), (GlyphSet {}));
}

TEST_F(DependencyGraphTest, ClosurePhasesEnforced) {
  Reconfigure(WithDefaultFeatures({}), {
    {{0x133 /* ij */}, ProbabilityBound::Zero()},
    {{0x300 /* gravecomb */}, ProbabilityBound::Zero()},
  });

  auto r = graph.ClosureTraversal({
    Node::Segment(0),
    Node::Segment(1),
  });
  ASSERT_TRUE(r.ok()) << r.status();
  const auto& traversal = *r;

  // gravecomb interacts with 'i', but that interaction isn't reachable since 'i'
  // only becomes available after GSUB traversal is finished in the later glyf phase.
  ASSERT_EQ(traversal.ReachedGlyphs(), (GlyphSet {
    168 /* gravecomb */,
    77 /* i */,
    78 /* j */,
    740 /* ij */,
  }));
  ASSERT_EQ(traversal.ContextGlyphs(), (GlyphSet {}));
}

// TODO(garretrieger):
// - basic math, CFF, and COLR tests.

// TODO(garretrieger) we currently only have a few specialized tests, relyng primarily on DepedencyClosureTest
// for coverage of DepedencyGraph functionality. We should add some basic tests here that test DepedencyGraph
// core features in isolation.

}  // namespace ift::dep_graph