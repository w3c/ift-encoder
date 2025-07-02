#include "ift/encoder/activation_condition.h"

#include <optional>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/int_set.h"
#include "ift/encoder/subset_definition.h"
#include "ift/proto/patch_encoding.h"
#include "ift/proto/patch_map.h"

using absl::btree_set;
using absl::flat_hash_map;
using absl::Span;
using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using common::IntSet;
using common::SegmentSet;
using ift::proto::PatchEncoding;
using ift::proto::PatchMap;

namespace ift::encoder {

ActivationCondition ActivationCondition::exclusive_segment(
    segment_index_t index, patch_id_t activated) {
  ActivationCondition condition;
  condition.activated_ = activated;
  condition.conditions_ = {{index}};
  condition.is_exclusive_ = true;
  return condition;
}

ActivationCondition ActivationCondition::and_segments(
    const SegmentSet& segments, patch_id_t activated) {
  ActivationCondition conditions;
  conditions.activated_ = activated;

  for (auto id : segments) {
    conditions.conditions_.push_back(SegmentSet{id});
  }

  return conditions;
}

ActivationCondition ActivationCondition::or_segments(const SegmentSet& segments,
                                                     patch_id_t activated,
                                                     bool is_fallback) {
  ActivationCondition conditions;
  conditions.activated_ = activated;
  conditions.conditions_.push_back(segments);
  conditions.is_fallback_ = is_fallback;

  return conditions;
}

ActivationCondition ActivationCondition::composite_condition(
    absl::Span<const SegmentSet> groups, patch_id_t activated) {
  ActivationCondition conditions;
  conditions.activated_ = activated;
  for (const auto& group : groups) {
    conditions.conditions_.push_back(group);
  }

  return conditions;
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

  // These two are equal
  return false;
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

void MakeIgnored(PatchMap::Entry& entry, patch_id_t& last_patch_id) {
  entry.ignored = true;
  // patch id for ignored entries doesn't matter, use last + 1 to minimize
  // encoding size.
  entry.patch_indices.clear();
  entry.patch_indices.push_back(++last_patch_id);
}

patch_id_t MapTo(PatchMap::Entry& entry, patch_id_t new_patch_id) {
  entry.ignored = false;
  entry.patch_indices.clear();
  entry.patch_indices.push_back(new_patch_id);
  return new_patch_id;
}

StatusOr<std::vector<PatchMap::Entry>>
ActivationCondition::ActivationConditionsToPatchMapEntries(
    Span<const ActivationCondition> conditions,
    const flat_hash_map<segment_index_t, SubsetDefinition>& segments) {
  std::vector<PatchMap::Entry> entries;
  if (conditions.empty()) {
    return entries;
  }

  // The conditions list describes what the patch map should do, here
  // we need to convert that into an equivalent list of encoder condition
  // entries.
  //
  // To minimize encoded size we can reuse set definitions in later entries
  // via the copy indices mechanism. The conditions are evaluated in three
  // phases to successively build up a set of common entries which can be reused
  // by later ones.
  //
  // Tracks the list of conditions which have not yet been placed in a map
  // entry.
  btree_set<ActivationCondition> remaining_conditions;
  remaining_conditions.insert(conditions.begin(), conditions.end());

  // Phase 1 generate the base entries, there should be one for each
  // unique glyph segment that is referenced in at least one condition.
  // the conditions will refer back to these base entries via copy indices
  //
  // Each base entry can be used to map one condition as well.
  flat_hash_map<uint32_t, uint32_t> segment_id_to_entry_index;
  uint32_t next_entry_index = 0;
  patch_id_t last_patch_id = 0;
  for (auto condition = remaining_conditions.begin();
       condition != remaining_conditions.end();) {
    bool remove = false;
    for (const auto& group : condition->conditions()) {
      for (uint32_t segment_id : group) {
        if (segment_id_to_entry_index.contains(segment_id)) {
          continue;
        }

        auto original = segments.find(segment_id);
        if (original == segments.end()) {
          return absl::InvalidArgumentError(
              StrCat("Codepoint segment ", segment_id, " not found."));
        }
        const auto& original_def = original->second;

        // Segments match on {codepoints} OR {features}, whereas IFT conditions
        // match on {codepoints} AND {features}. So the codepoint and features
        // sets need to be placed in separate conditions which are joined by a
        // disjunctive match if both are present
        std::optional<PatchMap::Entry> codepoints_entry;
        if (!original_def.codepoints.empty()) {
          PatchMap::Entry entry;
          entry.coverage.codepoints = original_def.codepoints;
          entry.encoding = PatchEncoding::GLYPH_KEYED;
          codepoints_entry = entry;
        }

        std::optional<PatchMap::Entry> feature_entry;
        if (!original_def.feature_tags.empty()) {
          PatchMap::Entry entry;
          entry.coverage.features = original_def.feature_tags;
          entry.encoding = PatchEncoding::GLYPH_KEYED;
          feature_entry = entry;
        }

        PatchMap::Entry entry;
        if (codepoints_entry && feature_entry) {
          // The codepoints and feature entries are going to be child entries
          // which don't directly include an activated patch id, so set them to
          // be ignored
          MakeIgnored(*codepoints_entry, last_patch_id);
          entries.push_back(*codepoints_entry);
          uint32_t codepoints_entry_index = next_entry_index++;

          MakeIgnored(*feature_entry, last_patch_id);
          entries.push_back(*feature_entry);
          uint32_t features_entry_index = next_entry_index++;

          entry.coverage.conjunctive = false;
          entry.coverage.child_indices.insert(codepoints_entry_index);
          entry.coverage.child_indices.insert(features_entry_index);
          entry.encoding = PatchEncoding::GLYPH_KEYED;
        } else if (codepoints_entry) {
          entry = *codepoints_entry;
        } else if (feature_entry) {
          entry = *feature_entry;
        }

        if (condition->IsUnitary()) {
          // this condition can use this entry to map itself.
          last_patch_id = MapTo(entry, condition->activated());
          remove = true;
        } else {
          // Otherwise this entry does nothing (ignored = true), but will be
          // referenced by later entries.
          MakeIgnored(entry, last_patch_id);
        }

        entries.push_back(entry);
        segment_id_to_entry_index[segment_id] = next_entry_index++;
      }
    }

    if (remove) {
      condition = remaining_conditions.erase(condition);
    } else {
      ++condition;
    }
  }

  // Phase 2 generate entries for all groups of patches reusing the base entries
  // written in phase one. When writing an entry if the triggering group is the
  // only one in the condition then that condition can utilize the entry (just
  // like in Phase 1).
  flat_hash_map<IntSet, uint32_t> segment_group_to_entry_index;
  for (auto condition = remaining_conditions.begin();
       condition != remaining_conditions.end();) {
    bool remove = false;

    for (const auto& group : condition->conditions()) {
      if (group.size() <= 1 || segment_group_to_entry_index.contains(group)) {
        // don't handle groups of size one, those will just reference the base
        // entry directly.
        continue;
      }

      PatchMap::Entry entry;
      entry.encoding = PatchEncoding::GLYPH_KEYED;
      entry.coverage.conjunctive = false;  // ... OR ...

      for (uint32_t segment_id : group) {
        auto entry_index = segment_id_to_entry_index.find(segment_id);
        if (entry_index == segment_id_to_entry_index.end()) {
          return absl::InternalError(
              StrCat("entry for segment_id = ", segment_id,
                     " was not previously created."));
        }
        entry.coverage.child_indices.insert(entry_index->second);
      }

      if (condition->conditions().size() == 1) {
        last_patch_id = MapTo(entry, condition->activated());
        remove = true;
      } else {
        MakeIgnored(entry, last_patch_id);
      }

      entries.push_back(entry);
      segment_group_to_entry_index[group] = next_entry_index++;
    }

    if (remove) {
      condition = remaining_conditions.erase(condition);
    } else {
      ++condition;
    }
  }

  // Phase 3 for any remaining conditions create the actual entries utilizing
  // the groups (phase 2) and base entries (phase 1) as needed
  for (auto condition = remaining_conditions.begin();
       condition != remaining_conditions.end(); condition++) {
    PatchMap::Entry entry;
    entry.encoding = PatchEncoding::GLYPH_KEYED;
    entry.coverage.conjunctive = true;  // ... AND ...

    for (const auto& group : condition->conditions()) {
      if (group.size() == 1) {
        entry.coverage.child_indices.insert(
            segment_id_to_entry_index[*group.begin()]);
        continue;
      }

      entry.coverage.child_indices.insert(segment_group_to_entry_index[group]);
    }

    last_patch_id = MapTo(entry, condition->activated());
    entries.push_back(entry);
  }

  return entries;
}

}  // namespace ift::encoder