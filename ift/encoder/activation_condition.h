#ifndef IFT_ENCODER_ACTIVATION_CONDITION_H_
#define IFT_ENCODER_ACTIVATION_CONDITION_H_

#include "common/int_set.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"
#include "ift/proto/patch_encoding.h"
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
  patch_id_t activated() const { return activated_.front(); }

  absl::Span<const patch_id_t> prefetches() const {
    return absl::Span<const patch_id_t>(activated_).subspan(1);
  }

  bool IsExclusive() const { return is_exclusive_; }

  bool IsUnitary() const {
    return conditions().size() == 1 && conditions().at(0).size() == 1;
  }

  // Compute and return the probability that this condition will be activated
  // based on the provided individual segment probabilities.
  //
  // Important: this makes the assumption that segment probabilies are
  //            independent which means this is only an estimate as this is
  //            likely not true.
  absl::StatusOr<double> Probability(absl::Span<const Segment> segments) const {
    // TODO(garretrieger): XXXXX this is assuming independence between the
    //   segmentations being combined. Rework to utilize the probability
    //   calculator.
    bool is_conjunctive = conditions_.size() > 1;
    double total_probability = 1.0;
    for (const auto& segment_set : conditions_) {
      if (is_conjunctive && segment_set.size() > 1) {
        // Composite conditions (eg. (a or b) and (c or d)) may have repeated
        // segments in each conjunctive group (eg. (a or b) and (a or d)) which
        // requires special analysis to correctly determine probability. For our
        // current use cases we don't need to support this.
        return absl::UnimplementedError(
            "Calculating probability of composite conditions is not "
            "supported.");
      }

      double not_probability = 1.0;
      for (unsigned s_index : segment_set) {
        const auto& s = segments[s_index];
        not_probability *= 1.0 - s.Probability();
      }
      double set_probability = 1.0 - not_probability;
      total_probability *= set_probability;
    }
    return total_probability;
  }

  // Compute and return the probability that this condition will be activated if
  // it is modified to merge all segments in "merged_segments" into a single
  // segment with "merged_probability".
  absl::StatusOr<double> MergedProbability(
      absl::Span<const Segment> segments,
      const common::SegmentSet& merged_segments,
      double merged_probability) const {
    if (conditions_.size() > 1) {
      // Purely conjunctive condition.
      double total_probability = 1.0;
      bool segment_set_contains_merged = false;
      for (const auto& segment_set : conditions_) {
        if (segment_set.size() > 1) {
          // Composite conditions (eg. (a or b) and (c or d)) may have repeated
          // segments in each conjunctive group (eg. (a or b) and (a or d))
          // which requires special analysis to correctly determine probability.
          // For our current use cases we don't need to support this.
          return absl::UnimplementedError(
              "Calculating probability of composite conditions is not "
              "supported.");
        }
        for (unsigned s_index : segment_set) {
          if (merged_segments.contains(s_index)) {
            segment_set_contains_merged = true;
            continue;  // Skip individual segments that are part of the merged
                       // set
          }
          const auto& s = segments[s_index];
          total_probability *= s.Probability();
        }
      }
      if (segment_set_contains_merged) {
        total_probability *= merged_probability;
      }
      return total_probability;
    } else {
      // Purely disjunctive condition.
      double total_probability = 1.0;
      for (const auto& segment_set : conditions_) {
        double not_probability = 1.0;
        bool segment_set_contains_merged = false;
        for (unsigned s_index : segment_set) {
          if (merged_segments.contains(s_index)) {
            segment_set_contains_merged = true;
            continue;  // Skip individual segments that are part of the merged
                       // set
          }
          const auto& s = segments[s_index];
          not_probability *= 1.0 - s.Probability();
        }

        if (segment_set_contains_merged) {
          not_probability *= 1.0 - merged_probability;
        }
        double set_probability = 1.0 - not_probability;
        total_probability *= set_probability;
      }
      return total_probability;
    }
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
  std::vector<common::SegmentSet> conditions_;
  std::vector<patch_id_t> activated_;
  proto::PatchEncoding encoding_;
};

}  // namespace ift::encoder

#endif  // IFT_ENCODER_ACTIVATION_CONDITION_H_