#include "common/int_set.h"

#include <optional>

#include "absl/hash/hash_testing.h"
#include "common/hb_set_unique_ptr.h"
#include "gtest/gtest.h"

namespace common {

class IntSetTest : public ::testing::Test {};

TEST_F(IntSetTest, BasicOperations) {
  IntSet set;
  ASSERT_TRUE(set.empty());

  set.insert(5);
  set.insert(7);
  set.insert(7);
  set.insert(8);

  ASSERT_FALSE(set.contains(4));
  ASSERT_TRUE(set.contains(5));
  ASSERT_FALSE(set.contains(6));
  ASSERT_TRUE(set.contains(7));
  ASSERT_TRUE(set.contains(8));

  ASSERT_EQ(set.size(), 3);
  ASSERT_FALSE(set.empty());

  set.erase(4);
  ASSERT_EQ(set.size(), 3);
  ASSERT_FALSE(set.empty());

  set.erase(7);
  ASSERT_EQ(set.size(), 2);
  ASSERT_FALSE(set.empty());
  ASSERT_FALSE(set.contains(7));
}

TEST_F(IntSetTest, Equality) {
  IntSet a{1, 2, 1000};
  IntSet b{1, 1000, 2};
  IntSet c{1, 2, 1000, 1001};

  ASSERT_TRUE(a == a);
  ASSERT_FALSE(a != a);

  ASSERT_TRUE(a == b);
  ASSERT_FALSE(a != b);

  ASSERT_FALSE(a == c);
  ASSERT_TRUE(a != c);

  ASSERT_FALSE(b == c);
  ASSERT_TRUE(b != c);
}

TEST_F(IntSetTest, LessThan) {
  // These are in the appropriate sorted order
  IntSet empty;
  IntSet a{7, 8};
  IntSet b{7, 8, 11};
  IntSet c{7, 8, 12};
  IntSet d{8, 11};

  // Self comparisons
  ASSERT_FALSE(empty < empty);
  ASSERT_FALSE(a < a);

  // Ordering
  ASSERT_TRUE(a < b);
  ASSERT_TRUE(b < c);
  ASSERT_TRUE(c < d);

  ASSERT_FALSE(b < a);
  ASSERT_FALSE(c < b);
  ASSERT_FALSE(d < c);
}

TEST_F(IntSetTest, InitList) {
  IntSet set{10, 1000};
  ASSERT_TRUE(set.contains(10));
  ASSERT_FALSE(set.contains(100));
  ASSERT_TRUE(set.contains(1000));
}

TEST_F(IntSetTest, Move) {
  IntSet a{10, 1000};

  IntSet b(std::move(a));

  ASSERT_TRUE(b.contains(10));
  ASSERT_FALSE(b.contains(100));
  ASSERT_TRUE(b.contains(1000));

  // We gaurantee moved values remain valid after a move.
  ASSERT_EQ(a.size(), 0);

  a = std::move(b);

  ASSERT_TRUE(a.contains(10));
  ASSERT_FALSE(a.contains(100));
  ASSERT_TRUE(a.contains(1000));

  // We gaurantee moved values remain valid after a move.
  ASSERT_EQ(b.size(), 0);
}

TEST_F(IntSetTest, CopyConstructor) {
  IntSet a{13, 47};
  IntSet b(a);

  ASSERT_EQ(a, b);
  ASSERT_TRUE(a.contains(13));
  ASSERT_TRUE(a.contains(47));

  ASSERT_TRUE(b.contains(13));
  ASSERT_TRUE(b.contains(47));
}

TEST_F(IntSetTest, CopyHbSet) {
  hb_set_unique_ptr hb_set = make_hb_set(2, 13, 47);

  IntSet a(hb_set.get());
  IntSet b(hb_set);

  // Make sure chaing hb_set doesn't cause changes in the IntSet's
  hb_set_add(hb_set.get(), 49);

  IntSet expected{13, 47};

  ASSERT_EQ(a, expected);
  ASSERT_EQ(b, expected);

  ASSERT_TRUE(a.contains(13));
  ASSERT_TRUE(a.contains(47));
  ASSERT_FALSE(a.contains(49));

  ASSERT_TRUE(b.contains(13));
  ASSERT_TRUE(b.contains(47));
  ASSERT_FALSE(b.contains(49));
}

TEST_F(IntSetTest, Assignment) {
  IntSet a{13, 47};
  IntSet b{5, 9};

  b = a;

  ASSERT_EQ(a, b);
  ASSERT_TRUE(a.contains(13));
  ASSERT_TRUE(a.contains(47));

  ASSERT_TRUE(b.contains(13));
  ASSERT_TRUE(b.contains(47));
}

TEST_F(IntSetTest, EmptySetIteration) {
  IntSet empty;
  ASSERT_EQ(empty.begin(), empty.end());
}

TEST_F(IntSetTest, BasicIteration) {
  IntSet set{7, 9, 10};
  auto it = set.begin();

  ASSERT_NE(it, set.end());
  ASSERT_EQ(*it, 7);
  ASSERT_EQ(*it++, 7);
  ASSERT_EQ(*it, 9);
  ASSERT_EQ(*(++it), 10);
  ASSERT_EQ(*it, 10);
  ++it;
  ASSERT_EQ(it, set.end());
}

TEST_F(IntSetTest, ForLoop) {
  IntSet set{7, 9, 10};
  int expected[] = {7, 9, 10};

  int index = 0;
  for (auto v : set) {
    ASSERT_EQ(v, expected[index++]);
  }
}

TEST_F(IntSetTest, UseInBtreeSet) {
  absl::btree_set<IntSet> sets{{7, 8, 11}, {7, 8}, {7, 8, 12}, {}};

  IntSet empty{};
  IntSet a{7, 8};
  IntSet b{7, 8, 11};
  IntSet c{7, 8, 12};
  IntSet d{8, 11};

  ASSERT_TRUE(sets.contains(a));
  ASSERT_TRUE(sets.contains(b));
  ASSERT_TRUE(sets.contains(c));
  ASSERT_FALSE(sets.contains(d));

  auto it = sets.begin();
  ASSERT_EQ(*it, empty);
}

TEST_F(IntSetTest, UseInHashSet) {
  absl::flat_hash_set<IntSet> sets{{7, 8, 11}, {7, 8}, {7, 8, 12}, {}};

  IntSet empty{};
  IntSet a{7, 8};
  IntSet b{7, 8, 11};
  IntSet c{7, 8, 12};
  IntSet d{8, 11};

  ASSERT_TRUE(sets.contains(empty));
  ASSERT_TRUE(sets.contains(a));
  ASSERT_TRUE(sets.contains(b));
  ASSERT_TRUE(sets.contains(c));
  ASSERT_FALSE(sets.contains(d));
}

TEST_F(IntSetTest, SupportsAbslHash) {
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({
      IntSet{},
      IntSet{7, 8},
      IntSet{7, 8, 11},
      IntSet{7, 8, 12},
      IntSet{8, 11},
      IntSet{7, 8, 12},
  }));
}

TEST_F(IntSetTest, MinMax) {
  IntSet empty{};
  IntSet a{8};
  IntSet b{7, 8, 11};

  ASSERT_EQ(empty.min(), std::nullopt);
  ASSERT_EQ(empty.max(), std::nullopt);

  ASSERT_EQ(*a.min(), 8);
  ASSERT_EQ(*a.max(), 8);

  ASSERT_EQ(*b.min(), 7);
  ASSERT_EQ(*b.max(), 11);
}

TEST_F(IntSetTest, InsertRange) {
  IntSet a{7, 8, 11};

  a.insert_range(10, 15);
  IntSet expected{7, 8, 10, 11, 12, 13, 14, 15};

  ASSERT_EQ(a, expected);
}

TEST_F(IntSetTest, InsertIterator) {
  IntSet a{7, 8, 11};

  std::vector b{5, 15, 21};

  a.insert(b.begin(), b.end());
  IntSet expected{5, 7, 8, 11, 15, 21};

  ASSERT_EQ(a, expected);
}

TEST_F(IntSetTest, IsSubsetOf) {
  IntSet empty;
  IntSet a{7, 8};
  IntSet b{7, 8, 11};

  ASSERT_TRUE(empty.is_subset_of(a));
  ASSERT_TRUE(empty.is_subset_of(b));

  ASSERT_FALSE(a.is_subset_of(empty));
  ASSERT_FALSE(b.is_subset_of(empty));

  ASSERT_TRUE(a.is_subset_of(b));
  ASSERT_FALSE(b.is_subset_of(a));

  ASSERT_TRUE(a.is_subset_of(a));
  ASSERT_TRUE(b.is_subset_of(b));
}

TEST_F(IntSetTest, Union) {
  IntSet a{5, 8};
  IntSet b{8, 11};
  IntSet expected{5, 8, 11};

  a.union_set(b);

  ASSERT_EQ(a, expected);

  hb_set_unique_ptr c = make_hb_set(1, 7);

  b.union_into(c.get());

  ASSERT_TRUE(hb_set_has(c.get(), 7));
  ASSERT_TRUE(hb_set_has(c.get(), 8));
  ASSERT_TRUE(hb_set_has(c.get(), 11));
  ASSERT_EQ(hb_set_get_population(c.get()), 3);
}

TEST_F(IntSetTest, Intersect) {
  IntSet a{5, 8};
  IntSet b{8, 11};
  IntSet expected{8};

  a.intersect(b);

  ASSERT_EQ(a, expected);
}

TEST_F(IntSetTest, Subtract) {
  IntSet a{5, 8};
  IntSet b{8, 11};
  IntSet expected{5};

  a.subtract(b);

  ASSERT_EQ(a, expected);
}

TEST_F(IntSetTest, SymmetricDifference) {
  IntSet a{5, 8};
  IntSet b{8, 11};
  IntSet expected{5, 11};

  a.symmetric_difference(b);

  ASSERT_EQ(a, expected);
}

}  // namespace common