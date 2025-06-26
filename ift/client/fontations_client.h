#ifndef IFT_CLIENT_FONTATIONS_CLIENT_H_
#define IFT_CLIENT_FONTATIONS_CLIENT_H_

/*
 * Interface to the fontations IFT client command line programs for use in
 * tests.
 */

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "common/axis_range.h"
#include "common/font_data.h"
#include "common/int_set.h"
#include "ift/encoder/compiler.h"

namespace ift::client {

typedef absl::btree_map<std::string, absl::btree_set<std::string>> graph;

/**
 * Runs 'ift_graph' on the IFT font created by encoder and writes a
 * representation of the graph into 'out'.
 */
absl::Status ToGraph(const ift::encoder::Compiler::Encoding& encoding,
                     graph& out, bool include_patch_paths = false);

/**
 * Runs 'ift_extend' on the IFT font created by encoder and returns the
 * resulting extended font.
 *
 * if non null, applied_uris will be populated with the set of uris that
 * the client ended up fetching and applying.
 */
absl::StatusOr<common::FontData> ExtendWithDesignSpace(
    const ift::encoder::Compiler::Encoding& encoding,
    const common::IntSet& codepoints,
    const absl::btree_set<hb_tag_t>& feature_tags,
    const absl::flat_hash_map<hb_tag_t, common::AxisRange>& design_space,
    absl::btree_set<std::string>* applied_uris = nullptr,
    uint32_t max_round_trips = UINT32_MAX, uint32_t max_fetches = UINT32_MAX);

absl::StatusOr<common::FontData> Extend(
    const ift::encoder::Compiler::Encoding& encoding,
    const common::IntSet& codepoints, uint32_t max_round_trips = UINT32_MAX,
    uint32_t max_fetches = UINT32_MAX);

}  // namespace ift::client

#endif  // IFT_CLIENT_FONTATIONS_CLIENT_H_