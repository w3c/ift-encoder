#include "common/sparse_bit_set.h"

#include <unordered_map>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/bit_input_buffer.h"
#include "common/bit_output_buffer.h"
#include "common/branch_factor.h"
#include "common/int_set.h"
#include "hb.h"

namespace common {

using absl::StatusOr;
using absl::string_view;
using common::IntSet;
using std::string;
using std::unordered_map;
using std::vector;

// Finds the tree height needed to represent the codepoints in the set.
uint32_t TreeDepthFor(uint32_t max_value, BranchFactor branch_factor) {
  uint32_t depth = 1;
  uint64_t max_value_64 = max_value >> kBFNodeSizeLog2[branch_factor];
  while (max_value_64) {
    depth++;
    max_value_64 >>= kBFNodeSizeLog2[branch_factor];
  }
  return depth;
}

uint32_t TreeDepthFor(const vector<uint32_t>& codepoints,
                      BranchFactor branch_factor) {
  return TreeDepthFor(codepoints[codepoints.size() - 1], branch_factor);
}

// Returns the log base 2 of the number of values that can be encoded by the
// descendants of a single bit in the given layer of a tree with the given
// depth, using bits_per_node bits at each node.
//
// For example in layer 0 (root) of a tree of depth 3, with 2 bits per node,
// each bit (a node at level 1) represents 4 values (2 child nodes, each with
// 2 values), so the result would be 2 (2**2 = 4).
//
// Because the size is always a multiple of two, it is faster to count the
// number of bits, then use bit shifting to multiply and divide by this amount.
uint8_t ValuesPerBitLog2ForLayer(uint32_t layer, uint32_t tree_depth,
                                 BranchFactor branch_factor) {
  int num_layers = (int)tree_depth - (int)layer - 1;
  return kBFNodeSizeLog2[branch_factor] * num_layers;
}

StatusOr<string_view> SparseBitSet::Decode(string_view sparse_bit_set,
                                           IntSet& out) {
  // TODO(garretrieger): ignore values beyond unicode max as required by spec.
  if (sparse_bit_set.empty()) {
    return sparse_bit_set;
  }

  BitInputBuffer bits(sparse_bit_set);
  BranchFactor branch_factor = bits.GetBranchFactor();
  uint32_t tree_height = bits.Depth();

  // Enforce upper limits on tree sizes.
  // We only need to encode the 32 bit range 0x0 .. 0xFFFFFFFF.
  if (tree_height > kBFMaxDepth[branch_factor]) {
    return absl::InvalidArgumentError(absl::StrCat("tree_height, ", tree_height,
                                                   " is larger than max ",
                                                   kBFMaxDepth[branch_factor]));
  }

  // At each level, this is the number of leaf values a node covers.
  // To be able to describe a range at least 32 bits large (some branch factors
  // cover slightly more than that exaxt range), 64 bits are needed.
  uint64_t leaf_node_size = 1ull
                            << (kBFNodeSizeLog2[branch_factor] * tree_height);
  // At each level, to get from node_base to the values at the leaf level,
  // multiply by this. For example in a BF=4 D=4 tree, at level 1, the node
  // with node_base 2 covers final leaf values starting at 2 * 16.
  // Bit-based version of:
  //   node_base_factor = leaf_node_size / kBFNodeSize[branch_factor];
  uint64_t node_base_factor = leaf_node_size >> kBFNodeSizeLog2[branch_factor];
  vector<uint32_t> node_bases{0u};  // Root node.
  vector<uint32_t> next_level_node_bases;
  vector<hb_codepoint_t> pending_codepoints;

  for (uint32_t level = 0; level < tree_height; level++) {
    for (uint32_t node_base : node_bases) {
      // This is a normal node so read a node's worth of bits.
      uint32_t current_node_bits;
      if (!bits.read(&current_node_bits)) {
        return absl::InvalidArgumentError("ran out of node bits.");
      }
      if (current_node_bits == 0u) {
        // This is a completely filled node encoded as a zero!
        uint32_t leaf_node_base = node_base * node_base_factor;
        // Add to the set now; range additions are efficient.
        out.insert_range(leaf_node_base, leaf_node_base + leaf_node_size - 1);
      } else {
        // It's a normally encoded node.
        for (uint32_t bit_index = 0u; bit_index < kBFNodeSize[branch_factor];
             bit_index++) {
          if (current_node_bits & (1u << bit_index)) {
            if (level == tree_height - 1) {
              // Queue up individual additions to the set for a later bulk add.
              // Bit-based version of:
              //   pending_codepoints.push_back(node_base + bit_index);
              pending_codepoints.push_back(node_base | bit_index);
            } else {
              // Bit-based version of:
              //   base = (node_base + bit_index) * kBFNodeSize[branch_factor];
              uint32_t base = (node_base | bit_index)
                              << kBFNodeSizeLog2[branch_factor];
              next_level_node_bases.push_back(base);
            }
          }
        }
      }
    }
    // Bit-based version of:
    //    leaf_node_size /= kBFNodeSize[branch_factor];
    //    node_base_factor /= kBFNodeSize[branch_factor];
    leaf_node_size >>= kBFNodeSizeLog2[branch_factor];
    node_base_factor >>= kBFNodeSizeLog2[branch_factor];
    node_bases.swap(next_level_node_bases);
    next_level_node_bases.clear();
  }
  if (!pending_codepoints.empty()) {
    out.insert_sorted_array(pending_codepoints);
  }

  return bits.Remaining();
}

static void AdvanceToCp(uint32_t prev_cp, uint32_t cp,
                        uint32_t empty_leaves[BF32 + 1] /* OUT */) {
  if ((cp < kBFNodeSize[BF2]) || (cp - prev_cp < kBFNodeSize[BF2])) {
    return;
  }
  uint32_t first_missing = prev_cp + 1;
  // Count skipped over nodes, if any.
  for (BranchFactor branch_factor : {BF2, BF4, BF8, BF32}) {
    // Find start of node at least 1 after last cp (first missing value).
    // Bit-based version of:
    //   uint32_t remainder = first_missing % kBFNodeSize[branch_factor];
    uint32_t remainder = first_missing & kBFNodeSizeBitMask[branch_factor];
    uint32_t start =
        remainder ? first_missing + (kBFNodeSize[branch_factor] - remainder)
                  : first_missing;
    // Find start of node containing current value - 1 (last missing value).
    // Bit-based version of:
    //   remainder = cp % kBFNodeSize[branch_factor];
    remainder = cp & kBFNodeSizeBitMask[branch_factor];
    uint32_t end = cp - remainder;
    if (end > start) {
      uint32_t delta = end - start;
      // Bit-based version of:
      //   empty_leaves[branch_factor] += delta / kBFNodeSize[branch_factor];
      empty_leaves[branch_factor] += delta >> kBFNodeSizeLog2[branch_factor];
    }
  }
}

// Given a tree with num_leaf_nodes, quickly estimate the number of nodes above
// the leaves.
uint32_t EstimateTreeSize(uint32_t num_leaf_nodes, BranchFactor branch_factor) {
  // Instead of iterating across all the levels from leaf to root, summing the
  // numbers of nodes at each level, and reducing the # of nodes by a constant
  // factor, we can do all the adds and multiplies via a single multiply.
  //
  // For example, if you keep dividing by 2 each level, then the sum is the
  // equivalent of multiplying by 2, because 1/2 + 1/4 + 1/16 + 1/32 ... = 1.
  // In general the sum of 1/(x**n) n=1..infinity is 1/(x-1).
  //
  // The ratios below were chosen to match the tree sizes seen in a combination
  // of uniform random and codepoint-usage-frequency weighted random sets.
  double geometric_sum = 1.0;
  switch (branch_factor) {
    case BF2:
      // Estimate that the number of nodes divides by 1.4 going up each level.
      geometric_sum = 1.0 / 0.4;
      break;
    case BF4:
      // Estimate that the number of nodes divides by 2.8 going up each level.
      geometric_sum = 1.0 / 1.8;
      break;
    case BF8:
      // Estimate that the number of nodes divides by 4 going up each level.
      geometric_sum = 1.0 / 3.0;
      break;
    case BF32:
      // Estimate that the number of nodes divides by going up at each level.
      geometric_sum = 1.0 / 15.0;
      break;
  }
  return (uint32_t)(num_leaf_nodes * geometric_sum);
}

BranchFactor ChooseBranchFactor(const vector<hb_codepoint_t>& codepoints,
                                vector<uint32_t>& filled_twigs /* OUT */) {
  uint32_t empty_leaves[BF32 + 1]{};

  // "Twigs" are one level above leaves.
  // Zero-encoding happens at this level or above.
  // Only consider the twig level here.
  vector<uint32_t> bf2;
  vector<uint32_t> bf4;
  vector<uint32_t> bf8;
  vector<uint32_t> bf32;
  vector<uint32_t> all_filled_twigs[]{bf2, bf4, bf8, bf32};

  auto it = codepoints.begin();
  if (it == codepoints.end()) {
    return BF8;
  }
  // 0 .. cp-1 are missing/empty (if any).
  hb_codepoint_t cp = *it++;
  AdvanceToCp(UINT32_MAX, cp, empty_leaves);
  uint32_t seq_len = 1;
  uint32_t prev_cp = cp;
  while (it != codepoints.end()) {
    cp = *it++;
    AdvanceToCp(prev_cp, cp, empty_leaves);
    if (cp == prev_cp + 1) {
      seq_len++;
    } else {
      seq_len = 1;
    }
    for (BranchFactor branch_factor : {BF2, BF4, BF8, BF32}) {
      // Bit-based version of:
      //   bool last_value_in_twig = (cp + 1) % kBFTwigSize[branch_factor] == 0;
      bool last_value_in_twig = (cp & kBFTwigSizeBitMask[branch_factor]) ==
                                kBFTwigSizeBitMask[branch_factor];
      if (last_value_in_twig) {
        if (seq_len >= kBFTwigSize[branch_factor]) {
          // Bit-based version of:
          //   all_filled_twigs[branch_factor].push_back(cp / twig_size);
          all_filled_twigs[branch_factor].push_back(
              cp >> kBFTwigSizeLog2[branch_factor]);
        }
      } else {
        break;
      }
    }
    prev_cp = cp;
  }

  uint32_t bytes[BF32 + 1];
  for (BranchFactor branch_factor : {BF2, BF4, BF8, BF32}) {
    // We probably did not see the entire range encoded by the leaf layer of the
    // tree for this set (depth depends on BF and max value). The remaining
    // leaves will all be empty and can be ignored. Finish off current node /
    // round up to next node.
    // Bit-based version of:
    //  remainder = (prev_cp + 1) % kBFNodeSize[branch_factor];
    uint32_t remainder = (prev_cp + 1) & kBFNodeSizeBitMask[branch_factor];
    if (remainder) {
      prev_cp += kBFNodeSize[branch_factor] - remainder;
    }
    // Bit-based version of:
    //   processed_leaves = (prev_cp + 1) / kBFNodeSize[branch_factor];
    uint32_t processed_leaves = (prev_cp + 1) >> kBFNodeSizeLog2[branch_factor];
    // Of the leaves we processed, throw out the empty ones and the filled ones.
    // These are the nodes that will be encoded. Each twig represents multiple
    // leaves.
    // Bit-based version of:
    //   filled_leaves = all_filled_twigs[branch_factor].size() *
    //   kBFNodeSize[branch_factor];
    uint32_t filled_leaves = all_filled_twigs[branch_factor].size()
                             << kBFNodeSizeLog2[branch_factor];
    uint32_t leaf_nodes =
        processed_leaves - empty_leaves[branch_factor] - filled_leaves;
    // Now estimate the size of the rest of the tree above the leaves.
    uint32_t tree_nodes = EstimateTreeSize(leaf_nodes, branch_factor);
    // Compute size in bytes.
    switch (branch_factor) {
      case BF2:
        bytes[branch_factor] = (leaf_nodes + tree_nodes) >> 2;
        break;
      case BF4:
        bytes[branch_factor] = (leaf_nodes + tree_nodes) >> 1;
        break;
      case BF8:
        bytes[branch_factor] = (leaf_nodes + tree_nodes);
        break;
      case BF32:
        bytes[branch_factor] = (leaf_nodes + tree_nodes) << 2;
        break;
    }
  }

  // Pick the one that saves the most bytes, defaulting to order BF4, BF2,
  // BF32, BF8 in the case of ties.
  BranchFactor optimal = BF4;
  for (BranchFactor bf : {BF2, BF32, BF8}) {
    uint32_t depth = TreeDepthFor(codepoints, bf);
    if (depth > kBFMaxDepth[bf]) {
      // Don't consider options that would exceed max depth.
      continue;
    }

    if (bytes[bf] < bytes[optimal]) {
      optimal = bf;
    }
  }
  filled_twigs.swap(all_filled_twigs[optimal]);
  return optimal;
}

vector<uint32_t> FindFilledTwigs(const vector<hb_codepoint_t>& codepoints,
                                 BranchFactor branch_factor,
                                 vector<uint32_t>& filled_twigs /* OUT */) {
  uint32_t prev_cp = UINT32_MAX - 1;
  uint32_t seq_len = 0;
  for (hb_codepoint_t cp : codepoints) {
    if (cp == prev_cp + 1) {
      seq_len++;
    } else {
      seq_len = 1;
    }
    // Bit based version of:
    //   bool last_value_in_twig = (cp + 1) % twig_size == 0;
    bool last_value_in_twig = (cp & kBFTwigSizeBitMask[branch_factor]) ==
                              kBFTwigSizeBitMask[branch_factor];
    if (last_value_in_twig) {
      if (seq_len == kBFTwigSize[branch_factor]) {
        // Bit-based version of: filled_twigs.push_back(cp / twig_size);
        filled_twigs.push_back(cp >> kBFTwigSizeLog2[branch_factor]);
      }
      seq_len = 0;
    }
    prev_cp = cp;
  }
  return filled_twigs;
}

/*
 * Determines which nodes are completely filled, and thus should be encoded
 * with a zero. Leaf nodes are never marked as filled - writing all 0s instead
 * of all ones would not save any bytes. So the length of the array is the
 * number of nodes one level above the leaf level. For a given codepoint CP,
 * the value stored at index CP / (bits_per_node * bits_per_node). The value
 * will be the tree depth (0 for root) at which the node is first completely
 * filled, and thus should be encoded as a zero. The value will be greater
 * than the tree height when no filled nodes exist for these codepoints.
 */
unordered_map<uint32_t, uint8_t> FindFilledNodes(
    BranchFactor branch_factor, uint32_t tree_height,
    const vector<uint32_t>& filled_twigs) {
  unordered_map<uint32_t, uint8_t> filled_levels;
  if (tree_height < 2 || filled_twigs.empty()) {
    return filled_levels;
  }
  // "Twigs" are nodes one layer above the leaves. Layer tree_height - 2.
  for (uint32_t filled_twig : filled_twigs) {
    filled_levels[filled_twig] = tree_height - 2;
  }

  // Now work our way up the layers, "merging" filled nodes by decrementing
  // their filled-at number. Start processing at the layer above the twigs.
  uint32_t node_size =
      kBFNodeSize[branch_factor];  // Number to twigs to consider as a node.
  uint32_t node_size_bit_mask = kBFNodeSizeBitMask[branch_factor];
  for (int layer = (int)tree_height - 3; layer >= 0; layer--) {
    uint8_t target_level = layer + 1;
    uint32_t prev_twig = UINT32_MAX - 1;
    uint32_t seq_len = 0;
    uint32_t num_merged_nodes = 0;
    for (uint32_t twig : filled_twigs) {
      uint8_t filled_level = filled_levels[twig];
      if (twig == prev_twig + 1 && filled_level == target_level) {
        seq_len++;  // Continue a good sequence.
      } else if (filled_level == target_level) {
        seq_len = 1;  // Start a possible new sequence.
      } else {
        seq_len = 0;  // Can not be part of a sequence.
      }
      // Bit-based version of:
      // bool last_value_in_twig = (twig + 1) % node_size == 0;
      bool last_value_in_twig =
          (twig & node_size_bit_mask) == node_size_bit_mask;
      if (last_value_in_twig) {
        if (seq_len == node_size) {
          for (uint32_t i = twig - node_size + 1; i <= twig; i++) {
            filled_levels.find(i)->second = layer;  // Increment to next level.
          }
          num_merged_nodes++;
        }
        seq_len = 0;
      }
      prev_twig = twig;
    }
    if (num_merged_nodes < kBFNodeSize[branch_factor]) {
      break;  // No further merges are possible.
    }
    // Bit-based version of: node_size *= branch_factor;
    node_size <<= kBFNodeSizeLog2[branch_factor];
    // N zeros in a row, then 32-N ones in a row.
    node_size_bit_mask <<= kBFNodeSizeLog2[branch_factor];
    node_size_bit_mask |= kBFNodeSizeBitMask[branch_factor];
  }
  return filled_levels;
}

enum EncodeState {
  START,
  BUILDING_NORMAL_NODE,
  SKIPPING_FILLED_NODE,
  END,
  ERROR,
};

enum EncodeSymbolType {
  NEW_NORMAL_NODE,
  EXISTING_NORMAL_NODE,
  NEW_FILLED_NODE,
  EXISTING_FILLED_NODE,
  END_OF_VALUES,
  INVALID,
};

struct EncodeSymbol {
  EncodeSymbolType type;
  uint32_t cp;
};

static const uint32_t kInvalidCp = UINT32_MAX;
static const EncodeSymbol kEndOfValues =
    EncodeSymbol{END_OF_VALUES, kInvalidCp};

struct EncodeContext {
  const uint32_t layer;
  const BranchFactor branch_factor;
  const uint32_t tree_height;
  const uint8_t values_per_bit_log_2;
  const uint64_t node_size;
  const unordered_map<uint32_t, uint8_t>& filled_levels;
  const vector<uint32_t>& node_bases;
  int next_node_base;
  uint32_t node_base;
  uint64_t node_max;
  uint32_t node_mask;
  uint32_t filled_max;
  vector<uint32_t>& next_node_bases; /* OUT */
  BitOutputBuffer& bit_buffer;       /* OUT */
};

static EncodeSymbolType OverrideIfFilled(uint32_t cp,
                                         const EncodeContext& context) {
  // Bit-based version of: twig = cp / context.twig_size;
  uint32_t twig = cp >> kBFTwigSizeLog2[context.branch_factor];
  if (context.filled_levels.count(twig)) {
    uint8_t filled_level = context.filled_levels.at(twig);
    if (context.layer == filled_level) {
      return NEW_FILLED_NODE;
    } else if (context.layer > filled_level) {
      return EXISTING_FILLED_NODE;
    }
  }
  return NEW_NORMAL_NODE;
}

static void ParseCodepoint(uint32_t cp, EncodeState state,
                           const EncodeContext& context,
                           EncodeSymbol& symbol /* OUT */) {
  symbol.cp = cp;
  switch (state) {
    case START:
      symbol.type = OverrideIfFilled(cp, context);
      break;
    case BUILDING_NORMAL_NODE:
      if (cp <= context.node_max) {
        symbol.type = EXISTING_NORMAL_NODE;
      } else {
        symbol.type = OverrideIfFilled(cp, context);
      }
      break;
    case SKIPPING_FILLED_NODE:
      if (cp <= context.filled_max) {
        symbol.type = EXISTING_FILLED_NODE;  // Keep skipping.
      } else {
        symbol.type = OverrideIfFilled(cp, context);
      }
      break;
    case END:
    case ERROR:
      // No more values should happen while in the END state.
      symbol.cp = kInvalidCp;
      symbol.type = INVALID;
  }
}

static void StartFilledNode(EncodeContext& context) {
  uint32_t node_base = context.node_bases[context.next_node_base++];
  context.bit_buffer.append(0u);
  context.filled_max = node_base + context.node_size - 1;
}

static void SkipExistingFilledNode(uint32_t cp, EncodeContext& context) {
  // Bit-based version of: twig = cp / twig-size;
  uint32_t twig = cp >> kBFTwigSizeLog2[context.branch_factor];
  // Scan to the right across all applicable filled twigs.
  do {
    uint8_t filled_depth = context.filled_levels.at(twig);
    // # of twigs covered by this filled node depends on its level.
    uint32_t twig_size = 1 << (context.tree_height - filled_depth - 2) *
                                  kBFNodeSizeLog2[context.branch_factor];
    // Advance 1 past this filled node.
    twig += twig_size;
    // Did we land on another filled node?
  } while (context.filled_levels.count(twig) &&
           context.filled_levels.at(twig) < context.layer);
  // Bit-based version of: context.filled_max = (twig * context.twig_size) - 1;
  context.filled_max = twig << kBFTwigSizeLog2[context.branch_factor];
  context.filled_max--;
}

static void EndNormalNode(EncodeContext& context) {
  context.bit_buffer.append(context.node_mask);
  // Reset context.
  context.node_mask = 0u;
  context.node_base = kInvalidCp;
  context.node_max = kInvalidCp;
  context.filled_max = kInvalidCp;
}

static void UpdateNodeBit(uint32_t cp, EncodeContext& context) {
  // Figure out which sub-range (bit) cp falls in.
  uint32_t bit_index = (cp - context.node_base) >> context.values_per_bit_log_2;
  uint32_t cp_mask = 1u << bit_index;

  // If this bit is already set, no action needed.
  if (!(context.node_mask & cp_mask)) {
    // We are setting this bit for the first time.
    context.node_mask |= cp_mask;
    // Record its base value in the next layer.
    if (context.values_per_bit_log_2 > 0) {
      // Only compute bases if we're not in the last/leaf layer.
      context.next_node_bases.push_back(
          // Bit-based version of:
          //   context.node_base + (bit_index << context.values_per_bit_log_2));
          context.node_base | (bit_index << context.values_per_bit_log_2));
    }
  }
}

static void StartNewNormalNode(uint32_t cp, EncodeContext& context) {
  context.node_base = context.node_bases[context.next_node_base++];
  context.node_max = context.node_base + context.node_size - 1;
  context.filled_max = kInvalidCp;
  UpdateNodeBit(cp, context);
}

static void UpdateNormalNode(uint32_t cp, EncodeContext& context) {
  UpdateNodeBit(cp, context);
}

static EncodeState UpdateState(EncodeState state, const EncodeSymbol& input,
                               EncodeContext& context) {
  if (input.type == INVALID || state == ERROR || state == END) {
    return ERROR;
  }
  switch (state) {
    case START:
      switch (input.type) {
        case NEW_NORMAL_NODE:
          StartNewNormalNode(input.cp, context);
          return BUILDING_NORMAL_NODE;
        case NEW_FILLED_NODE:
          StartFilledNode(context);
          return SKIPPING_FILLED_NODE;
        case EXISTING_FILLED_NODE:
          SkipExistingFilledNode(input.cp, context);
          return SKIPPING_FILLED_NODE;
        default:
          return ERROR;
      }
    case BUILDING_NORMAL_NODE: {
      switch (input.type) {
        case NEW_NORMAL_NODE:
          EndNormalNode(context);
          StartNewNormalNode(input.cp, context);
          return BUILDING_NORMAL_NODE;
        case EXISTING_NORMAL_NODE:
          // Stay in state BUILDING_NORMAL_NODE.
          UpdateNormalNode(input.cp, context);
          return BUILDING_NORMAL_NODE;
        case NEW_FILLED_NODE:
          EndNormalNode(context);
          StartFilledNode(context);
          return SKIPPING_FILLED_NODE;
        case EXISTING_FILLED_NODE:
          EndNormalNode(context);
          SkipExistingFilledNode(input.cp, context);
          return SKIPPING_FILLED_NODE;
        case END_OF_VALUES:
          EndNormalNode(context);
          return END;
        default:
          return ERROR;
      }
    }
    case SKIPPING_FILLED_NODE:
      switch (input.type) {
        case NEW_NORMAL_NODE:
          StartNewNormalNode(input.cp, context);
          return BUILDING_NORMAL_NODE;
        case NEW_FILLED_NODE:
          // Stay in state SKIPPING_FILLED_NODE.
          StartFilledNode(context);
          return SKIPPING_FILLED_NODE;
        case EXISTING_FILLED_NODE:
          // Ignore value. Stay in state SKIPPING_FILLED_NODE.
          return SKIPPING_FILLED_NODE;
        case END_OF_VALUES:
          return END;
        default:
          return ERROR;
      }
    // Default case needed by Bazel.
    default:
      return ERROR;
  }
}

void EncodeLayer(const vector<uint32_t>& codepoints, uint32_t layer,
                 uint32_t tree_height, BranchFactor branch_factor,
                 const unordered_map<uint32_t, uint8_t>& filled_levels,
                 const vector<uint32_t>& node_bases,
                 vector<uint32_t>& next_node_bases, /* OUT */
                 BitOutputBuffer& bit_buffer /* OUT */) {
  uint8_t values_per_bit_log_2 =
      ValuesPerBitLog2ForLayer(layer, tree_height, branch_factor);
  uint64_t node_size = (uint64_t)kBFNodeSize[branch_factor]
                       << values_per_bit_log_2;
  EncodeContext context{
      layer,           branch_factor, tree_height, values_per_bit_log_2,
      node_size,       filled_levels, node_bases,  0,
      kInvalidCp,      kInvalidCp,    0u,          kInvalidCp,
      next_node_bases, bit_buffer};
  EncodeState state = START;
  EncodeSymbol input{INVALID, kInvalidCp};
  for (uint32_t cp : codepoints) {
    ParseCodepoint(cp, state, context, input);
    state = UpdateState(state, input, context);
  }
  UpdateState(state, kEndOfValues, context);
}

/*
 * Encodes the set as a sparse bit set with the given branch factor.
 * The fully filled twigs lists the twigs (1 level above leaves) that are
 * completely filled. For example, with BF4, a 1 in filled_twigs means that
 * values 16..31 are all present in the set.
 */
string EncodeSet(const vector<uint32_t>& codepoints, BranchFactor branch_factor,
                 const vector<uint32_t>& filled_twigs) {
  if (codepoints.empty()) {
    // One empty byte signifies an empty set.
    return string{0b00000000};
  }
  uint32_t tree_height = TreeDepthFor(codepoints, branch_factor);
  // Determine which nodes are completely filled; encode them with zero.
  unordered_map<uint32_t, uint8_t> filled_levels =
      FindFilledNodes(branch_factor, tree_height, filled_twigs);
  BitOutputBuffer bit_buffer(branch_factor, tree_height);

  // Starting values of the encoding ranges of the nodes queued to be encoded.
  // Queue up the root node.
  vector<uint32_t> node_bases(1, 0);
  vector<uint32_t> next_node_bases;
  for (uint32_t layer = 0; layer < tree_height; layer++) {
    EncodeLayer(codepoints, layer, tree_height, branch_factor, filled_levels,
                node_bases, next_node_bases, bit_buffer);
    if (next_node_bases.empty()) {
      break;  // Filled nodes mean nothing left to encode.
    }
    node_bases.swap(next_node_bases);
    next_node_bases.clear();
  }
  return bit_buffer.to_string();
}

string SparseBitSet::Encode(const IntSet& set, BranchFactor branch_factor) {
  uint32_t tree_depth =
      TreeDepthFor(set.max().value_or(HB_SET_VALUE_INVALID), branch_factor);
  if (tree_depth > kBFMaxDepth[branch_factor] && branch_factor == BF2) {
    // It's possible for uint32_t::MAX to exceed the max tree depth on BF2,
    // upgrade to 4 in that case.
    branch_factor = BF4;
  }

  if (set.empty()) {
    return string{0b00000000};
  }
  vector<hb_codepoint_t> codepoints = set.to_vector();
  vector<uint32_t> filled_twigs;
  FindFilledTwigs(codepoints, branch_factor, filled_twigs);
  return EncodeSet(codepoints, branch_factor, filled_twigs);
}

string SparseBitSet::Encode(const IntSet& set) {
  if (set.empty()) {
    return "";
  }
  vector<hb_codepoint_t> codepoints = set.to_vector();
  vector<uint32_t> filled_twigs;
  BranchFactor branch_factor = ChooseBranchFactor(codepoints, filled_twigs);

  return EncodeSet(codepoints, branch_factor, filled_twigs);
}

}  // namespace common
