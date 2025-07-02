#ifndef IFT_ENCODER_ACTIVATION_CONDITION_H_
#define IFT_ENCODER_ACTIVATION_CONDITION_H_

#include "common/int_set.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"
#include "ift/proto/patch_map.h"
#include "util/segmentation_plan.pb.h"

namespace ift::encoder {

/*
 * The conditions under which a patch should be laoded.
 */
class ActivationCondition {
 public:
  /*
   * Constructs a condition that activates when the input intersects(patch_1)
   * AND ... AND inersects(patch_n).
   */
  static ActivationCondition exclusive_segment(segment_index_t index,
                                               patch_id_t activated);

  /*
   * Constructs a condition that activates when the input intersects(patch_1)
   * AND ... AND inersects(segment_n).
   */
  static ActivationCondition and_segments(const common::SegmentSet& ids,
                                          patch_id_t activated);

  /*
   * Constructs a condition that activates when the input intersects
   * (segment_1) OR ... OR inersects(segment_n).
   */
  static ActivationCondition or_segments(const common::SegmentSet& ids,
                                         patch_id_t activated,
                                         bool is_fallback = false);

  /*
   * Constructs a condition that activates when the input intersects:
   * (s1 OR ..) AND (si OR ...) AND ...
   */
  static ActivationCondition composite_condition(
      absl::Span<const common::SegmentSet> groups, patch_id_t activated);

  /*
   * Converts a list of activation conditions into a list of condition entries
   * which are used by the encoder to specify conditions.
   */
  static absl::StatusOr<std::vector<proto::PatchMap::Entry>>
  ActivationConditionsToPatchMapEntries(
      absl::Span<const ActivationCondition> conditions,
      const absl::flat_hash_map<segment_index_t, SubsetDefinition>& segments);

  /*
   * This condition is activated if every set of segments intersects the
   * input subset definition. ie. input subset def intersects {s_1, s_2} AND
   * input subset def intersects {...} AND ...
   *     which is effectively: (s_1 OR s_2) AND ...
   */
  const absl::Span<const common::SegmentSet> conditions() const {
    return conditions_;
  }

  bool IsFallback() const { return is_fallback_; }

  /*
   * Populates out with the set of patch ids that are part of this condition
   * (excluding the activated patch)
   */
  common::SegmentSet TriggeringSegments() const {
    common::SegmentSet out;
    for (auto g : conditions_) {
      for (auto segment_id : g) {
        out.insert(segment_id);
      }
    }
    return out;
  }

  /*
   * Returns a human readable string representation of this condition.
   */
  std::string ToString() const;

  /*
   * The patch to load when the condition is satisfied.
   */
  patch_id_t activated() const { return activated_; }

  bool IsExclusive() const { return is_exclusive_; }

  bool IsUnitary() const {
    return conditions().size() == 1 && conditions().at(0).size() == 1;
  }

  ActivationConditionProto ToConfigProto() const;

  bool operator<(const ActivationCondition& other) const;

  bool operator==(const ActivationCondition& other) const {
    return conditions_ == other.conditions_ && activated_ == other.activated_ &&
           is_fallback_ == other.is_fallback_ &&
           is_exclusive_ == other.is_exclusive_;
  }

  bool operator!=(const ActivationCondition& other) const {
    return !(*this == other);
  }

 private:
  ActivationCondition() : conditions_(), activated_(0) {}

  bool is_fallback_ = false;
  bool is_exclusive_ = false;
  // Represents:
  // (s_1_1 OR s_1_2 OR ...) AND (s_2_1 OR ...) ...
  std::vector<common::SegmentSet> conditions_;
  patch_id_t activated_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_ACTIVATION_CONDITION_H_