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
#include "ift/encoder/encoder.h"

namespace ift::client {

typedef absl::btree_map<std::string, absl::btree_set<std::string>> graph;

/**
 * Runs 'ift_graph' on the IFT font created by encoder and writes a
 * representation of the graph into 'out'.
 */
absl::Status ToGraph(const ift::encoder::Encoder& encoder,
                     const common::FontData& base, graph& out);

/**
 * Runs 'ift_extend' on the IFT font created by encoder and returns the
 * resulting extended font.
 *
 * if non null, applied_uris will be populated with the set of uris that
 * the client ended up fetching and applying.
 */
absl::StatusOr<common::FontData> ExtendWithDesignSpace(
    const ift::encoder::Encoder& encoder, const common::FontData& ift_font,
    absl::btree_set<uint32_t> codepoints,
    absl::btree_set<hb_tag_t> feature_tags,
    absl::flat_hash_map<hb_tag_t, common::AxisRange> design_space,
    absl::btree_set<std::string>* applied_uris = nullptr);

absl::StatusOr<common::FontData> Extend(const ift::encoder::Encoder& encoder,
                                        const common::FontData& ift_font,
                                        absl::btree_set<uint32_t> codepoints);

}  // namespace ift::client

#endif  // IFT_CLIENT_FONTATIONS_CLIENT_H_