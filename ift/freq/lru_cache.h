#ifndef IFT_FREQ_LRU_CACHE_H_
#define IFT_FREQ_LRU_CACHE_H_

#include <list>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"

namespace ift::freq {

// A simple LRU cache from Key to Value.
template <typename Key, typename Value>
class LruCache {
 public:
  explicit LruCache(absl::string_view name, size_t max_size)
      : max_size_(max_size), name_(name) {}

  // Move only
  LruCache(const LruCache&) = delete;
  LruCache& operator=(const LruCache&) = delete;
  LruCache(LruCache&&) = default;
  LruCache& operator=(LruCache&&) = default;

  // Lookups up the key in the cache and returns a reference to the existing
  // value entry, or inserts a new default value and returns reference if one
  // doesn't exist yet.
  //
  // Moves the accessed/inserted item to the front of the LRU list.
  Value& operator[](const Key& key) {
    if (total_count_++ % 500000 == 0) {
      log_stats();
    }
    auto it = map_.find(&key);
    if (it != map_.end()) {
      hit_count_++;
      const auto& key_it = it->second;
      list_.splice(list_.begin(), list_, key_it);
      return key_it->second;
    }

    if (list_.size() >= max_size_) {
      const Key& last_key = list_.back().first;
      map_.erase(&last_key);
      list_.pop_back();
      eviction_count_++;
    }

    list_.emplace_front(key, Value());
    map_[&list_.front().first] = list_.begin();
    return list_.front().second;
  }

  void clear() {
    map_.clear();
    list_.clear();
  }

  size_t size() const { return map_.size(); }
  size_t max_size() const { return max_size_; }

 private:
  void log_stats() const {
    VLOG(1) << name_ << " cache hit % = "
            << ((double)hit_count_ / (double)(total_count_)) * 100.0
            << ", evictions = " << eviction_count_;
  }

  struct HashPtr {
    size_t operator()(const Key* k) const { return absl::Hash<Key>()(*k); }
  };
  struct EqPtr {
    bool operator()(const Key* a, const Key* b) const { return *a == *b; }
  };

  size_t max_size_;
  std::list<std::pair<Key, Value>> list_;
  absl::flat_hash_map<const Key*,
                      typename std::list<std::pair<Key, Value>>::iterator,
                      HashPtr, EqPtr>
      map_;

  std::string name_;
  uint64_t hit_count_ = 0;
  uint64_t total_count_ = 0;
  uint64_t eviction_count_ = 0;
};

}  // namespace ift::freq

#endif  // IFT_FREQ_LRU_CACHE_H_
