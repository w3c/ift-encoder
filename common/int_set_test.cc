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

// TODO(garretrieger): test use in hash and btree, sets and maps.
// TODO(garretrieger): test various operators (<, >, ==, !=)

}  // namespace common