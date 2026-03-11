#ifndef COMMON_BRANCH_FACTOR_H_
#define COMMON_BRANCH_FACTOR_H_

#include <stdint.h>

namespace ift::common {

enum BranchFactor { BF2, BF4, BF8, BF32 };

// How many children does node have?
const uint32_t kBFNodeSize[]{2, 4, 8, 32};

// How many children of each node, but log base 2.
// For use when bit shifting to multiply or divide by node size.
const uint32_t kBFNodeSizeLog2[]{1, 2, 3, 5};

// How many values are covered by a "Twig" - 1 layer above a leaf node.
const uint32_t kBFTwigSize[]{
    kBFNodeSize[BF2] * kBFNodeSize[BF2], kBFNodeSize[BF4] * kBFNodeSize[BF4],
    kBFNodeSize[BF8] * kBFNodeSize[BF8], kBFNodeSize[BF32] * kBFNodeSize[BF32]};

// How many values are covered by a "Twig" - 1 layer above a leaf node, but log
// base 2. For use when bit shifting to multiply or divide by twig size.
const uint32_t kBFTwigSizeLog2[]{
    kBFNodeSizeLog2[BF2] * 2, kBFNodeSizeLog2[BF4] * 2,
    kBFNodeSizeLog2[BF8] * 2, kBFNodeSizeLog2[BF32] * 2};

// Bit masks that cover the bits needed to represent the node size.
// For example, BF8 needs 3 bits to represent values 0..7, the size of one node.
// This can be used to compute modulus arithmetic, for example:
//   uint32_t remainder8 = n & kBFNodeSizeBitMask[BF8];
const uint32_t kBFNodeSizeBitMask[]{0b1, 0b11, 0b111, 0b11111};

// Bit masks that cover the bits needed to represent the twig size.
const uint32_t kBFTwigSizeBitMask[]{0b11, 0b1111, 0b111111, 0b1111111111};

// Max depth of trees. Enough to encode the entire 32 bit range 0..0xFFFFFFFF.
const uint32_t kBFMaxDepth[]{31, 16, 11, 7};

}  // namespace ift::common

#endif  // COMMON_BRANCH_FACTOR_H_
