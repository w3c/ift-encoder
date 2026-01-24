#ifndef COMMON_INT_SET
#define COMMON_INT_SET

#include <cstddef>
#include <optional>
#include <sstream>
#include <vector>

#include "absl/types/span.h"
#include "common/hb_set_unique_ptr.h"
namespace common {

class IntSet;

template <bool reverse>
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

  IntSetIterator(const hb_set_t* set,
                 hb_codepoint_t current_codepoint = HB_SET_VALUE_INVALID)
      : set_(set), current_codepoint_(current_codepoint) {
    // c++ iterators start on the first element, so advance the iterator one
    // element.
    ++(*this);
  }

 public:
  IntSetIterator() = default;

  reference operator*() const { return current_codepoint_; }

  IntSetIterator& operator++() {
    if (set_) {
      if (!(!reverse ? hb_set_next(set_, &current_codepoint_)
                     : hb_set_previous(set_, &current_codepoint_))) {
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
  using iterator = IntSetIterator<false>;
  using rev_iterator = IntSetIterator<true>;
  using const_iterator = IntSetIterator<false>;
  using const_rev_iterator = IntSetIterator<true>;

  IntSet() : set_(make_hb_set()) {}

  IntSet(std::initializer_list<hb_codepoint_t> values) : set_(make_hb_set()) {
    for (auto v : values) {
      this->insert(v);
    }
  }

  explicit IntSet(const hb_set_t* set) : set_(make_hb_set()) {
    // We always keep exclusive ownership of the internal set, so copy the
    // contents of the input set instead of referencing it.
    hb_set_union(set_.get(), set);
  }

  explicit IntSet(const hb_set_unique_ptr& set) : set_(make_hb_set()) {
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

  // Reverse iterators
  rev_iterator rbegin() { return rev_iterator(set_.get()); }

  rev_iterator rend() { return rev_iterator(); }

  // const versions simply return the same iterator type
  const_rev_iterator rbegin() const { return const_rev_iterator(set_.get()); }

  const_rev_iterator rend() const { return const_rev_iterator(); }

  // Iterator over values in the set that are equal to or greater than start.
  const_iterator lower_bound(hb_codepoint_t start) const {
    if (start > 0) {
      start -= 1;
    } else {
      start = HB_SET_VALUE_INVALID;
    }
    return const_iterator(set_.get(), start);
  }

  void insert(hb_codepoint_t codepoint) { hb_set_add(set_.get(), codepoint); }

  void insert_range(hb_codepoint_t start, hb_codepoint_t end) {
    hb_set_add_range(set_.get(), start, end);
  }

  // Optimized insert that takes an array of sorted values
  void insert_sorted_array(absl::Span<const hb_codepoint_t> sorted_values) {
    hb_set_add_sorted_array(set_.get(), sorted_values.data(),
                            sorted_values.size());
  }

  std::vector<hb_codepoint_t> to_vector() const {
    std::vector<hb_codepoint_t> values;
    auto size = this->size();
    values.resize(size);
    hb_set_next_many(set_.get(), HB_SET_VALUE_INVALID, values.data(), size);
    return values;
  }

  template <typename It>
  void insert(It start, It end) {
    while (start != end) {
      insert(*start);
      ++start;
    }
  }

  bool contains(hb_codepoint_t codepoint) const {
    return hb_set_has(set_.get(), codepoint);
  }

  bool is_subset_of(const IntSet& other) const {
    return hb_set_is_subset(set_.get(), other.set_.get());
  }

  bool intersects(const IntSet& other) const {
    if (this->size() > other.size()) {
      return other.intersects(*this);
    }
    for (const hb_codepoint_t value : *this) {
      if (other.contains(value)) {
        return true;
      }
    }
    return false;
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

  size_t erase(hb_codepoint_t codepoint) {
    bool has = contains(codepoint);
    if (has) {
      hb_set_del(set_.get(), codepoint);
      return 1;
    }
    return 0;
  }

  size_t size() const { return hb_set_get_population(set_.get()); }

  bool empty() const { return hb_set_is_empty(set_.get()); }

  // Removes all elements
  void clear() { hb_set_clear(set_.get()); }

  // Compute the union of this and other, store the result in this set.
  void union_set(const IntSet& other) {
    hb_set_union(set_.get(), other.set_.get());
  }

  void union_into(hb_set_t* other) const { hb_set_union(other, set_.get()); }

  void union_from(hb_set_t* other) const { hb_set_union(set_.get(), other); }

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

  void invert() { hb_set_invert(set_.get()); }

  std::string ToString() const {
    std::stringstream out;

    out << "{";

    bool first = true;
    for (uint32_t v : *this) {
      if (!first) {
        out << ", ";
      }
      first = false;
      out << v;
    }

    out << "}";
    return out.str();
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

// Typed variants
class GlyphSet : public IntSet {
 public:
  GlyphSet() : IntSet(){};
  GlyphSet(std::initializer_list<hb_codepoint_t> values) : IntSet(values) {}
  explicit GlyphSet(const hb_set_t* set) : IntSet(set) {}
  explicit GlyphSet(const hb_set_unique_ptr& set) : IntSet(set) {}
};

class CodepointSet : public IntSet {
 public:
  CodepointSet() : IntSet(){};
  CodepointSet(std::initializer_list<hb_codepoint_t> values) : IntSet(values) {}
  explicit CodepointSet(const hb_set_t* set) : IntSet(set) {}
  explicit CodepointSet(const hb_set_unique_ptr& set) : IntSet(set) {}
};

class SegmentSet : public IntSet {
 public:
  SegmentSet() : IntSet(){};
  SegmentSet(std::initializer_list<hb_codepoint_t> values) : IntSet(values) {}
  explicit SegmentSet(const hb_set_t* set) : IntSet(set) {}
  explicit SegmentSet(const hb_set_unique_ptr& set) : IntSet(set) {}

  static SegmentSet all() {
    SegmentSet all;
    all.invert();
    return all;
  }
};

}  // namespace common

#endif