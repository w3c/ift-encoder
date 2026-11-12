#include "ift/dep_graph/dependency_graph.h"

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "hb-subset.h"
#include "hb.h"
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
using ::testing::Contains;
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
      /* 0 */ {{'a'}},
      /* 1 */ {{'f'}},
      /* 2 */ {{'i'}},
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
                  {{'a'}},
                  {{'b'}},
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
                  {{0xD4DB}},  // korean codepoint not in Roboto
              });

  auto r = graph.ClosureTraversal({Node::Unicode(0xD4DB)});
  ASSERT_TRUE(r.ok()) << r.status();
  // Decompositions should not be present
  EXPECT_EQ(r->ReachedCodepoints(), (CodepointSet{}));
}

TEST_F(DependencyGraphTest, UnicodeCompDecomp_Traversal) {
  Reconfigure(WithDefaultFeatures({}),
              {
                  {{0xe1}},   // á
                  {{0x61}},   // a
                  {{0x301}},  // combining acute
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
                  {{0xe1}},  // á
                  {{0x61}},  // a
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
                                           {{0x2126}},
                                           {{0x3a9}},
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
                  {{0xD4DB}},
                  {{0x1111}},
                  {{0x1171}},
                  {{0x11B6}},
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
                        {{'i'}},
                        {{0x300 /* gravecomb */}},

                        {{'1'}},
                        {{0x2044 /* fraction */}},
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
                  {{0x300 /* gravecomb */}},
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
                  {{0x133 /* ij */}},
                  {{0x300 /* gravecomb */}},
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
                  {{0x4fae}},
                  {{0xfa30}},
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
          {{'f', 0xfb01 /* fi */, 0xfb03 /* ffi */}},
          {{'i'}},
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
                  {{0x798f}},
                  {{0xe0100}},
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
                  {{'f'}},
                  {{0xfb01 /* fi */}},
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
                                              {{'B'}},
                                              {c2sc},
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
                         {{'i'}},
                         {liga},
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
                         {{'i'}},
                         {liga},
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
                                           {{'f'}},
                                           {{'i'}},
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
                      {{'a'}},
                      {{'f'}},
                      {{'i'}},
                      {liga},
                  });

  auto sccs_or = graph.StronglyConnectedComponents(
      {DependencyGraph::kPhaseInit, FontHelper::kCmap, FontHelper::kGSUB,
       FontHelper::kGlyf},
      0xFFFFFFFF);
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
                  {{'a'}},
                  {dlig},
              });

  auto sccs_or = graph.StronglyConnectedComponents(
      {DependencyGraph::kPhaseInit, FontHelper::kCmap, FontHelper::kGSUB,
       FontHelper::kGlyf},
      0xFFFFFFFF);
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
                  /* 0 */ {{'a'}},
                  /* 1 */ {{'f'}},
                  /* 2 */ {{'i'}},
                  /* 3 */ {{'q'}},
                  /* 4 */ {{'A'}},
                  /* 5 */ {{0xC1 /* Aacute */}},
              });

  auto sccs_or = graph.StronglyConnectedComponents(
      {DependencyGraph::kPhaseInit, FontHelper::kCmap, FontHelper::kGSUB,
       FontHelper::kGlyf},
      0xFFFFFFFF);
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
                  /* 0 */ {{0xc6 /* AE */}},
                  /* 1 */ {{0x301 /* acutecomb */}},
                  /* 2 */ {{ccmp}},
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
                  /* 0 */ {{'A'}},
                  /* 1 */ {c2sc},
              });

  glyph_id_t gid_A = *FontHelper::GetNominalGlyph(face.get(), 'A');
  glyph_id_t gid_smcp_A = 563;

  // Without GSUB, smcp sub of A is not reachable.
  auto edges =
      graph.CollectIncomingEdges({FontHelper::kCmap}, 0xFFFFFFFF, 0xFFFFFFFF);
  ASSERT_TRUE(edges.ok()) << edges.status();
  EXPECT_NE(edges->find(Node::Glyph(gid_A)), edges->end());
  EXPECT_EQ(edges->find(Node::Glyph(gid_smcp_A)), edges->end());

  // With GSUB only, cmap edge to gid_A is excluded while smcp sub of A is
  // included.
  edges =
      graph.CollectIncomingEdges({FontHelper::kGSUB}, 0xFFFFFFFF, 0xFFFFFFFF);
  ASSERT_TRUE(edges.ok()) << edges.status();
  EXPECT_EQ(edges->find(Node::Glyph(gid_A)), edges->end());
  EXPECT_NE(edges->find(Node::Glyph(gid_smcp_A)), edges->end());

  // With both cmap and GSUB, both are reachable.
  edges = graph.CollectIncomingEdges({FontHelper::kCmap, FontHelper::kGSUB},
                                     0xFFFFFFFF, 0xFFFFFFFF);
  ASSERT_TRUE(edges.ok()) << edges.status();
  EXPECT_NE(edges->find(Node::Glyph(gid_A)), edges->end());
  EXPECT_NE(edges->find(Node::Glyph(gid_smcp_A)), edges->end());
}

TEST_F(DependencyGraphTest, CollectIncomingEdges_NodeFiltering) {
  SubsetDefinition liga;
  liga.feature_tags = {HB_TAG('l', 'i', 'g', 'a')};

  Reconfigure(WithDefaultFeatures({}),
              {
                  /* 0 */ {{'f'}},
                  /* 1 */ {{'i'}},
                  /* 2 */ {liga},
              });

  glyph_id_t gid_f = *FontHelper::GetNominalGlyph(face.get(), 'f');

  // Filter out Glyph nodes. f (glyph) should have no incoming edges.
  auto edges_or = graph.CollectIncomingEdges(
      {FontHelper::kCmap}, 0xFFFFFFFF, 0xFFFFFFFF & ~Node::NodeType::GLYPH);
  ASSERT_TRUE(edges_or.ok()) << edges_or.status();
  EXPECT_EQ(edges_or->find(Node::Glyph(gid_f)), edges_or->end());

  // Include Glyph nodes. f should have an incoming edge.
  edges_or =
      graph.CollectIncomingEdges({FontHelper::kCmap}, 0xFFFFFFFF, 0xFFFFFFFF);
  ASSERT_TRUE(edges_or.ok()) << edges_or.status();
  EXPECT_NE(edges_or->find(Node::Glyph(gid_f)), edges_or->end());
}

TEST_F(DependencyGraphTest, CollectIncomingEdges) {
  SubsetDefinition liga;
  liga.feature_tags = {HB_TAG('l', 'i', 'g', 'a')};

  Reconfigure(WithDefaultFeatures({}),
              {
                  /* 0 */ {{'f'}},
                  /* 1 */ {{'i'}},
                  /* 2 */ {liga},
              });

  auto edges_or = graph.CollectIncomingEdges(
      {FontHelper::kCmap, FontHelper::kGSUB}, 0xFFFFFFFF, 0xFFFFFFFF);
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
                      {{'a'}},
                      {{'f'}},
                      {{'i'}},
                      {liga},
                  });

  glyph_id_t gid_f = *FontHelper::GetNominalGlyph(face.get(), 'f');
  flat_hash_set<Node> filter = {
      Node::Segment(1),
      Node::Unicode('f'),
      Node::Glyph(gid_f),
  };

  auto sccs_or = graph.StronglyConnectedComponents(
      {DependencyGraph::kPhaseInit, FontHelper::kCmap, FontHelper::kGSUB,
       FontHelper::kGlyf},
      0xFFFFFFFF, &filter);
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
                  /* 0 */ {{'f'}},
                  /* 1 */ {{'i'}},
                  /* 2 */ {liga},
              });

  glyph_id_t gid_fi = *FontHelper::GetNominalGlyph(face.get(), 0xfb01);
  glyph_id_t gid_f = *FontHelper::GetNominalGlyph(face.get(), 'f');
  glyph_id_t gid_i = *FontHelper::GetNominalGlyph(face.get(), 'i');

  flat_hash_set<Node> filter = {
      Node::Glyph(gid_fi),
  };

  auto edges_or = graph.CollectIncomingEdges(
      {FontHelper::kCmap, FontHelper::kGSUB}, 0xFFFFFFFF, 0xFFFFFFFF, &filter);
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

TEST_F(DependencyGraphTest, CollectIncomingEdges_UnicodePhases) {
  Reconfigure(WithDefaultFeatures({}), {
                                           /* 0 */ {{0xC1 /* Aacute */}},
                                           /* 1 */ {{'A'}},
                                           /* 2 */ {{0x301 /* acutecomb */}},
                                           /* 3 */ {{'('}},
                                           /* 4 */ {{')'}},
                                       });

  glyph_id_t gid_A = *FontHelper::GetNominalGlyph(face.get(), 'A');

  // Phase 0 (Init + Bidi): segment -> unicode/feature and bidi unicode ->
  // unicode edges.
  auto init_edges = graph.CollectIncomingEdges({DependencyGraph::kPhaseInit},
                                               0xFFFFFFFF, 0xFFFFFFFF);
  ASSERT_TRUE(init_edges.ok()) << init_edges.status();
  EXPECT_THAT(init_edges->at(Node::Unicode('(')),
              UnorderedElementsAre(EdgeConditionsCnf{{Node::Segment(3)}},
                                   EdgeConditionsCnf{{Node::Unicode(')')}}));
  EXPECT_THAT(init_edges->at(Node::Unicode(')')),
              UnorderedElementsAre(EdgeConditionsCnf{{Node::Segment(4)}},
                                   EdgeConditionsCnf{{Node::Unicode('(')}}));
  EXPECT_EQ(init_edges->at(Node::Unicode('A')),
            (std::vector<EdgeConditionsCnf>{{{Node::Segment(1)}}}));
  EXPECT_EQ(init_edges->at(Node::Unicode(0xC1)),
            (std::vector<EdgeConditionsCnf>{{{Node::Segment(0)}}}));
  EXPECT_EQ(init_edges->find(Node::Glyph(gid_A)), init_edges->end());

  // Phase 2 (Decomposition): only unicode -> decomposed unicode edges.
  auto decomp_edges = graph.CollectIncomingEdges(
      {DependencyGraph::kPhaseDecomposition}, 0xFFFFFFFF, 0xFFFFFFFF);
  ASSERT_TRUE(decomp_edges.ok()) << decomp_edges.status();
  EXPECT_EQ(decomp_edges->at(Node::Unicode('A')),
            (std::vector<EdgeConditionsCnf>{{{Node::Unicode(0xC1)}}}));
  EXPECT_EQ(decomp_edges->at(Node::Unicode(0x301)),
            (std::vector<EdgeConditionsCnf>{{{Node::Unicode(0xC1)}}}));
  EXPECT_EQ(decomp_edges->find(Node::Unicode(0xC1)), decomp_edges->end());
  EXPECT_EQ(decomp_edges->find(Node::Unicode(')')), decomp_edges->end());
  EXPECT_EQ(decomp_edges->find(Node::Glyph(gid_A)), decomp_edges->end());

  // Phase 3 (Composition): only unicode -> composed unicode conjunctive edges.
  auto comp_edges = graph.CollectIncomingEdges(
      {DependencyGraph::kPhaseComposition}, 0xFFFFFFFF, 0xFFFFFFFF);
  ASSERT_TRUE(comp_edges.ok()) << comp_edges.status();
  EXPECT_EQ(comp_edges->at(Node::Unicode(0xC1)),
            (std::vector<EdgeConditionsCnf>{
                {{Node::Unicode('A')}, {Node::Unicode(0x301)}}}));
  EXPECT_EQ(comp_edges->find(Node::Unicode('A')), comp_edges->end());
  EXPECT_EQ(comp_edges->find(Node::Unicode(')')), comp_edges->end());
  EXPECT_EQ(comp_edges->find(Node::Glyph(gid_A)), comp_edges->end());

  // Phase 4 (cmap): only nominal unicode -> glyph edges.
  auto cmap_edges =
      graph.CollectIncomingEdges({FontHelper::kCmap}, 0xFFFFFFFF, 0xFFFFFFFF);
  ASSERT_TRUE(cmap_edges.ok()) << cmap_edges.status();
  EXPECT_EQ(cmap_edges->at(Node::Glyph(gid_A)),
            (std::vector<EdgeConditionsCnf>{{{Node::Unicode('A')}}}));
  EXPECT_EQ(cmap_edges->find(Node::Unicode('A')), cmap_edges->end());
  EXPECT_EQ(cmap_edges->find(Node::Unicode(')')), cmap_edges->end());

  // Phase 1 (Variation Selectors) with NotoSansJP: only UVS -> glyph edges.
  Reconfigure(noto_sans_jp.get(), WithDefaultFeatures({}),
              {
                  /* 0 */ {{0x4FAE}},
                  /* 1 */ {{0xFE00}},
                  /* 2 */ {{0xFA30}},
              });
  glyph_id_t gid_4fae = *FontHelper::GetNominalGlyph(noto_sans_jp.get(), 0x4FAE);
  glyph_id_t gid_fa30 = *FontHelper::GetNominalGlyph(noto_sans_jp.get(), 0xFA30);

  auto uvs_edges = graph.CollectIncomingEdges(
      {DependencyGraph::kPhaseVariationSelectors}, 0xFFFFFFFF, 0xFFFFFFFF);
  ASSERT_TRUE(uvs_edges.ok()) << uvs_edges.status();
  EXPECT_THAT(
      uvs_edges->at(Node::Glyph(gid_fa30)),
      Contains(EdgeConditionsCnf{
          {Node::Unicode(0x4FAE)}, {Node::Unicode(0xFE00)}}));
  EXPECT_EQ(uvs_edges->find(Node::Glyph(gid_4fae)), uvs_edges->end());

  auto jp_cmap_edges =
      graph.CollectIncomingEdges({FontHelper::kCmap}, 0xFFFFFFFF, 0xFFFFFFFF);
  ASSERT_TRUE(jp_cmap_edges.ok()) << jp_cmap_edges.status();
  EXPECT_EQ(jp_cmap_edges->at(Node::Glyph(gid_4fae)),
            (std::vector<EdgeConditionsCnf>{{{Node::Unicode(0x4FAE)}}}));
  EXPECT_EQ(jp_cmap_edges->at(Node::Glyph(gid_fa30)),
            (std::vector<EdgeConditionsCnf>{{{Node::Unicode(0xFA30)}}}));
}

TEST_F(DependencyGraphTest,
       StronglyConnectedComponents_NoCycleBetweenCompAndDecomp) {
  Reconfigure(WithDefaultFeatures({}), {
                                           {{0xC1 /* Aacute */}},
                                           {{'A'}},
                                           {{0x301 /* acutecomb */}},
                                           {{'('}},
                                           {{')'}},
                                       });

  // In Decomposition alone, there are no cycles and Aacute precedes A and 0x301.
  auto decomp_sccs = graph.StronglyConnectedComponents(
      {DependencyGraph::kPhaseDecomposition}, Node::NodeType::UNICODE);
  ASSERT_TRUE(decomp_sccs.ok()) << decomp_sccs.status();
  for (const auto& scc : *decomp_sccs) {
    EXPECT_EQ(scc.size(), 1);
  }
  const std::vector<Node> decomp_order = FlattenSccs(*decomp_sccs);
  EXPECT_LT(GetIndex(decomp_order, Node::Unicode(0xC1)),
            GetIndex(decomp_order, Node::Unicode('A')));
  EXPECT_LT(GetIndex(decomp_order, Node::Unicode(0xC1)),
            GetIndex(decomp_order, Node::Unicode(0x301)));

  // In Composition alone, there are no cycles and A and 0x301 precede Aacute.
  auto comp_sccs = graph.StronglyConnectedComponents(
      {DependencyGraph::kPhaseComposition}, Node::NodeType::UNICODE);
  ASSERT_TRUE(comp_sccs.ok()) << comp_sccs.status();
  for (const auto& scc : *comp_sccs) {
    EXPECT_EQ(scc.size(), 1);
  }
  const std::vector<Node> comp_order = FlattenSccs(*comp_sccs);
  EXPECT_LT(GetIndex(comp_order, Node::Unicode('A')),
            GetIndex(comp_order, Node::Unicode(0xC1)));
  EXPECT_LT(GetIndex(comp_order, Node::Unicode(0x301)),
            GetIndex(comp_order, Node::Unicode(0xC1)));

  // Init + Bidi has its own symmetric 2-node SCC between '(' and ')'.
  auto init_sccs = graph.StronglyConnectedComponents(
      {DependencyGraph::kPhaseInit},
      Node::NodeType::SEGMENT | Node::NodeType::UNICODE);
  ASSERT_TRUE(init_sccs.ok()) << init_sccs.status();
  uint32_t bidi_cycles = 0;
  for (const auto& scc : *init_sccs) {
    if (scc.size() > 1) {
      EXPECT_THAT(scc, UnorderedElementsAre(Node::Unicode('('),
                                            Node::Unicode(')')));
      bidi_cycles++;
    }
  }
  EXPECT_EQ(bidi_cycles, 1);
  const std::vector<Node> init_order = FlattenSccs(*init_sccs);
  EXPECT_LT(GetIndex(init_order, Node::Segment(3)),
            GetIndex(init_order, Node::Unicode('(')));
  EXPECT_LT(GetIndex(init_order, Node::Segment(4)),
            GetIndex(init_order, Node::Unicode(')')));

  // If Decomposition and Composition were combined, they would form a cycle.
  auto combined_sccs = graph.StronglyConnectedComponents(
      {DependencyGraph::kPhaseDecomposition,
       DependencyGraph::kPhaseComposition},
      Node::NodeType::UNICODE);
  ASSERT_TRUE(combined_sccs.ok()) << combined_sccs.status();
  uint32_t combined_cycles = 0;
  for (const auto& scc : *combined_sccs) {
    if (scc.size() > 1) {
      EXPECT_THAT(scc, UnorderedElementsAre(Node::Unicode(0xC1),
                                            Node::Unicode('A'),
                                            Node::Unicode(0x301)));
      combined_cycles++;
    }
  }
  EXPECT_EQ(combined_cycles, 1);
}

TEST_F(DependencyGraphTest, ClosureTraversal_DecompBeforeComp) {
  Reconfigure(WithDefaultFeatures({}), {
                                           {{0xE1 /* aacute */}},
                                           {{'e'}},
                                           {{'a'}},
                                           {{0x301 /* acutecomb */}},
                                           {{0xE9 /* eacute */}},
                                       });

  auto r = graph.ClosureTraversal({Node::Unicode(0xE1), Node::Unicode('e')});
  ASSERT_TRUE(r.ok()) << r.status();
  EXPECT_EQ(r->ReachedCodepoints(),
            (CodepointSet{0xE1, 'e', 'a', 0x301, 0xE9}));

  glyph_id_t gid_eacute = *FontHelper::GetNominalGlyph(face.get(), 0xE9);
  EXPECT_TRUE(r->ReachedGlyphs().contains(gid_eacute));
  EXPECT_TRUE(r->TraversedTables().contains(FontHelper::kCmap));
  EXPECT_FALSE(
      r->TraversedTables().contains(DependencyGraph::kPhaseDecomposition));
  EXPECT_FALSE(
      r->TraversedTables().contains(DependencyGraph::kPhaseComposition));
}

TEST_F(DependencyGraphTest, ClosureTraversal_BidiBeforeDecomposition) {
  // Keep 0x2329, 0x3008, 0x3009 while dropping 0x232A (bidi mirror of 0x2329).
  // 0x2329 canonically decomposes to 0x3008, and 0x3008 <-> 0x3009 are bidi
  // mirrors.
  hb_subset_input_t* input = hb_subset_input_create_or_fail();
  ASSERT_NE(input, nullptr);
  hb_subset_input_set_flags(input, HB_SUBSET_FLAGS_NO_BIDI_CLOSURE);
  hb_set_t* unicodes = hb_subset_input_unicode_set(input);
  hb_set_add(unicodes, 0x2329);
  hb_set_add(unicodes, 0x3008);
  hb_set_add(unicodes, 0x3009);
  hb_face_unique_ptr subset =
      ift::common::make_hb_face(hb_subset_or_fail(noto_sans_jp.get(), input));
  hb_subset_input_destroy(input);
  ASSERT_NE(subset.get(), nullptr);
  ift::common::hb_blob_unique_ptr blob =
      ift::common::make_hb_blob(hb_face_reference_blob(subset.get()));
  hb_face_unique_ptr subset_face =
      ift::common::make_hb_face(hb_face_create(blob.get(), 0));

  CodepointSet subset_cps = FontHelper::ToCodepointsSet(subset_face.get());
  ASSERT_TRUE(subset_cps.contains(0x2329));
  ASSERT_TRUE(subset_cps.contains(0x3008));
  ASSERT_TRUE(subset_cps.contains(0x3009));
  ASSERT_FALSE(subset_cps.contains(0x232A));

  Reconfigure(subset_face.get(), WithDefaultFeatures({}),
              {
                  {{0x2329}},
                  {{0x3008}},
                  {{0x3009}},
              });

  // Starting from 0x2329: Bidi runs before Decomposition, so 0x3008 (reached
  // only via Decomposition of 0x2329) must NOT trigger Bidi mirroring to
  // 0x3009.
  auto r = graph.ClosureTraversal({Node::Unicode(0x2329)});
  ASSERT_TRUE(r.ok()) << r.status();
  EXPECT_EQ(r->ReachedCodepoints(), (CodepointSet{0x2329, 0x3008}));

  // Starting from 0x3009: Bidi mirrors 0x3009 -> 0x3008.
  r = graph.ClosureTraversal({Node::Unicode(0x3009)});
  ASSERT_TRUE(r.ok()) << r.status();
  EXPECT_EQ(r->ReachedCodepoints(), (CodepointSet{0x3009, 0x3008}));
}

TEST_F(DependencyGraphTest, ClosureTraversal_UvsBeforeDecomposition) {
  // 0xF901 canonically decomposes to 0x66F4, and (0x66F4, 0xE0101) maps via UVS
  // to glyph 16687 (while nominal glyph for both 0xF901 and 0x66F4 is 6563).
  Reconfigure(noto_sans_jp.get(), WithDefaultFeatures({}),
              {
                  /* 0 */ {{0xF901}},
                  /* 1 */ {{0xE0101}},
                  /* 2 */ {{0x66F4}},
              });

  // Starting from 0xF901 + 0xE0101: Phase 2 (Variation Selectors) runs before
  // Phase 3 (Decomposition), so 0x66F4 (reached via Decomposition) must NOT
  // activate UVS glyph 16687.
  auto r = graph.ClosureTraversal({Node::Segment(0), Node::Segment(1)});
  ASSERT_TRUE(r.ok()) << r.status();
  EXPECT_TRUE(r->ReachedCodepoints().contains(0x66F4));
  EXPECT_TRUE(r->ReachedGlyphs().contains(6563));
  EXPECT_FALSE(r->ReachedGlyphs().contains(16687));

  // Starting from 0x66F4 + 0xE0101 directly DOES reach UVS glyph 16687.
  r = graph.ClosureTraversal({Node::Segment(2), Node::Segment(1)});
  ASSERT_TRUE(r.ok()) << r.status();
  EXPECT_TRUE(r->ReachedGlyphs().contains(16687));
}

TEST_F(DependencyGraphTest, ClosureTraversal_UvsPhaseAndPendingPruning) {
  Reconfigure(noto_sans_jp.get(), WithDefaultFeatures({}),
              {
                  /* 0 */ {{0x4FAE, 0xFA30}},
                  /* 1 */ {{0xFE00}},
              });

  glyph_id_t gid_fa30 = *FontHelper::GetNominalGlyph(noto_sans_jp.get(), 0xFA30);

  // Segment 0 reaches gid_fa30 via cmap (Phase 5) even though Phase 2 (UVS)
  // leaves a pending edge (0x4FAE, 0xFE00) -> gid_fa30. That pending edge must
  // be pruned because gid_fa30 was reached in Phase 5.
  auto r = graph.ClosureTraversal({Node::Segment(0)});
  ASSERT_TRUE(r.ok()) << r.status();
  EXPECT_TRUE(r->ReachedGlyphs().contains(gid_fa30));
  EXPECT_FALSE(r->HasPendingEdges());

  // When both base and variation selector are reached, UVS (Phase 2) reaches
  // gid_fa30 directly without adding codepoint 0xFA30, and records kCmap table.
  Reconfigure(noto_sans_jp.get(), WithDefaultFeatures({}),
              {
                  /* 0 */ {{0x4FAE}},
                  /* 1 */ {{0xFE00}},
              });
  r = graph.ClosureTraversal({Node::Segment(0), Node::Segment(1)});
  ASSERT_TRUE(r.ok()) << r.status();
  EXPECT_TRUE(r->ReachedGlyphs().contains(gid_fa30));
  EXPECT_FALSE(r->ReachedCodepoints().contains(0xFA30));
  EXPECT_TRUE(r->TraversedTables().contains(FontHelper::kCmap));
  EXPECT_FALSE(r->TraversedTables().contains(
      DependencyGraph::kPhaseVariationSelectors));
}

// TODO(garretrieger):
// - basic math, CFF, and COLR tests.

// TODO(garretrieger) we currently only have a few specialized tests, relyng
// primarily on DependencyClosureTest for coverage of DependencyGraph
// functionality. We should add some basic tests here that test DepedencyGraph
// core features in isolation.

}  // namespace ift::dep_graph