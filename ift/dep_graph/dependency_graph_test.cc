#include "ift/dep_graph/dependency_graph.h"

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "ift/common/bazel_data_file_resolver.h"
#include "ift/common/data_file_resolver.h"
#include "ift/common/font_data.h"
#include "ift/common/font_helper.h"
#include "ift/common/int_set.h"
#include "ift/dep_graph/traversal.h"
#include "ift/encoder/glyph_closure_cache.h"
#include "ift/encoder/requested_segmentation_information.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"
#include "ift/freq/probability_bound.h"

using ift::config::PATCH;

using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using ift::common::BazelDataFileResolver;
using ift::common::CodepointSet;
using ift::common::DataFileResolver;
using ift::common::FontData;
using ift::common::FontHelper;
using ift::common::GlyphSet;
using ift::common::hb_face_unique_ptr;
using ift::encoder::glyph_id_t;
using ift::encoder::GlyphClosureCache;
using ift::encoder::RequestedSegmentationInformation;
using ift::encoder::Segment;
using ift::encoder::SubsetDefinition;
using ift::freq::ProbabilityBound;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::UnorderedElementsAre;

#include "ift/common/test_font_loader.h"

namespace ift::dep_graph {

class DependencyGraphTest : public ::testing::Test {
 protected:
  DependencyGraphTest()
      : loader(ift::common::TestFontLoader::Default().value()),
        face(
            loader->LoadFace("ift/common/testdata/Roboto-Regular.ttf").value()),
        resolver(*BazelDataFileResolver::CreateForTest()),
        closure_cache(
            std::move(*GlyphClosureCache::Create(face.get(), *resolver))),
        noto_sans_jp(
            loader->LoadFace("ift/common/testdata/NotoSansJP-Regular.ttf")
                .value()),
        noto_sans_jp_vf(
            loader->LoadFace("ift/common/testdata/NotoSansJP-VF.cmap14.ttf")
                .value()),
        noto_sans_kr(
            loader->LoadFace("ift/common/testdata/NotoSansKR[wght].subset.ttf")
                .value()),
        segmentation_info(*RequestedSegmentationInformation::Create(
            segments, WithDefaultFeatures({}), *closure_cache, PATCH)),
        graph(*DependencyGraph::Create(segmentation_info.get(), face.get(),
                                       *resolver)) {}

  static SubsetDefinition WithDefaultFeatures(SubsetDefinition def) {
    AddInitSubsetDefaults(def);
    return def;
  }

 public:
  void Reconfigure(SubsetDefinition new_init,
                   std::vector<Segment> new_segments) {
    segmentation_info = *RequestedSegmentationInformation::Create(
        new_segments, new_init, *closure_cache, PATCH);
    graph = *DependencyGraph::Create(segmentation_info.get(), face.get(),
                                     *resolver);
  }

  void Reconfigure(hb_face_t* new_face, SubsetDefinition new_init,
                   std::vector<Segment> new_segments) {
    closure_cache = std::move(*GlyphClosureCache::Create(new_face, *resolver));
    segmentation_info = *RequestedSegmentationInformation::Create(
        new_segments, new_init, *closure_cache, PATCH);
    graph =
        *DependencyGraph::Create(segmentation_info.get(), new_face, *resolver);
  }

 private:
  std::vector<Segment> segments = {
      /* 0 */ {{'a'}, ProbabilityBound::Zero()},
      /* 1 */ {{'f'}, ProbabilityBound::Zero()},
      /* 2 */ {{'i'}, ProbabilityBound::Zero()},
  };

 protected:
  std::unique_ptr<ift::common::TestFontLoader> loader;
  hb_face_unique_ptr face;
  std::shared_ptr<DataFileResolver> resolver;
  std::unique_ptr<GlyphClosureCache> closure_cache;

 public:
  hb_face_unique_ptr noto_sans_jp;
  hb_face_unique_ptr noto_sans_jp_vf;
  hb_face_unique_ptr noto_sans_kr;
  std::unique_ptr<RequestedSegmentationInformation> segmentation_info;
  DependencyGraph graph;
};

TEST_F(DependencyGraphTest, InitFontTraversal) {
  Reconfigure(WithDefaultFeatures({'f', 'i'}),
              {
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

  r = graph.ClosureTraversal({Node::InitFont()},
                             &segmentation_info->FullClosure(),
                             &segmentation_info->FullDefinition().codepoints);
  ASSERT_TRUE(r.ok()) << r.status();
  const auto& traversal_scoped = *r;
  ASSERT_EQ(traversal_scoped.ReachedGlyphs(), (GlyphSet{
                                                  74 /* f */,
                                                  77 /* i */,
                                                  444 /* fi */,
                                                  446 /* ffi */,
                                              }));
}

TEST_F(DependencyGraphTest, ClosureTraversal_RecordsInputNodes) {
  GlyphSet all_g = GlyphSet::all();
  CodepointSet all_u = CodepointSet::all();

  Node glyph_node = Node::Glyph(74);  // 'f'
  Node unicode_node = Node::Unicode('a');
  Node feature_node = Node::Feature(HB_TAG('l', 'i', 'g', 'a'));

  auto r = graph.ClosureTraversal({glyph_node, unicode_node, feature_node},
                                  &all_g, &all_u);
  ASSERT_TRUE(r.ok()) << r.status();
  const auto& traversal = *r;

  EXPECT_TRUE(traversal.ReachedGlyphs().contains(74));
  EXPECT_TRUE(traversal.ReachedCodepoints().contains('a'));
  EXPECT_TRUE(
      traversal.ReachedLayoutFeatures().contains(HB_TAG('l', 'i', 'g', 'a')));
}

TEST_F(DependencyGraphTest, ClosureTraversal_FiltersInputNodes) {
  GlyphSet empty_g;
  CodepointSet empty_u;

  Node glyph_node = Node::Glyph(74);  // 'f'
  Node unicode_node = Node::Unicode('a');

  auto r =
      graph.ClosureTraversal({glyph_node, unicode_node}, &empty_g, &empty_u);
  ASSERT_TRUE(r.ok()) << r.status();
  const auto& traversal = *r;

  EXPECT_FALSE(traversal.ReachedGlyphs().contains(74));
  EXPECT_FALSE(traversal.ReachedCodepoints().contains('a'));
}

TEST_F(DependencyGraphTest, ClosureTraversal_FiltersNotInFont) {
  Reconfigure(WithDefaultFeatures({}),
              {
                  {{0xD4DB},
                   ProbabilityBound::Zero()},  // korean codepoint not in Roboto
              });

  auto r = graph.ClosureTraversal({Node::Unicode(0xD4DB)});
  ASSERT_TRUE(r.ok()) << r.status();
  // Decompositions should not be present
  EXPECT_EQ(r->ReachedCodepoints(), (CodepointSet{}));
}

TEST_F(DependencyGraphTest, UnicodeCompDecomp_Traversal) {
  Reconfigure(WithDefaultFeatures({}),
              {
                  {{0xe1}, ProbabilityBound::Zero()},   // á
                  {{0x61}, ProbabilityBound::Zero()},   // a
                  {{0x301}, ProbabilityBound::Zero()},  // combining acute
              });

  auto r = graph.ClosureTraversal({Node::Unicode(0xe1)});
  ASSERT_TRUE(r.ok()) << r.status();
  EXPECT_EQ(r->ReachedCodepoints(), (CodepointSet{0xe1, 0x61, 0x301}));

  r = graph.ClosureTraversal({Node::Unicode(0x61), Node::Unicode(0x301)});
  ASSERT_TRUE(r.ok()) << r.status();
  EXPECT_EQ(r->ReachedCodepoints(), (CodepointSet{0xe1, 0x61, 0x301}));
}

TEST_F(DependencyGraphTest, UnicodeCompDecomp_IncludesNonSegmentCodepoints) {
  Reconfigure(WithDefaultFeatures({}),
              {
                  {{0xe1}, ProbabilityBound::Zero()},  // á
                  {{0x61}, ProbabilityBound::Zero()},  // a
              });

  // Even though 0x301 is not in a segment's definition closure traversal will
  // still pass through it.
  auto r = graph.ClosureTraversal({Node::Unicode(0xe1)});
  ASSERT_TRUE(r.ok()) << r.status();
  EXPECT_EQ(r->ReachedCodepoints(), (CodepointSet{0xe1, 0x61, 0x301}));

  // Because 0x301 isn't in scope, the composition and decomposition edges can't
  // be traversed.
  r = graph.ClosureTraversal({Node::Unicode(0x61)});
  ASSERT_TRUE(r.ok()) << r.status();
  EXPECT_EQ(r->ReachedCodepoints(), (CodepointSet{0x61}));
}

TEST_F(DependencyGraphTest, UnicodeCompDecomp_CompositionExclusion) {
  // U+2126 (OHM SIGN) decomposes to U+03A9 (GREEK CAPITAL LETTER OMEGA)
  // and is in the Full_Composition_Exclusion list.
  Reconfigure(WithDefaultFeatures({}), {
                                           {{0x2126}, ProbabilityBound::Zero()},
                                           {{0x3a9}, ProbabilityBound::Zero()},
                                       });

  // Only composition is excluded so decomposition should still work.
  auto r = graph.ClosureTraversal({Node::Unicode(0x2126)});
  ASSERT_TRUE(r.ok()) << r.status();
  EXPECT_EQ(r->ReachedCodepoints(), (CodepointSet{0x3a9, 0x2126}));

  // The reverse should not work
  r = graph.ClosureTraversal({Node::Unicode(0x03A9)});
  ASSERT_TRUE(r.ok()) << r.status();
  EXPECT_EQ(r->ReachedCodepoints(), (CodepointSet{0x3a9}));
}

TEST_F(DependencyGraphTest, UnicodeCompDecomp_HangulExclusionTraversal) {
  // U+0xD4DB decomposes to U+1111, U+1171, U+11B6
  // (from:
  // https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-3/#G24646)
  Reconfigure(noto_sans_kr.get(), WithDefaultFeatures({}),
              {
                  {{0xD4DB}, ProbabilityBound::Zero()},
                  {{0x1111}, ProbabilityBound::Zero()},
                  {{0x1171}, ProbabilityBound::Zero()},
                  {{0x11B6}, ProbabilityBound::Zero()},
              });

  auto r = graph.ClosureTraversal({Node::Unicode(0xD4DB)});
  ASSERT_TRUE(r.ok()) << r.status();
  // Decompositions should not be present
  EXPECT_EQ(r->ReachedCodepoints(), (CodepointSet{0xD4DB}));

  r = graph.ClosureTraversal(
      {Node::Unicode(0x1111), Node::Unicode(0x1171), Node::Unicode(0x11B6)});
  ASSERT_TRUE(r.ok()) << r.status();
  // Composition should not be present
  EXPECT_EQ(r->ReachedCodepoints(), (CodepointSet{0x1111, 0x1171, 0x11B6}));
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

  ASSERT_EQ(segmentation_info->FullClosure(), (GlyphSet{
                                                  0, 21, /* one */
                                                  68,    /* grave */
                                                  77,    /* i */
                                                  122,   /* superscript one */
                                                  141,   /* dotlessi */
                                                  168,   /* gravecomb */
                                                  404,   /* fraction */
                                                  454,   /* one for fraction */
                                                  609,   /* dotlessi wrapper */
                                                  678    /* iacute */
                                              }));

  ASSERT_EQ(traversal.ContextGlyphs(), (GlyphSet{
                                           168, /* gravecomb */
                                           404, /* fraction */
                                           454, /* one for fraction */
                                       }));

  ASSERT_EQ(traversal.ContextPerGlyph(), (flat_hash_map<glyph_id_t, GlyphSet>{
                                             {454, {404, 454}},
                                             {609, {168}},
                                         }));
}

TEST_F(DependencyGraphTest, ContextGlyphTraversal) {
  Reconfigure(WithDefaultFeatures({'i'}),
              {
                  {{0x300 /* gravecomb */}, ProbabilityBound::Zero()},
              });

  auto r = graph.ClosureTraversal({
      Node::Segment(0),
  });
  ASSERT_TRUE(r.ok()) << r.status();
  const auto& traversal = *r;

  // Gravecomb interacts with 'i' as only a context glyph, but now with
  // implied edges it can reach 'igrave' (609) which in turn reaches 'i' (141).
  ASSERT_EQ(traversal.ReachedGlyphs(), (GlyphSet{68, 141, 168, 609, 678}));
  ASSERT_EQ(traversal.ContextGlyphs(), (GlyphSet{168}));
}

TEST_F(DependencyGraphTest, ClosurePhasesEnforced) {
  Reconfigure(WithDefaultFeatures({}),
              {
                  {{0x133 /* ij */}, ProbabilityBound::Zero()},
                  {{0x300 /* gravecomb */}, ProbabilityBound::Zero()},
              });

  auto r = graph.ClosureTraversal({
      Node::Segment(0),
      Node::Segment(1),
  });
  ASSERT_TRUE(r.ok()) << r.status();
  const auto& traversal = *r;

  // gravecomb interacts with 'i', but that interaction isn't reachable since
  // 'i' only becomes available after GSUB traversal is finished in the later
  // glyf phase.
  ASSERT_EQ(traversal.ReachedGlyphs(), (GlyphSet{
                                           168 /* gravecomb */,
                                           77 /* i */,
                                           78 /* j */,
                                           740 /* ij */,
                                       }));
  ASSERT_EQ(traversal.ContextGlyphs(), (GlyphSet{}));
}

TEST_F(DependencyGraphTest, IgnoreUnreachable_Uvs) {
  /* <map uv="0x4fae" uvs="0xfe00" name="uniFA30"/>  */
  Reconfigure(noto_sans_jp.get(), WithDefaultFeatures({}),
              {
                  {{0x4fae}, ProbabilityBound::Zero()},
                  {{0xfa30}, ProbabilityBound::Zero()},
              });

  auto r = graph.ClosureTraversal({
      Node::Segment(0),
  });
  ASSERT_TRUE(r.ok()) << r.status();
  const auto& traversal = *r;

  ASSERT_EQ(traversal.ReachedGlyphs(), (GlyphSet{
                                           2684 /* U+4fae */
                                       }));
}

TEST_F(DependencyGraphTest, IgnoreAlreadyReachedPendingEdge) {
  Reconfigure(
      WithDefaultFeatures({}),
      {
          {{'f', 0xfb01 /* fi */, 0xfb03 /* ffi */}, ProbabilityBound::Zero()},
          {{'i'}, ProbabilityBound::Zero()},
      });

  auto r = graph.ClosureTraversal({
      Node::Segment(0),
  });
  ASSERT_TRUE(r.ok()) << r.status();
  const auto& traversal = *r;

  ASSERT_EQ(traversal.ReachedGlyphs(),
            (GlyphSet{
                74 /* f */, 444 /* fi */, 446 /* ffi */
            }));
  // the pending edge of f AND i -> fi isn't included since fi glyph is pulled
  // in via fb01.
  ASSERT_FALSE(traversal.HasPendingEdges());
}

TEST_F(DependencyGraphTest, IgnoreDefaultUVS) {
  /* <map uv="0x798f" uvs="0xe0100"/> should be ignored  */
  Reconfigure(noto_sans_jp_vf.get(), WithDefaultFeatures({}),
              {
                  {{0x798f}, ProbabilityBound::Zero()},
                  {{0xe0100}, ProbabilityBound::Zero()},
              });

  auto r = graph.ClosureTraversal({
      Node::Segment(0),
  });
  ASSERT_TRUE(r.ok()) << r.status();
  const auto& traversal = *r;

  ASSERT_EQ(traversal.ReachedGlyphs(), (GlyphSet{5}));
  // The UVS edge doesn't get considered since it's a default glyph
  // subsitution (effectively a noop).
  ASSERT_FALSE(traversal.HasPendingEdges());
}

TEST_F(DependencyGraphTest, IgnoreUnreachable_Liga) {
  Reconfigure(WithDefaultFeatures({}),
              {
                  {{'f'}, ProbabilityBound::Zero()},
                  {{0xfb01 /* fi */}, ProbabilityBound::Zero()},
              });

  auto r = graph.ClosureTraversal({
      Node::Segment(0),
  });
  ASSERT_TRUE(r.ok()) << r.status();
  const auto& traversal = *r;

  ASSERT_EQ(traversal.ReachedGlyphs(), (GlyphSet{
                                           74 /* f */
                                       }));
  // the edge for the fi ligature never gets traversed since it can't be reached
  // without i present.
  ASSERT_FALSE(traversal.HasPendingEdges());
}

TEST_F(DependencyGraphTest, ImpliedFeatureEdge) {
  SubsetDefinition c2sc;
  c2sc.feature_tags = {HB_TAG('c', '2', 's', 'c')};
  Reconfigure(WithDefaultFeatures({'A'}), {
                                              {{'B'}, ProbabilityBound::Zero()},
                                              {c2sc, ProbabilityBound::Zero()},
                                          });

  /* ### s0 ### */
  auto traversal = graph.ClosureTraversal({
      Node::Segment(0),
  });
  ASSERT_TRUE(traversal.ok()) << traversal.status();

  ASSERT_EQ(traversal->ReachedGlyphs(), (GlyphSet{
                                            38, /* B */
                                        }));
  ASSERT_TRUE(traversal->HasPendingEdges());

  /* ### s1 ### */
  traversal = graph.ClosureTraversal({
      Node::Segment(1),
  });
  ASSERT_TRUE(traversal.ok()) << traversal.status();

  ASSERT_EQ(traversal->ReachedGlyphs(), (GlyphSet{
                                            563, /* smcap A */
                                        }));
  ASSERT_TRUE(traversal->HasPendingEdges());

  /* ### s0 + s1 ### */
  traversal = graph.ClosureTraversal({
      Node::Segment(0),
      Node::Segment(1),
  });
  ASSERT_TRUE(traversal.ok()) << traversal.status();

  ASSERT_EQ(traversal->ReachedGlyphs(), (GlyphSet{
                                            38,  /* B */
                                            562, /* smcap B */
                                            563, /* smcap A */
                                        }));
  ASSERT_FALSE(traversal->HasPendingEdges());
}

TEST_F(DependencyGraphTest, ImpliedFeatureEdge_Liga) {
  SubsetDefinition liga;
  liga.feature_tags = {HB_TAG('l', 'i', 'g', 'a')};
  Reconfigure({'f'}, {
                         {{'i'}, ProbabilityBound::Zero()},
                         {liga, ProbabilityBound::Zero()},
                     });

  /* s0 constraints not satisfied */
  auto traversal = graph.ClosureTraversal({
      Node::Segment(0),
  });
  ASSERT_TRUE(traversal.ok()) << traversal.status();

  ASSERT_EQ(traversal->ReachedGlyphs(), (GlyphSet{
                                            77, /* i */
                                        }));
  ASSERT_TRUE(traversal->HasPendingEdges());

  /* s0 + s1 all constraints satisfied */
  traversal = graph.ClosureTraversal({
      Node::Segment(0),
      Node::Segment(1),
  });
  ASSERT_TRUE(traversal.ok()) << traversal.status();

  ASSERT_EQ(traversal->ReachedGlyphs(), (GlyphSet{
                                            /* f is in init */
                                            77,  /* i */
                                            444, /* fi */
                                            446, /* ffi */
                                        }));
  ASSERT_FALSE(traversal->HasPendingEdges());
}

TEST_F(DependencyGraphTest, PendingEdgesCollection) {
  SubsetDefinition liga;
  liga.feature_tags = {HB_TAG('l', 'i', 'g', 'a')};
  // Roboto has f + i -> fi in liga.
  Reconfigure({'f'}, {
                         {{'i'}, ProbabilityBound::Zero()},
                         {liga, ProbabilityBound::Zero()},
                     });

  // Start with only s0 (i). f is in init.
  // fi is reachable from i IF liga is present.
  // Since liga (s1) is missing, fi should be in pending edges.
  auto traversal = graph.ClosureTraversal({
      Node::Segment(0),
  });
  ASSERT_TRUE(traversal.ok()) << traversal.status();

  ASSERT_TRUE(traversal->HasPendingEdges());
  const auto& pending = traversal->PendingEdges();

  bool found_fi = false;
  for (const auto& edge : pending) {
    if (edge.dest == Node::Glyph(444 /* fi */)) {
      found_fi = true;
      // It should require liga feature.
      EXPECT_EQ(edge.required_feature, HB_TAG('l', 'i', 'g', 'a'));
    }
  }
  EXPECT_TRUE(found_fi);
}

TEST_F(DependencyGraphTest, RequiredGlyphsFor_Liga) {
  Reconfigure(WithDefaultFeatures({}), {
                                           {{'f'}, ProbabilityBound::Zero()},
                                           {{'i'}, ProbabilityBound::Zero()},
                                       });

  auto traversal = graph.ClosureTraversal({
      Node::Segment(0),
  });
  ASSERT_TRUE(traversal.ok()) << traversal.status();
  ASSERT_TRUE(traversal->HasPendingEdges());
  const auto& pending = traversal->PendingEdges();

  bool found_fi = false;
  for (const auto& edge : pending) {
    if (edge.dest == Node::Glyph(444 /* fi */)) {
      EXPECT_EQ(*graph.RequiredGlyphsFor(edge),
                (GlyphSet{74 /* f */, 77 /* i */}));
      found_fi = true;
    }
  }
  EXPECT_TRUE(found_fi);
}

static std::vector<Node> FlattenSccs(
    const std::vector<std::vector<Node>>& sccs) {
  std::vector<Node> out;
  for (const auto& scc : sccs) {
    for (Node n : scc) {
      out.push_back(n);
    }
  }
  return out;
}

static int GetIndex(const std::vector<Node>& topo_order, Node n) {
  for (int i = 0; i < (int)topo_order.size(); ++i) {
    if (topo_order[i] == n) return i;
  }
  return -1;
}

TEST_F(DependencyGraphTest, StronglyConnectedComponents_TopologicalSorting) {
  // For dependency graphs that are DAG's strongly connected components is just
  // a topological sort
  SubsetDefinition liga;
  liga.feature_tags = {HB_TAG('l', 'i', 'g', 'a')};
  Reconfigure({}, {
                      {{'a'}, ProbabilityBound::Zero()},
                      {{'f'}, ProbabilityBound::Zero()},
                      {{'i'}, ProbabilityBound::Zero()},
                      {liga, ProbabilityBound::Zero()},
                  });

  auto sccs_or = graph.StronglyConnectedComponents(
      {FontHelper::kCmap, FontHelper::kGSUB, FontHelper::kGlyf}, 0xFFFFFFFF);
  ASSERT_TRUE(sccs_or.ok()) << sccs_or.status();
  const std::vector<Node> topo_order = FlattenSccs(*sccs_or);
  glyph_id_t gid_a = *FontHelper::GetNominalGlyph(face.get(), 'a');
  glyph_id_t gid_f = *FontHelper::GetNominalGlyph(face.get(), 'f');
  glyph_id_t gid_i = *FontHelper::GetNominalGlyph(face.get(), 'i');

  glyph_id_t gid_fi = *FontHelper::GetNominalGlyph(face.get(), 0xfb01);
  glyph_id_t gid_ffi = *FontHelper::GetNominalGlyph(face.get(), 0xfb03);

  Node liga_node = Node::Feature(HB_TAG('l', 'i', 'g', 'a'));

  ASSERT_THAT(topo_order,
              UnorderedElementsAre(
                  Node::Segment(0), Node::Segment(1), Node::Segment(2),
                  Node::Segment(3), Node::Unicode('a'), Node::Unicode('f'),
                  Node::Unicode('i'), Node::Glyph(gid_a), Node::Glyph(gid_f),
                  Node::Glyph(gid_i), Node::Glyph(gid_fi), Node::Glyph(gid_ffi),
                  Node::Unicode(0xfb01), Node::Unicode(0xfb03), liga_node));

  // Now check relative ordering of the elements
  int s0 = GetIndex(topo_order, Node::Segment(0));
  int s1 = GetIndex(topo_order, Node::Segment(1));
  int s2 = GetIndex(topo_order, Node::Segment(2));
  int s3 = GetIndex(topo_order, Node::Segment(3));

  int ua = GetIndex(topo_order, Node::Unicode('a'));
  int uf = GetIndex(topo_order, Node::Unicode('f'));
  int ui = GetIndex(topo_order, Node::Unicode('i'));

  int ga = GetIndex(topo_order, Node::Glyph(gid_a));
  int gf = GetIndex(topo_order, Node::Glyph(gid_f));
  int gi = GetIndex(topo_order, Node::Glyph(gid_i));
  int gfi = GetIndex(topo_order, Node::Glyph(gid_fi));
  int gffi = GetIndex(topo_order, Node::Glyph(gid_ffi));

  int liga_f = GetIndex(topo_order, liga_node);

  EXPECT_LT(s0, ua);
  EXPECT_LT(s1, uf);
  EXPECT_LT(s2, ui);
  EXPECT_LT(s3, liga_f);

  EXPECT_LT(ua, ga);
  EXPECT_LT(uf, gf);
  EXPECT_LT(ui, gi);

  EXPECT_LT(gf, gfi);
  EXPECT_LT(gf, gffi);
  EXPECT_LT(gi, gfi);
  EXPECT_LT(gi, gffi);

  EXPECT_LT(liga_f, gfi);
  EXPECT_LT(liga_f, gffi);
}

TEST_F(DependencyGraphTest,
       StronglyConnectedComponents_TopologicalSorting_InitFontFilter) {
  SubsetDefinition liga;
  liga.feature_tags = {HB_TAG('l', 'i', 'g', 'a')};
  SubsetDefinition dlig;
  dlig.feature_tags = {HB_TAG('d', 'l', 'i', 'g')};

  Reconfigure(WithDefaultFeatures({'f', 'i'}),
              {
                  {{'a'}, ProbabilityBound::Zero()},
                  {dlig, ProbabilityBound::Zero()},
              });

  auto sccs_or = graph.StronglyConnectedComponents(
      {FontHelper::kCmap, FontHelper::kGSUB, FontHelper::kGlyf}, 0xFFFFFFFF);
  ASSERT_TRUE(sccs_or.ok()) << sccs_or.status();
  const std::vector<Node> topo_order = FlattenSccs(*sccs_or);

  glyph_id_t gid_a = *FontHelper::GetNominalGlyph(face.get(), 'a');

  // Init font items should not be present in the ordering.
  ASSERT_THAT(topo_order,
              UnorderedElementsAre(Node::Segment(0), Node::Segment(1),
                                   Node::Unicode('a'), Node::Glyph(gid_a),
                                   Node::Glyph(443 /* dlig ff */),
                                   Node::Feature(HB_TAG('d', 'l', 'i', 'g'))));
}

TEST_F(DependencyGraphTest,
       StronglyConnectedComponents_TopologicalSorting_InitFontFeatures) {
  Reconfigure(WithDefaultFeatures({}),
              {
                  /* 0 */ {{'a'}, ProbabilityBound::Zero()},
                  /* 1 */ {{'f'}, ProbabilityBound::Zero()},
                  /* 2 */ {{'i'}, ProbabilityBound::Zero()},
                  /* 3 */ {{'q'}, ProbabilityBound::Zero()},
                  /* 4 */ {{'A'}, ProbabilityBound::Zero()},
                  /* 5 */ {{0xC1 /* Aacute */}, ProbabilityBound::Zero()},
              });

  auto sccs_or = graph.StronglyConnectedComponents(
      {FontHelper::kCmap, FontHelper::kGSUB, FontHelper::kGlyf}, 0xFFFFFFFF);
  ASSERT_TRUE(sccs_or.ok()) << sccs_or.status();
  const std::vector<Node> topo_order = FlattenSccs(*sccs_or);

  // f
  int s1 = GetIndex(topo_order, Node::Segment(1));
  int uf = GetIndex(topo_order, Node::Unicode(102));
  int gf = GetIndex(topo_order, Node::Glyph(74));
  int gi = GetIndex(topo_order, Node::Glyph(77));
  int gffi = GetIndex(topo_order, Node::Glyph(446));

  EXPECT_LT(s1, uf);
  EXPECT_LT(uf, gf);
  EXPECT_LT(gf, gffi);
  EXPECT_LT(gi, gffi);
}

TEST_F(DependencyGraphTest,
       StronglyConnectedComponents_TableFilteringWithCycle) {
  SubsetDefinition ccmp;
  ccmp.feature_tags = {HB_TAG('c', 'c', 'm', 'p')};
  Reconfigure({},
              {
                  /* 0 */ {{0xc6 /* AE */}, ProbabilityBound::Zero()},
                  /* 1 */ {{0x301 /* acutecomb */}, ProbabilityBound::Zero()},
                  /* 2 */ {{ccmp}, ProbabilityBound::Zero()},
              });

  // With both GSUB and glyf enabled, it should find a cycle.
  // Cycle: AE -GSUB-> AEacute -glyf-> AE
  auto sccs_or = graph.StronglyConnectedComponents(
      {FontHelper::kGSUB, FontHelper::kGlyf}, 0xFFFFFFFF);
  ASSERT_TRUE(sccs_or.ok()) << sccs_or.status();

  uint32_t num_cycles = 0;
  for (const auto& scc : *sccs_or) {
    if (scc.size() > 1) {
      EXPECT_THAT(scc, UnorderedElementsAre(Node::Glyph(129 /* AE */),
                                            Node::Glyph(811 /* AEacute */)));
      num_cycles++;
    }
  }
  EXPECT_EQ(num_cycles, 1);

  // With only GSUB, there should be no cycles
  sccs_or = graph.StronglyConnectedComponents({FontHelper::kGSUB}, 0xFFFFFFFF);
  EXPECT_TRUE(sccs_or.ok()) << sccs_or.status();
  for (const auto& scc : *sccs_or) {
    EXPECT_EQ(scc.size(), 1);
  }

  // With only glyf, there should be no cycles
  sccs_or = graph.StronglyConnectedComponents({FontHelper::kGlyf}, 0xFFFFFFFF);
  EXPECT_TRUE(sccs_or.ok()) << sccs_or.status();
  for (const auto& scc : *sccs_or) {
    EXPECT_EQ(scc.size(), 1);
  }
}

TEST_F(DependencyGraphTest, CollectIncomingEdges_TableFiltering) {
  SubsetDefinition c2sc;
  c2sc.feature_tags = {HB_TAG('c', '2', 's', 'c')};

  Reconfigure(WithDefaultFeatures({}),
              {
                  /* 0 */ {{'A'}, ProbabilityBound::Zero()},
                  /* 1 */ {c2sc, ProbabilityBound::Zero()},
              });

  glyph_id_t gid_A = *FontHelper::GetNominalGlyph(face.get(), 'A');
  glyph_id_t gid_smcp_A = 563;

  // Without GSUB, smcp sub of A is not reachable.
  auto edges = graph.CollectIncomingEdges({FontHelper::kCmap}, 0xFFFFFFFF);
  ASSERT_TRUE(edges.ok()) << edges.status();
  EXPECT_NE(edges->find(Node::Glyph(gid_A)), edges->end());
  EXPECT_EQ(edges->find(Node::Glyph(gid_smcp_A)), edges->end());

  // With GSUB, it should now be reachable
  edges = graph.CollectIncomingEdges({FontHelper::kGSUB}, 0xFFFFFFFF);
  ASSERT_TRUE(edges.ok()) << edges.status();
  EXPECT_NE(edges->find(Node::Glyph(gid_A)), edges->end());
  EXPECT_NE(edges->find(Node::Glyph(gid_smcp_A)), edges->end());
}

TEST_F(DependencyGraphTest, CollectIncomingEdges_NodeFiltering) {
  SubsetDefinition liga;
  liga.feature_tags = {HB_TAG('l', 'i', 'g', 'a')};

  Reconfigure(WithDefaultFeatures({}),
              {
                  /* 0 */ {{'f'}, ProbabilityBound::Zero()},
                  /* 1 */ {{'i'}, ProbabilityBound::Zero()},
                  /* 2 */ {liga, ProbabilityBound::Zero()},
              });

  glyph_id_t gid_f = *FontHelper::GetNominalGlyph(face.get(), 'f');

  // Filter out Glyph nodes. f (glyph) should have no incoming edges.
  auto edges_or = graph.CollectIncomingEdges(
      {FontHelper::kCmap}, 0xFFFFFFFF & ~Node::NodeType::GLYPH);
  ASSERT_TRUE(edges_or.ok()) << edges_or.status();
  EXPECT_EQ(edges_or->find(Node::Glyph(gid_f)), edges_or->end());

  // Include Glyph nodes. f should have an incoming edge.
  edges_or = graph.CollectIncomingEdges({FontHelper::kCmap}, 0xFFFFFFFF);
  ASSERT_TRUE(edges_or.ok()) << edges_or.status();
  EXPECT_NE(edges_or->find(Node::Glyph(gid_f)), edges_or->end());
}

TEST_F(DependencyGraphTest, CollectIncomingEdges) {
  SubsetDefinition liga;
  liga.feature_tags = {HB_TAG('l', 'i', 'g', 'a')};

  Reconfigure(WithDefaultFeatures({}),
              {
                  /* 0 */ {{'f'}, ProbabilityBound::Zero()},
                  /* 1 */ {{'i'}, ProbabilityBound::Zero()},
                  /* 2 */ {liga, ProbabilityBound::Zero()},
              });

  auto edges_or = graph.CollectIncomingEdges(
      {FontHelper::kCmap, FontHelper::kGSUB}, 0xFFFFFFFF);
  ASSERT_TRUE(edges_or.ok()) << edges_or.status();
  const auto& edges = *edges_or;

  glyph_id_t gid_f = *FontHelper::GetNominalGlyph(face.get(), 'f');
  glyph_id_t gid_i = *FontHelper::GetNominalGlyph(face.get(), 'i');
  glyph_id_t gid_fi = *FontHelper::GetNominalGlyph(face.get(), 0xfb01);

  // 'fi' ligature requires 'f', 'i', and 'liga' feature
  auto fi_edges_it = edges.find(Node::Glyph(gid_fi));
  ASSERT_NE(fi_edges_it, edges.end());
  const auto& fi_edges = fi_edges_it->second;

  EdgeConditionsCnf expected_fi_edge = {
      {Node::Glyph(gid_f)},
      {Node::Glyph(gid_i)},
      {Node::Feature(HB_TAG('l', 'i', 'g', 'a'))},
  };
  EXPECT_EQ(fi_edges, (std::vector<EdgeConditionsCnf>{
                          EdgeConditionsCnf{
                              {Node::Unicode(0xfb01)},
                          },
                          expected_fi_edge,
                      }));

  // 'f' requires 'f' (Unicode)
  auto f_edges_it = edges.find(Node::Glyph(gid_f));
  ASSERT_NE(f_edges_it, edges.end());
  const auto& f_edges = f_edges_it->second;

  EdgeConditionsCnf expected_f_edge = {{Node::Unicode('f')}};
  EXPECT_EQ(f_edges, (std::vector<EdgeConditionsCnf>{expected_f_edge}));
}

TEST_F(DependencyGraphTest, StronglyConnectedComponents_NodeInclusionFilter) {
  SubsetDefinition liga;
  liga.feature_tags = {HB_TAG('l', 'i', 'g', 'a')};
  Reconfigure({}, {
                      {{'a'}, ProbabilityBound::Zero()},
                      {{'f'}, ProbabilityBound::Zero()},
                      {{'i'}, ProbabilityBound::Zero()},
                      {liga, ProbabilityBound::Zero()},
                  });

  glyph_id_t gid_f = *FontHelper::GetNominalGlyph(face.get(), 'f');
  flat_hash_set<Node> filter = {
      Node::Segment(1),
      Node::Unicode('f'),
      Node::Glyph(gid_f),
  };

  auto sccs_or = graph.StronglyConnectedComponents(
      {FontHelper::kCmap, FontHelper::kGSUB, FontHelper::kGlyf}, 0xFFFFFFFF,
      &filter);
  ASSERT_TRUE(sccs_or.ok()) << sccs_or.status();
  const std::vector<Node> topo_order = FlattenSccs(*sccs_or);

  ASSERT_THAT(topo_order, ElementsAre(Node::Segment(1), Node::Unicode('f'),
                                      Node::Glyph(gid_f)));
}

TEST_F(DependencyGraphTest, CollectIncomingEdges_NodeInclusionFilter) {
  SubsetDefinition liga;
  liga.feature_tags = {HB_TAG('l', 'i', 'g', 'a')};

  Reconfigure(WithDefaultFeatures({}),
              {
                  /* 0 */ {{'f'}, ProbabilityBound::Zero()},
                  /* 1 */ {{'i'}, ProbabilityBound::Zero()},
                  /* 2 */ {liga, ProbabilityBound::Zero()},
              });

  glyph_id_t gid_fi = *FontHelper::GetNominalGlyph(face.get(), 0xfb01);
  glyph_id_t gid_f = *FontHelper::GetNominalGlyph(face.get(), 'f');
  glyph_id_t gid_i = *FontHelper::GetNominalGlyph(face.get(), 'i');

  flat_hash_set<Node> filter = {
      Node::Glyph(gid_fi),
  };

  auto edges_or = graph.CollectIncomingEdges(
      {FontHelper::kCmap, FontHelper::kGSUB}, 0xFFFFFFFF, &filter);
  ASSERT_TRUE(edges_or.ok()) << edges_or.status();
  const auto& edges = *edges_or;

  // Only gid_fi should have collected edges
  EXPECT_EQ(edges.size(), 1);
  auto fi_edges_it = edges.find(Node::Glyph(gid_fi));
  ASSERT_NE(fi_edges_it, edges.end());
  const auto& fi_edges = fi_edges_it->second;

  EdgeConditionsCnf expected_fi_edge = {
      {Node::Glyph(gid_f)},
      {Node::Glyph(gid_i)},
      {Node::Feature(HB_TAG('l', 'i', 'g', 'a'))},
  };
  EXPECT_EQ(fi_edges, (std::vector<EdgeConditionsCnf>{
                          EdgeConditionsCnf{
                              {Node::Unicode(0xfb01)},
                          },
                          expected_fi_edge,
                      }));
}

// TODO(garretrieger):
// - basic math, CFF, and COLR tests.

// TODO(garretrieger) we currently only have a few specialized tests, relyng
// primarily on DependencyClosureTest for coverage of DependencyGraph
// functionality. We should add some basic tests here that test DepedencyGraph
// core features in isolation.

}  // namespace ift::dep_graph