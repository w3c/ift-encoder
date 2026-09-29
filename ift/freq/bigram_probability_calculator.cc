#include "ift/freq/bigram_probability_calculator.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/container/inlined_vector.h"
#include "ift/common/int_set.h"
#include "ift/encoder/segment.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/probability_bound.h"

using absl::btree_set;
using absl::InlinedVector;
using ift::common::CodepointSet;
using ift::common::SegmentSet;
using ift::encoder::Segment;
using ift::encoder::segment_index_t;
using ift::encoder::SubsetDefinition;

namespace ift::freq {

BigramProbabilityCalculator::BigramProbabilityCalculator(
    UnicodeFrequencies frequencies, size_t max_cache_size)
    : frequencies_(std::move(frequencies)),
      cache_("bigram probability", max_cache_size) {}

static ProbabilityBound KouniasBound(
  double unigram_total,
  double bigram_total,
  double max_partial_bigram_total,
  double max_single_bound,
  double max_pair_bound
) {
  // The bounds calculations are based on the Kounias bounds:
  // https://projecteuclid.org/journals/annals-of-mathematical-statistics/volume-39/issue-6/Bounds-for-the-Probability-of-a-Union-with-Applications/10.1214/aoms/1177698049.full

  // A lower bound is given by the greater of three values:
  // - Either the largest individual codepoint frequency.
  // - max(Pi + Pj - Pij)
  // - Or: sum(Pi) - sum(Pj<k)
  double lower = std::max(
      std::max(unigram_total - bigram_total,
               max_pair_bound), max_single_bound);

  // An upper bound is given by
  // sum(Pi) - max_j=1..n [ sum_j!=k(Pjk) ]
  double upper = std::max(
      std::min(unigram_total - max_partial_bigram_total, 1.0),
      lower);
  return ProbabilityBound(lower, upper);
}

ProbabilityBound BigramProbabilityCalculator::ApplyFeatureTags(
    const btree_set<hb_tag_t>& feature_tags,
    ProbabilityBound codepoints_bound) const {
  if (feature_tags.empty()) {
    return codepoints_bound;
  }

  // Since we don't have conjunctive frequencies between codepoints/features
  // and features/features use a simple disjunctive bound for incorporating
  // layout feature probabilities.
  double feature_min = 0.0;
  double feature_sum = 0.0;
  for (hb_tag_t tag : feature_tags) {
    double p = frequencies_.ProbabilityForLayoutTag(tag);
    feature_min = std::max(feature_min, p);
    feature_sum += p;
  }
  double t_max = std::min(1.0, feature_sum);

  return ProbabilityBound(std::max(codepoints_bound.Min(), feature_min),
                          std::min(1.0, codepoints_bound.Max() + t_max));
}

ProbabilityBound BigramProbabilityCalculator::ApplyLowerBoundAndFeatureTags(
    const absl::btree_set<hb_tag_t>& feature_tags,
    double best_lower,
    ProbabilityBound codepoints_bound) const {

  double final_lower = std::max(codepoints_bound.Min(), best_lower);
  double final_upper = std::max(codepoints_bound.Max(), final_lower);
  codepoints_bound = ProbabilityBound(final_lower, final_upper);
  return ApplyFeatureTags(feature_tags, codepoints_bound);
}

BigramProbabilityCalculator::BigramSegmentCacheEntry
BigramProbabilityCalculator::ComputeSegmentEntry(
    const SubsetDefinition& definition) const {
  BigramSegmentCacheEntry entry;
  if (definition.Empty()) {
    entry.codepoints_bound = ProbabilityBound(1.0, 1.0);
    entry.bound = ProbabilityBound(1.0, 1.0);
    entry.saturated = true;
    return entry;
  }

  unsigned n = definition.codepoints.size();
  entry.cps.reserve(n);
  entry.unigram_probs.reserve(n);
  entry.partial_totals.resize(n, 0.0);

  for (unsigned cp : definition.codepoints) {
    double P_cp = frequencies_.ProbabilityFor(cp);
    entry.cps.push_back(cp);
    entry.unigram_probs.push_back(P_cp);
    entry.unigram_total += P_cp;
    entry.max_single_bound = std::max(P_cp, entry.max_single_bound);
  }

  if (entry.max_single_bound >= 1.0) {
    entry.codepoints_bound = ProbabilityBound(1.0, 1.0);
    entry.saturated = true;
  } else {
    for (unsigned i = 0; i < n; i++) {
      for (unsigned j = i + 1; j < n; j++) {
        double Pij = frequencies_.ProbabilityFor(
            entry.cps[i], entry.cps[j], entry.unigram_probs[i],
            entry.unigram_probs[j]);
        entry.bigram_total += Pij;
        entry.partial_totals[i] += Pij;
        entry.partial_totals[j] += Pij;

        entry.max_pair_bound = std::max(
            entry.unigram_probs[i] + entry.unigram_probs[j] - Pij,
            entry.max_pair_bound);
        if (entry.max_pair_bound >= 1.0) {
          entry.codepoints_bound = ProbabilityBound(1.0, 1.0);
          entry.saturated = true;
          break;
        }
      }
      if (entry.saturated) {
        break;
      }
    }

    if (!entry.saturated) {
      double max_partial_bigram_total = 0.0;
      for (double partial_total : entry.partial_totals) {
        max_partial_bigram_total =
            std::max(partial_total, max_partial_bigram_total);
      }
      entry.codepoints_bound = KouniasBound(entry.unigram_total,
        entry.bigram_total, max_partial_bigram_total,
        entry.max_single_bound, entry.max_pair_bound);
    }
  }

  entry.bound = ApplyFeatureTags(definition.feature_tags, entry.codepoints_bound);
  return entry;
}

const BigramProbabilityCalculator::BigramSegmentCacheEntry&
BigramProbabilityCalculator::GetOrComputeSegmentEntry(
    absl::Span<const Segment> segments, segment_index_t segment_index) const {
  std::optional<BigramSegmentCacheEntry>& cached_seg =
      segment_cache_[segment_index];
  if (!cached_seg.has_value()) {
    cached_seg = ComputeSegmentEntry(segments.at(segment_index).Definition());
  }
  return *cached_seg;
}

ProbabilityBound BigramProbabilityCalculator::ComputeProbability(
    absl::Span<const ift::encoder::Segment> segments,
    segment_index_t segment_index) const {
  return GetOrComputeSegmentEntry(segments, segment_index).bound;
}

ProbabilityBound BigramProbabilityCalculator::ComputeMergedProbability(
    absl::Span<const Segment> segments,
    const SegmentSet& segment_indices) const {
  if (segment_indices.empty()) {
    return ProbabilityBound(1.0, 1.0);
  }
  if (segment_indices.size() == 1) {
    return ComputeProbability(segments, *segment_indices.min());
  }

  // Note: this assumes that segments are all disjoint, which is enforced in
  // ClosureGlyphSegmenter::CodepointToGlyphSegments().

  // Like ComputeSegmentEntry() this also utilizes kounias bounds to compute
  // a probability bound. As a starting point we use the various totals
  // collected per segment in the segment_cache_. These are then augmented
  // with any missing inter segment codepoint pairs.

  double best_lower = 0.0;
  double unigram_total = 0.0;
  double bigram_total = 0.0;
  double max_single_bound = 0.0;
  double max_pair_bound = 0.0;
  CodepointSet all_codepoints;
  bool has_feature_tags = false;
  size_t num_partial_totals = 0;

  InlinedVector<const BigramSegmentCacheEntry*, 8> entries;
  entries.reserve(segment_indices.size());

  for (segment_index_t s : segment_indices) {
    const BigramSegmentCacheEntry& entry =
        GetOrComputeSegmentEntry(segments, s);

    // Note: entry is stable since the entry cache is only invalidated/resized
    // via InvalidateSegmentProbabilities() or ResetSegmentProbabilities(...)
    entries.push_back(&entry);
    best_lower = std::max(best_lower, entry.codepoints_bound.Min());
    if (best_lower >= 1.0 || entry.saturated) {
      // Since this is a union the bound must be [1, 1]
      return ProbabilityBound(1.0, 1.0);
    }

    const auto& segment = segments.at(s);
    if (!segment.Definition().feature_tags.empty()) {
      has_feature_tags = true;
    }

    all_codepoints.union_set(segment.Definition().codepoints);
    unigram_total += entry.unigram_total;
    bigram_total += entry.bigram_total;
    max_single_bound = std::max(max_single_bound, entry.max_single_bound);
    max_pair_bound = std::max(max_pair_bound, entry.max_pair_bound);
    num_partial_totals += entry.partial_totals.size();
  }

  btree_set<hb_tag_t> feature_tags;
  if (has_feature_tags) {
    for (segment_index_t s : segment_indices) {
      const auto& seg_tags = segments.at(s).Definition().feature_tags;
      feature_tags.insert(seg_tags.begin(), seg_tags.end());
    }
  }

  auto& cache_entry = cache_[all_codepoints];
  if (cache_entry.has_value()) {
    return ApplyLowerBoundAndFeatureTags(feature_tags, best_lower, *cache_entry);
  }

  InlinedVector<double, 256> partial_totals;
  partial_totals.reserve(num_partial_totals);
  InlinedVector<size_t, 8> offsets;
  offsets.reserve(entries.size());

  for (const auto* entry : entries) {
    offsets.push_back(partial_totals.size());
    partial_totals.insert(partial_totals.end(), entry->partial_totals.begin(),
                          entry->partial_totals.end());
  }

  // All of the bigram sums within each segment are already captured in the
  // cached segment entries; we only need to accumulate the cross-segment pairs for:
  // - partial totals
  // - max pair bound
  // - bigram totals
  size_t num_segments = entries.size();
  for (size_t a = 0; a < num_segments; a++) {
    size_t n_a = entries[a]->cps.size();
    if (n_a == 0) {
      continue;
    }
    for (size_t b = a + 1; b < num_segments; b++) {
      size_t n_b = entries[b]->cps.size();
      if (n_b == 0) {
        continue;
      }

      // Iterate the smaller segment on the outer loop so the inner loop has a
      // larger trip count and accumulates outer partial totals in a register.
      size_t outer_idx = (n_a <= n_b) ? a : b;
      size_t inner_idx = (n_a <= n_b) ? b : a;
      const BigramSegmentCacheEntry& outer = *entries[outer_idx];
      const BigramSegmentCacheEntry& inner = *entries[inner_idx];
      double* partials_outer = partial_totals.data() + offsets[outer_idx];
      double* partials_inner = partial_totals.data() + offsets[inner_idx];
      size_t n_outer = outer.cps.size();
      size_t n_inner = inner.cps.size();

      for (size_t i = 0; i < n_outer; i++) {
        uint32_t cp_i = outer.cps[i];
        double p_i = outer.unigram_probs[i];
        double partial_i_delta = 0.0;
        for (size_t j = 0; j < n_inner; j++) {
          double p_j = inner.unigram_probs[j];
          double Pij =
              frequencies_.ProbabilityFor(cp_i, inner.cps[j], p_i, p_j);
          bigram_total += Pij;
          partial_i_delta += Pij;
          partials_inner[j] += Pij;

          max_pair_bound = std::max(p_i + p_j - Pij, max_pair_bound);
          if (max_pair_bound >= 1.0) {
            cache_entry = ProbabilityBound(1.0, 1.0);
            return *cache_entry;
          }
        }
        partials_outer[i] += partial_i_delta;
      }
    }
  }

  double max_partial_bigram_total = 0.0;
  for (double partial_total : partial_totals) {
    max_partial_bigram_total =
        std::max(partial_total, max_partial_bigram_total);
  }

  cache_entry = KouniasBound(unigram_total, bigram_total,
    max_partial_bigram_total, max_single_bound, max_pair_bound);

  return ApplyLowerBoundAndFeatureTags(feature_tags, best_lower, *cache_entry);
}

ProbabilityBound BigramProbabilityCalculator::ComputeConjunctiveProbability(
    const std::vector<ProbabilityBound>& bounds) const {
  // Here we don't have access to pair probabilities between the segments so we
  // use a bound that relies only on the individual probabilities:
  //
  // sum(P(Si)) - (n - 1) <= P(intersection) <= min(P(Si))
  //
  // For the segments we actually have probability bounds, so use the segment
  // min for the lower bound calc and the segment max for the upper bound calc.
  double sum = 0.0;
  double min_of_maxes = 1.0;
  for (const auto& bound : bounds) {
    sum += bound.Min();
    if (bound.Max() < min_of_maxes) {
      min_of_maxes = bound.Max();
    }
  }
  double min_prob = sum - (double)bounds.size() + 1.0;
  return ProbabilityBound(std::max(0.0, min_prob), min_of_maxes);
}

}  // namespace ift::freq
