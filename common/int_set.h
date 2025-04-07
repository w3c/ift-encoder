#ifndef COMMON_INT_SET
#define COMMON_INT_SET

#include <optional>

#include "common/hb_set_unique_ptr.h"
namespace common {

class IntSet;

class IntSetIterator {
 public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = hb_codepoint_t;
  using difference_type = std::ptrdiff_t;
  using pointer = const hb_codepoint_t*;
  using reference = hb_codepoint_t;

 private:
  friend class IntSet;

  const hb_set_t* set_ = nullptr;  // nullptr signals we are at the end.
  hb_codepoint_t current_codepoint_ = HB_SET_VALUE_INVALID;

  IntSetIterator(const hb_set_t* set)
      : set_(set), current_codepoint_(HB_SET_VALUE_INVALID) {
    // c++ iterators start on the first element, so advance the iterator one
    // element.
    ++(*this);
  }

 public:
  IntSetIterator() = default;

  reference operator*() const { return current_codepoint_; }

  IntSetIterator& operator++() {
    if (set_) {
      if (!hb_set_next(set_, &current_codepoint_)) {
        // Reached the end
        set_ = nullptr;
        current_codepoint_ = HB_CODEPOINT_INVALID;
      }
    }
    return *this;
  }

  IntSetIterator operator++(int) {
    IntSetIterator temp = *this;
    ++(*this);
    return temp;
  }

  friend bool operator==(const IntSetIterator& a, const IntSetIterator& b) {
    return a.set_ == b.set_ && a.current_codepoint_ == b.current_codepoint_;
  }

  friend bool operator!=(const IntSetIterator& a, const IntSetIterator& b) {
    return !(a == b);
  }
};

/**
 * Wrapper around a harfbuzz hb_set_t*.
 *
 * Makes it act like a more typical c++ container class and provides
 * hashing/comparison needed to store the set inside of other container types.
 */
class IntSet {
 public:
  using iterator = IntSetIterator;
  using const_iterator = IntSetIterator;

  IntSet() : set_(make_hb_set()) {}

  IntSet(std::initializer_list<hb_codepoint_t> values) : set_(make_hb_set()) {
    for (auto v : values) {
      this->add(v);
    }
  }

  IntSet(const hb_set_t* set) : set_(make_hb_set()) {
    // We always keep exclusive ownership of the internal set, so copy the
    // contents of the input set instead of referencing it.
    hb_set_union(set_.get(), set);
  }

  IntSet(const hb_set_unique_ptr& set) : set_(make_hb_set()) {
    // We always keep exclusive ownership of the internal set, so copy the
    // contents of the input set instead of referencing it.
    hb_set_union(set_.get(), set.get());
  }

  IntSet(const IntSet& other) : set_(make_hb_set()) {
    hb_set_union(set_.get(), other.set_.get());
  }

  IntSet& operator=(const IntSet& other) {
    hb_set_clear(set_.get());
    hb_set_union(set_.get(), other.set_.get());
    return *this;
  }

  IntSet(IntSet&& other) noexcept : set_(make_hb_set()) {
    // swap pointers so that the moved set is still in a valid state.
    this->set_.swap(other.set_);
  }

  IntSet& operator=(IntSet&& other) noexcept {
    // swap pointers so that the moved set is still in a valid state.
    this->set_.swap(other.set_);
    return *this;
  }

  bool operator==(const IntSet& other) const {
    return hb_set_is_equal(this->set_.get(), other.set_.get());
  }

  bool operator!=(const IntSet& other) const { return !(*this == other); }

  bool operator<(const IntSet& other) const {
    auto a = this->begin();
    auto b = other.begin();

    while (a != this->end() && b != this->end()) {
      if (*a < *b) {
        return true;
      } else if (*a > *b) {
        return false;
      }
      // otherwise elements are equal, check the next
      ++a;
      ++b;
    }

    if (a == b) {
      // sets are equal
      return false;
    }

    // Otherwise only one of a or b is at the end, the shorter set comes first
    return a == this->end();
  }

  template <typename H>
  friend H AbslHashValue(H h, const IntSet& set) {
    // Utilize the existing harfbuzz hashing function.
    unsigned harfbuzz_hash = hb_set_hash(set.set_.get());
    return H::combine(std::move(h), harfbuzz_hash);
  }

  iterator begin() { return iterator(set_.get()); }

  iterator end() { return iterator(); }

  // const versions simply return the same iterator type
  const_iterator begin() const { return const_iterator(set_.get()); }

  const_iterator end() const { return const_iterator(); }

  // Provide cbegin() and cend() explicitly
  const_iterator cbegin() const {
    return begin();  // Calls const begin()
  }

  const_iterator cend() const {
    return end();  // Calls const end()
  }

  void add(hb_codepoint_t codepoint) { hb_set_add(set_.get(), codepoint); }

  void add_range(hb_codepoint_t start, hb_codepoint_t end) {
    hb_set_add_range(set_.get(), start, end);
  }

  bool contains(hb_codepoint_t codepoint) const {
    return hb_set_has(set_.get(), codepoint);
  }

  bool is_subset_of(const IntSet& other) const {
    return hb_set_is_subset(set_.get(), other.set_.get());
  }

  std::optional<hb_codepoint_t> min() const {
    hb_codepoint_t value = hb_set_get_min(set_.get());
    if (value == HB_SET_VALUE_INVALID) {
      return std::nullopt;
    }
    return value;
  }

  std::optional<hb_codepoint_t> max() const {
    hb_codepoint_t value = hb_set_get_max(set_.get());
    if (value == HB_SET_VALUE_INVALID) {
      return std::nullopt;
    }
    return value;
  }

  void erase(hb_codepoint_t codepoint) { hb_set_del(set_.get(), codepoint); }

  size_t size() const { return hb_set_get_population(set_.get()); }

  bool empty() const { return hb_set_is_empty(set_.get()); }

  // Removes all elements
  void clear() { hb_set_clear(set_.get()); }

  // Compute the union of this and other, store the result in this set.
  void union_set(const IntSet& other) {
    hb_set_union(set_.get(), other.set_.get());
  }

  // Compute the intersection of this and other, store the result in this set.
  void intersect(const IntSet& other) {
    hb_set_intersect(set_.get(), other.set_.get());
  }

  // Subtract other from this set.
  void subtract(const IntSet& other) {
    hb_set_subtract(set_.get(), other.set_.get());
  }

  // Compute the symmetric difference of this and other, store the result in
  // this set.
  void symmetric_difference(const IntSet& other) {
    hb_set_symmetric_difference(set_.get(), other.set_.get());
  }

 private:
  // Note: set_ must always point to a valid set object. nullptr is not allowed.
  // Note: we always retain exclusive ownership over set_. Normally hb_set_t*
  // can be
  //       shared (via hb_set_reference()), but for this container we don't ever
  //       expose the underlying hb_set_t* pointer so that we know we're always
  //       the only owner. This prevents the sets contents from being changed
  //       outside of this class.
  hb_set_unique_ptr set_;
};

}  // namespace common

#endif