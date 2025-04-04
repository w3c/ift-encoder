#ifndef COMMON_INT_SET
#define COMMON_INT_SET

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

  // TODO(garretrieger): construct from hb_set_t* or hb_set_unique_ptr.
  // TODO(garretrieger): copy assignment operator

  IntSet(const IntSet& other) : set_(make_hb_set()) {
    hb_set_union(set_.get(), other.set_.get());
  }

  IntSet& operator=(const IntSet&) =
      delete;  // TODO(garretrieger): implement this.

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

  // TODO(garretrieger): add absl hashing support so we can use these in
  // hash_sets/maps

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

  bool contains(hb_codepoint_t codepoint) const {
    return hb_set_has(set_.get(), codepoint);
  }

  void erase(hb_codepoint_t codepoint) { hb_set_del(set_.get(), codepoint); }

  size_t size() const { return hb_set_get_population(set_.get()); }

  bool empty() const { return hb_set_is_empty(set_.get()); }

  // Remove all elements
  void clear() { hb_set_clear(set_.get()); }

 private:
  // Note: set_ must always point to a valid set object. nullptr is not allowed.
  hb_set_unique_ptr set_;
};

}  // namespace common

#endif