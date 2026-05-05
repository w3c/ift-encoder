#include "ift/encoder/activation_condition.h"

#include <algorithm>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/encoder/entry_graph.h"
#include "ift/encoder/subset_definition.h"
#include "ift/encoder/types.h"
#include "ift/freq/probability_bound.h"
#include "ift/proto/patch_encoding.h"
#include "ift/proto/patch_map.h"

using ift::config::ActivationConditionProto;
using ift::config::SegmentsProto;

using absl::btree_set;
using absl::flat_hash_map;
using absl::Span;
using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using ift::common::IntSet;
using ift::common::SegmentSet;
using ift::freq::ProbabilityCalculator;
using ift::proto::GLYPH_KEYED;
using ift::proto::PatchEncoding;
using ift::proto::PatchMap;
using ift::freq::ProbabilityBound;

namespace ift::encoder {

static void Simplify(std::vector<SegmentSet>& conditions);

ActivationCondition ActivationCondition::True(patch_id_t activated) {
  ActivationCondition condition;
  condition.activated_ = {activated};
  condition.conditions_ = {};
  condition.is_exclusive_ = false;
  return condition;
}

ActivationCondition ActivationCondition::exclusive_segment(
    segment_index_t index, patch_id_t activated) {
  ActivationCondition condition;
  condition.activated_ = {activated};
  condition.conditions_ = {{index}};
  condition.is_exclusive_ = true;
  return condition;
}

ActivationCondition ActivationCondition::and_segments(
    const SegmentSet& segments, patch_id_t activated) {
  ActivationCondition conditions;
  conditions.activated_ = {activated};

  for (auto id : segments) {
    conditions.conditions_.push_back(SegmentSet{id});
  }

  return conditions;
}

ActivationCondition ActivationCondition::or_segments(const SegmentSet& segments,
                                                     patch_id_t activated,
                                                     bool is_fallback) {
  ActivationCondition conditions;
  conditions.activated_ = {activated};
  conditions.conditions_.push_back(segments);
  conditions.is_fallback_ = is_fallback;

  return conditions;
}

ActivationCondition ActivationCondition::composite_condition(
    absl::Span<const SegmentSet> groups, patch_id_t activated,
    bool is_fallback) {
  ActivationCondition conditions;
  conditions.activated_ = {activated};
  for (const auto& group : groups) {
    conditions.conditions_.push_back(group);
  }

  Simplify(conditions.conditions_);

  conditions.is_fallback_ = is_fallback;
  return conditions;
}

ActivationCondition ActivationCondition::clear_exclusive(ActivationCondition&& condition) {
  ActivationCondition updated(std::move(condition));
  updated.is_exclusive_ = false;
  return updated;
}

static void Simplify(std::vector<SegmentSet>& conditions) {
  if (conditions.size() <= 1) return;

  // Conditions can be simplified by removing duplicate and/or subset
  // sub-conditions. For example if one sub-condition is a subset of another
  // ((s1) AND (s1 or s2)), then the larger sub-condition (s1 or s2) is not
  // necessary and can be dropped.

  // Sort by size to ensure that if A is a subset of B, then A comes before B.
  std::sort(conditions.begin(), conditions.end(),
            [](const SegmentSet& a, const SegmentSet& b) {
              return a.size() < b.size();
            });

  std::vector<uint8_t> redundant(conditions.size(), 0);
  for (size_t i = 0; i < conditions.size(); ++i) {
    if (redundant[i]) continue;
    for (size_t j = i + 1; j < conditions.size(); ++j) {
      if (redundant[j]) continue;
      if (conditions[i].is_subset_of(conditions[j])) {
        redundant[j] = 1;
      }
    }
  }

  size_t write_index = 0;
  for (size_t i = 0; i < conditions.size(); ++i) {
    if (redundant[i]) {
      continue;
    }
    if (write_index != i) {
      conditions[write_index] = std::move(conditions[i]);
    }
    write_index++;
  }
  conditions.erase(conditions.begin() + write_index, conditions.end());

  // Final sort to normalize the order of conditions.
  std::sort(conditions.begin(), conditions.end());
}

ActivationCondition ActivationCondition::And(const ActivationCondition& a,
                                             const ActivationCondition& b) {
  bool a_exclusive = a.IsExclusive() || a.IsAlwaysTrue();
  bool b_exclusive = b.IsExclusive() || b.IsAlwaysTrue();

  ActivationCondition condition = a;
  condition.conditions_.insert(condition.conditions_.end(),
                               b.conditions().begin(), b.conditions().end());
  Simplify(condition.conditions_);
  condition.is_exclusive_ = a_exclusive && b_exclusive && condition.IsUnitary();
  return condition;
}

ActivationCondition ActivationCondition::Or(const ActivationCondition& a,
                                            const ActivationCondition& b) {
  ActivationCondition condition = a;
  condition.conditions_.clear();
  condition.conditions_.reserve(a.conditions().size() * b.conditions().size());

  for (const auto& a_group : a.conditions()) {
    for (const auto& b_group : b.conditions()) {
      SegmentSet combined_group = a_group;
      combined_group.union_set(b_group);
      condition.conditions_.push_back(std::move(combined_group));
    }
  }

  Simplify(condition.conditions_);
  condition.is_exclusive_ = false;
  return condition;
}

bool ActivationCondition::Intersects(const common::SegmentSet& segments) const {
  for (const auto& condition_group : conditions_) {
    if (condition_group.intersects(segments)) {
      return true;
    }
  }
  return false;
}

ActivationCondition ActivationCondition::ReplaceSegments(
    segment_index_t base_segment, const common::SegmentSet& segments) const {
  ActivationCondition new_condition = *this;
  for (auto& condition_group : new_condition.conditions_) {
    if (condition_group.intersects(segments)) {
      condition_group.subtract(segments);
      condition_group.insert(base_segment);
    }
  }
  Simplify(new_condition.conditions_);
  return new_condition;
}

std::string ActivationCondition::ToString() const {
  std::stringstream out;
  out << "if (";
  bool first = true;
  for (const auto& set : conditions()) {
    if (!first) {
      out << " AND ";
    } else {
      first = false;
    }

    if (set.size() > 1) {
      out << "(";
    }
    bool first_inner = true;
    for (uint32_t id : set) {
      if (!first_inner) {
        out << " OR ";
      } else {
        first_inner = false;
      }
      out << "s" << id;
    }
    if (set.size() > 1) {
      out << ")";
    }
  }

  if (IsAlwaysTrue()) {
    out << "true";
  }

  out << ") then p" << activated();
  return out.str();
}

bool ActivationCondition::operator<(const ActivationCondition& other) const {
  if (conditions_.size() != other.conditions_.size()) {
    return conditions_.size() < other.conditions_.size();
  }

  auto a = conditions_.begin();
  auto b = other.conditions_.begin();
  while (a != conditions_.end() && b != other.conditions_.end()) {
    if (a->size() != b->size()) {
      return a->size() < b->size();
    }

    auto aa = a->begin();
    auto bb = b->begin();
    while (aa != a->end() && bb != b->end()) {
      if (*aa != *bb) {
        return *aa < *bb;
      }
      aa++;
      bb++;
    }

    a++;
    b++;
  }

  if (activated_ != other.activated_) {
    return activated_ < other.activated_;
  }

  if (is_exclusive_ != other.is_exclusive_) {
    return is_exclusive_;
  }

  if (is_fallback_ != other.is_fallback_) {
    return !is_fallback_;
  }

  return encoding_ < other.encoding_;
}

template <typename ProtoType>
ProtoType ToSetProto(const IntSet& set) {
  ProtoType values;
  for (uint32_t v : set) {
    values.add_values(v);
  }
  return values;
}

ActivationConditionProto ActivationCondition::ToConfigProto() const {
  ActivationConditionProto proto;

  for (const auto& ss : conditions()) {
    *proto.add_required_segments() = ToSetProto<SegmentsProto>(ss);
  }
  proto.set_activated_patch(activated());

  return proto;
}

StatusOr<std::vector<PatchMap::Entry>>
ActivationCondition::ActivationConditionsToPatchMapEntries(
    Span<const ActivationCondition> conditions,
    const flat_hash_map<segment_index_t, SubsetDefinition>& segments) {
  // Set whichever encoding occurs the most to be the default
  PatchEncoding default_encoding = GLYPH_KEYED;
  uint32_t max_count = 0;
  flat_hash_map<PatchEncoding, uint32_t> encoding_count;
  for (const auto& c : conditions) {
    auto& count = encoding_count[c.Encoding()];
    count++;
    if (count > max_count) {
      max_count = count;
      default_encoding = c.Encoding();
    }
  }

  std::vector<PatchMap::Entry> entries;
  if (conditions.empty()) {
    return entries;
  }

  EntryGraph graph = TRY(EntryGraph::Create(conditions, segments));
  TRYV(graph.Optimize());
  return graph.ToPatchMapEntries(default_encoding);
}

StatusOr<double> ActivationCondition::Probability(
    Span<const Segment> segments,
    const ProbabilityCalculator& calculator) const {
  return TRY(this->ProbabilityBound(segments, calculator)).Average();
}

StatusOr<ProbabilityBound> ActivationCondition::ProbabilityBound(
    Span<const Segment> segments,
    const ProbabilityCalculator& calculator) const {
  if (conditions_.empty()) {
    return freq::ProbabilityBound(1.0, 1.0);
  }

  std::vector<freq::ProbabilityBound> bounds;
  bool is_conjunctive = conditions_.size() > 1;
  for (const auto& segment_set : conditions_) {
    freq::ProbabilityBound set_bound = freq::ProbabilityBound::Zero();
    if (segment_set.empty()) {
      return absl::InternalError("Unexpected empty disjunctive group.");
    } else if (segment_set.size() > 1) {
      // For a group (s1 OR s2 OR ...), compute the union of their definitions.
      std::vector<const Segment*> union_segments;
      for (unsigned s_index : segment_set) {
        union_segments.push_back(&segments[s_index]);
      }
      set_bound = calculator.ComputeMergedProbability(union_segments);
    } else {
      set_bound = segments[*segment_set.min()].ProbabilityBound();
    }

    if (!is_conjunctive) {
      return set_bound;
    }
    bounds.push_back(set_bound);
  }

  return calculator.ComputeConjunctiveProbability(bounds);
}

StatusOr<double> ActivationCondition::MergedProbability(
    Span<const Segment> segments,
    segment_index_t merged_segment_index,
    const Segment& merged_segment,
    const ProbabilityCalculator& calculator) const {
  return TRY(MergedProbabilityBound(segments, merged_segment_index, merged_segment, calculator)).Average();
}


StatusOr<ProbabilityBound> ActivationCondition::MergedProbabilityBound(
    Span<const Segment> segments,
    segment_index_t merged_segment_index,
    const Segment& merged_segment,
    const ProbabilityCalculator& calculator) const {

  if (conditions_.empty()) {
    return freq::ProbabilityBound(1.0, 1.0);
  }

  std::vector<freq::ProbabilityBound> bounds;
  bool is_conjunctive = conditions_.size() > 1;
  for (const auto& segment_set : conditions_) {
    freq::ProbabilityBound set_bound = freq::ProbabilityBound::Zero();
    if (segment_set.empty()) {
      return absl::InternalError("Unexpected empty disjunctive group.");
    } else if (segment_set.size() > 1) {
      // For a group (s1 OR s2 OR ...), compute the union of their definitions.
      std::vector<const Segment*> union_segments;
      for (unsigned s_index : segment_set) {
        if (s_index == merged_segment_index) {
          union_segments.push_back(&merged_segment);
        } else {
          union_segments.push_back(&segments[s_index]);
        }
      }
      set_bound = calculator.ComputeMergedProbability(union_segments);
    } else if (*segment_set.min() ==  merged_segment_index) {
      set_bound = merged_segment.ProbabilityBound();
    } else {
      set_bound = segments[*segment_set.min()].ProbabilityBound();
    }

    if (!is_conjunctive) {
      return set_bound;
    }
    bounds.push_back(set_bound);
  }

  return calculator.ComputeConjunctiveProbability(bounds);
}

}  // namespace ift::encoder