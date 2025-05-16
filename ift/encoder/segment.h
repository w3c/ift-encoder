#ifndef IFT_ENCODER_SEGMENT_H_
#define IFT_ENCODER_SEGMENT_H_

#include "absl/container/btree_set.h"
#include "common/int_set.h"
#include "hb-subset.h"

class Segment {
 public:
  Segment() {}
  Segment(std::initializer_list<hb_codepoint_t> values)
      : codepoints_(values), features_() {}

  const common::CodepointSet& Codepoints() const { return codepoints_; }

  void AddCodepoint(hb_codepoint_t cp) { codepoints_.insert(cp); }

  bool Empty() const { return codepoints_.empty() && features_.empty(); }

  void Union(const Segment& other) {
    codepoints_.union_set(other.codepoints_);
    features_.insert(other.features_.begin(), other.features_.end());
  }

  void Subtract(const Segment& other) {
    codepoints_.subtract(other.codepoints_);
    for (hb_tag_t tag : other.features_) {
      features_.erase(tag);
    }
  }

  void Clear() {
    codepoints_.clear();
    features_.clear();
  }

  void AddToSubsetInput(hb_subset_input_t* input) const {
    codepoints_.union_into(hb_subset_input_unicode_set(input));
    for (hb_tag_t tag : features_) {
      hb_set_add(hb_subset_input_set(input, HB_SUBSET_SETS_LAYOUT_FEATURE_TAG),
                 tag);
    }
  }

  uint32_t CodepointsSize() const { return codepoints_.size(); }

  bool operator==(const Segment& other) const {
    return codepoints_ == other.codepoints_ && features_ == other.features_;
  }

  template <typename H>
  friend H AbslHashValue(H h, const Segment& set) {
    return H::combine(std::move(h), set.codepoints_, set.features_);
  }

 private:
  common::CodepointSet codepoints_;
  absl::btree_set<hb_tag_t> features_;
};

#endif  // IFT_ENCODER_SEGMENT_H_