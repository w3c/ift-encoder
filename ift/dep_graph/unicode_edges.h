#ifndef IFT_DEP_GRAPH_UNICODE_EDGES_H_
#define IFT_DEP_GRAPH_UNICODE_EDGES_H_

#include <vector>
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "hb.h"
#include "ift/common/int_set.h"
#include "ift/encoder/types.h"

namespace ift::dep_graph {

struct UnicodeConjunctiveEdge {
  hb_codepoint_t other_source;
  hb_codepoint_t dest;

  bool operator==(const UnicodeConjunctiveEdge& other) const = default;
};

struct VariationSelectorEdge {
  hb_codepoint_t unicode;
  hb_codepoint_t gid;

  bool operator==(const VariationSelectorEdge& other) const = default;
};

struct UnicodeEdges {
  absl::flat_hash_map<hb_codepoint_t, std::vector<UnicodeConjunctiveEdge>> composition;
  absl::flat_hash_map<hb_codepoint_t, ift::common::CodepointSet> decomposition;
  absl::flat_hash_map<hb_codepoint_t, std::vector<VariationSelectorEdge>> variation_selector;
  absl::flat_hash_map<hb_codepoint_t, encoder::glyph_id_t> unicode_to_gid;
  absl::flat_hash_map<encoder::glyph_id_t, ift::common::CodepointSet> gid_to_vs;

  static absl::StatusOr<UnicodeEdges> ComputeUnicodeDependencyEdges(hb_face_t* face);

 private:
  static void ComputeUVSEdges(hb_face_t* face, const absl::flat_hash_map<hb_codepoint_t, encoder::glyph_id_t>& unicode_to_gid, UnicodeEdges& result);

  static absl::flat_hash_map<hb_codepoint_t, encoder::glyph_id_t> UnicodeToGid(
      hb_face_t* face);
};

}  // namespace ift::dep_graph

#endif  // IFT_DEP_GRAPH_UNICODE_EDGES_H_
