#ifndef IFT_DEP_GRAPH_UNICODE_EDGES_H_
#define IFT_DEP_GRAPH_UNICODE_EDGES_H_

#include <vector>
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "hb.h"
#include "ift/common/int_set.h"

namespace ift::dep_graph {

struct UnicodeConjunctiveEdge {
  hb_codepoint_t other_source;
  hb_codepoint_t dest;
};

struct UnicodeEdges {
  absl::flat_hash_map<hb_codepoint_t, std::vector<UnicodeConjunctiveEdge>> composition;
  absl::flat_hash_map<hb_codepoint_t, ift::common::CodepointSet> decomposition;

  static absl::StatusOr<UnicodeEdges> ComputeUnicodeDependencyEdges(
    const common::CodepointSet& unicodes);
};

}  // namespace ift::dep_graph

#endif  // IFT_DEP_GRAPH_UNICODE_EDGES_H_
