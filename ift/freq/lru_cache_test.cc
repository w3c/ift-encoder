#include "ift/freq/lru_cache.h"

#include <string>

#include "gtest/gtest.h"

namespace ift::freq {
namespace {

TEST(LruCacheTest, BasicPutAndGet) {
  LruCache<std::string, int> cache("test", 3);

  int& val_a = cache["a"];
  EXPECT_EQ(val_a, 0);  // operator[] inserts default value (0 for int)
  val_a = 1;

  int& val_b = cache["b"];
  EXPECT_EQ(val_b, 0);
  val_b = 2;

  int& val_c = cache["c"];
  EXPECT_EQ(val_c, 0);
  val_c = 3;

  EXPECT_EQ(cache.size(), 3);

  EXPECT_EQ(cache["a"], 1);
  EXPECT_EQ(cache["b"], 2);
  EXPECT_EQ(cache["c"], 3);
}

TEST(LruCacheTest, Eviction) {
  LruCache<std::string, int> cache("test", 3);

  cache["a"] = 1;
  cache["b"] = 2;
  cache["c"] = 3;

  // Evict "a" (LRU) when accessing "d"
  cache["d"];
  EXPECT_EQ(cache.size(), 3);

  // "a" should be evicted, so accessing it now should insert default value (0)
  EXPECT_EQ(cache["a"], 0);  // will evict "b"
  EXPECT_EQ(cache["b"], 0);
  EXPECT_EQ(cache.size(), 3);
}

TEST(LruCacheTest, Clear) {
  LruCache<std::string, int> cache("test", 3);

  cache["a"] = 1;
  cache["b"] = 2;
  cache.clear();

  EXPECT_EQ(cache.size(), 0);
  EXPECT_EQ(cache["a"], 0);
  EXPECT_EQ(cache.size(), 1);
}

// Used to track copy's in the no copy test.
struct MockKey {
  std::string val;
  mutable int copy_count = 0;

  MockKey(std::string v) : val(std::move(v)) {}
  MockKey(const MockKey& o) : val(o.val), copy_count(o.copy_count + 1) {
    o.copy_count++;
  }

  MockKey(MockKey&& o) noexcept
      : val(std::move(o.val)), copy_count(o.copy_count) {}

  bool operator==(const MockKey& o) const { return val == o.val; }

  template <typename H>
  friend H AbslHashValue(H h, const MockKey& k) {
    return H::combine(std::move(h), k.val);
  }
};

TEST(LruCacheTest, NoKeyCopyOnHit) {
  LruCache<MockKey, int> cache("test", 3);

  MockKey key1("a");
  cache[key1] = 1;  // Miss, inserts and copies key1.
  EXPECT_EQ(key1.copy_count, 1);

  // Now hit it.
  int& val = cache[key1];
  EXPECT_EQ(val, 1);

  // Accessing existing key should not copy it.
  EXPECT_EQ(key1.copy_count, 1);
}

}  // namespace
}  // namespace ift::freq
