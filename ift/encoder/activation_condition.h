#ifndef IFT_ENCODER_ACTIVATION_CONDITION_H_
#define IFT_ENCODER_ACTIVATION_CONDITION_H_

#include "ift/common/int_set.h"
#include "ift/config/segmentation_plan.pb.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"
#include "ift/freq/probability_bound.h"
#include "ift/freq/probability_calculator.h"
#include "ift/proto/patch_encoding.h"
#include "ift/proto/patch_map.h"

namespace ift::encoder {

/*
 * The conditions under which a patch should be laoded.
 *
 * The condition is encoded as a monotonic CNF (Conjunctive Normal Form) boolean
 * expression:
 *
 * (s_1_1 OR s_1_2 OR ...) AND (s_2_1 OR ...) ...
 */
class ActivationCondition {
 public:
  /*
   * Constructs a condition that always activates.
   */
  static ActivationCondition True(patch_id_t activated);

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
  static ActivationCondition and_segments(const ift::common::SegmentSet& ids,
                                          patch_id_t activated);

  /*
   * Constructs a condition that activates when the input intersects
   * (segment_1) OR ... OR inersects(segment_n).
   */
  static ActivationCondition or_segments(const ift::common::SegmentSet& ids,
                                         patch_id_t activated,
                                         bool is_fallback = false);

  /*
   * Constructs a condition that activates when the input intersects:
   * (s1 OR ..) AND (si OR ...) AND ...
   */
  static ActivationCondition composite_condition(
      absl::Span<const ift::common::SegmentSet> groups, patch_id_t activated,
      bool is_fallback = false);

  /*
   * Constructs a new condition equal to 'condition' except with the is exclusive flag
   * cleared.
   */
  static ActivationCondition clear_exclusive(ActivationCondition&& condition);

  // Returns a new activation condition that activates on (a && b)
  //
  // The new condition uses the values for the other fields
  // from condition a (eg. activated, encoding).
  static ActivationCondition And(const ActivationCondition& a,
                                 const ActivationCondition& b);

  // Returns a new activation condition that activates on (a || b)
  //
  // The new condition uses the values for the other fields
  // from condition a (eg. activated, encoding).
  static ActivationCondition Or(const ActivationCondition& a,
                                const ActivationCondition& b);

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
  const absl::Span<const ift::common::SegmentSet> conditions() const {
    return conditions_;
  }

  bool IsAlwaysTrue() const { return conditions().size() == 0; }

  bool IsFallback() const { return is_fallback_; }

  /*
   * Populates out with the set of patch ids that are part of this condition
   * (excluding the activated patch)
   */
  ift::common::SegmentSet TriggeringSegments() const {
    ift::common::SegmentSet out;
    for (const auto& segments : conditions_) {
      out.union_set(segments);
    }
    return out;
  }

  proto::PatchEncoding Encoding() const { return encoding_; }

  // Returns true if any of the condition groups intersects with 'segments'.
  bool Intersects(const common::SegmentSet& segments) const;

  // Generate a new condition which is a copy of this one but with all
  // occurences of 'segments' replaced with base_segment. New condition will be
  // simplified to remove any resulting redundancy.
  ActivationCondition ReplaceSegments(segment_index_t base_segment,
                                      const common::SegmentSet& segments) const;

  /*
   * Returns a human readable string representation of this condition.
   */
  std::string ToString() const;

  /*
   * The patch to load when the condition is satisfied.
   */
  patch_id_t activated() const { return activated_.front(); }

  absl::Span<const patch_id_t> prefetches() const {
    return absl::Span<const patch_id_t>(activated_).subspan(1);
  }

  bool IsExclusive() const { return is_exclusive_; }

  bool IsUnitary() const {
    return conditions().size() == 1 && conditions().at(0).size() == 1;
  }

  // Returns true if the condition is of the form
  // a OR b f OR c ... with no conjunction.
  bool IsPurelyDisjunctive() const { return conditions().size() == 1; }

  // Returns true if the condition is of the form
  // a AND b AND c ... with no disjunction.
  bool IsPurelyConjunctive() const {
    if (conditions().size() <= 1) {
      return false;
    }

    for (const auto& segments : conditions()) {
      if (segments.size() > 1) {
        return false;
      }
    }

    return true;
  }

  // Compute and return the probability that this condition will be activated
  // based on the provided individual segment probabilities.
  //
  // Important: this makes the assumption that segment probabilies are
  //            independent which means this is only an estimate as this is
  //            likely not true.
  absl::StatusOr<freq::ProbabilityBound> ProbabilityBound(
      absl::Span<const Segment> segments,
      const ift::freq::ProbabilityCalculator& calculator) const;

  absl::StatusOr<double> Probability(
      absl::Span<const Segment> segments,
      const ift::freq::ProbabilityCalculator& calculator) const;

  // Compute and return the probability that this condition will be activated if
  // it is modified to merge all segments in "merged_segments" into a single
  // segment with "merged_probability".
  absl::StatusOr<freq::ProbabilityBound> MergedProbabilityBound(
      absl::Span<const Segment> segments,
      segment_index_t merged_segment_index,
      const Segment& merged_segment,
      const ift::freq::ProbabilityCalculator& calculator) const;

  absl::StatusOr<double> MergedProbability(
      absl::Span<const Segment> segments,
      segment_index_t merged_segment_index,
      const Segment& merged_segment,
      const ift::freq::ProbabilityCalculator& calculator) const;

  ift::config::ActivationConditionProto ToConfigProto() const;

  template <typename H>
  friend H AbslHashValue(H h, const ActivationCondition& c) {
    return H::combine(std::move(h), c.conditions_, c.activated_, c.is_fallback_,
                      c.is_exclusive_, c.encoding_);
  }

  bool operator<(const ActivationCondition& other) const;

  bool operator==(const ActivationCondition& other) const {
    return conditions_ == other.conditions_ && activated_ == other.activated_ &&
           is_fallback_ == other.is_fallback_ &&
           is_exclusive_ == other.is_exclusive_ && encoding_ == other.encoding_;
  }

  bool operator!=(const ActivationCondition& other) const {
    return !(*this == other);
  }

  void SetEncoding(proto::PatchEncoding encoding) { encoding_ = encoding; }

  void AddPrefetches(absl::Span<const patch_id_t> activated) {
    activated_.insert(activated_.end(), activated.begin(), activated.end());
  }

 private:
  ActivationCondition()
      : conditions_(), activated_({0}), encoding_(proto::GLYPH_KEYED) {}

  bool is_fallback_ = false;
  bool is_exclusive_ = false;
  // Represents:
  // (s_1_1 OR s_1_2 OR ...) AND (s_2_1 OR ...) ...
  std::vector<ift::common::SegmentSet> conditions_;
  std::vector<patch_id_t> activated_;
  proto::PatchEncoding encoding_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_ACTIVATION_CONDITION_H_