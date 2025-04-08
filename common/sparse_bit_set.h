#ifndef COMMON_SPARSE_BIT_SET_H_
#define COMMON_SPARSE_BIT_SET_H_

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/branch_factor.h"
#include "common/int_set.h"

namespace common {

/*
 * A read only data structure which represents a set of non-negative integers
 * using a bit set tree. This gives the compactness of a bit set, but needs far
 * less bytes when dealing with a set that has large gaps between members.
 *
 * Each single byte is a node in the tree. Each bit in the byte indicates if
 * there is a child node at that position. For leaf nodes bits are used to
 * indicate that the corresponding value is present in the set.
 *
 * To illustrate we can represent the set {2, 63} with a tree of depth 2
 * using 3 bytes.
 *
 * Layer 1: [1, 0, 0, 0, 0, 0, 0, 1] (byte 0) This tells us there are two
 * children in the next layer. Since this is a two layer tree each bit points to
 * a child node that can contain 8 possible values. In this example the two
 * nodes cover the range 0-8 and 56 - 63.
 *
 * Layer 2: [0, 0, 0, 0, 0, 0, 1, 0] (byte 1) -> tells us we have the value
 * '2'. [1, 0, 0, 0, 0, 0, 0, 0] (byte 2) -> tells us we have the value '63'.
 *
 * A traditional bit set would have required 8 bytes to represent the same
 * set.
 *
 * Another example would be storing a set of unicode code points where you
 * have members from the Ascii set (0-128) and from a CJK block (0x3000+). With
 * a traditional bit set you would need to store zeroed out arrays for all of
 * the potential code point between 123 and 0x3000 (approx 1500 bytes). This set
 * will avoid needing to store those bytes at the cost of a few overhead bytes
 * (~7) for the tree.
 *
 * This data structure optimizes only for a minimal number of bytes needed to
 * represent the set. As such random access reads/writes are not supported. A
 * sparse bit set must be fully encoded/decoded to/from another set
 * representation.
 */
class SparseBitSet {
 public:
  // Decode a SparseBitSet binary blob into an actual set. The decoded set
  // items are appended to any existing items in out. Returns a sub string of
  // 'sparse_bit_set' with the consumed bytes removed.
  static absl::StatusOr<absl::string_view> Decode(
      absl::string_view sparse_bit_set, IntSet& out);

  // Encode a set of integers into a sparse bit set binary blob.
  static std::string Encode(const common::IntSet& set,
                            BranchFactor branch_factor);
  /*
   * Encode a set of integers into a sparse bit set binary blob.
   * The optimal branch_factor will be estimated and used automatically.
   */
  static std::string Encode(const common::IntSet& set);
};

}  // namespace common

#endif  // COMMON_SPARSE_BIT_SET_H_
