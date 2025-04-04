#include "common/int_set.h"

#include "gtest/gtest.h"

namespace common {

class IntSetTest : public ::testing::Test {};

TEST_F(IntSetTest, BasicOperations) {
  IntSet set;
  ASSERT_TRUE(set.empty());

  set.add(5);
  set.add(7);
  set.add(7);
  set.add(8);

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

// TODO(garretrieger): test use in hash and btree, sets and maps.
// TODO(garretrieger): test various operators (<, >, ==, !=)

}  // namespace common