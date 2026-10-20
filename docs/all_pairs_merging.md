# All-Pairs Cost-Based Segment Merging

Author: Garret Rieger  
Date: Oct 16th, 2026

## Introduction

During cost-based segmentation ([segmenter.md](segmenter.md)), `Merger::TryNextMerge()` iteratively merges segments within each merge group to minimize the expected number of bytes transferred:

$$\text{cost}(S) = \sum_{m=1}^M \sum_{p_i \in S} P_m(c_i) \times (\text{size}(p_i) + k)$$

Currently, candidate selection in `Merger::MergeSegmentWithCosts()` uses a **single-base ("leader-follower")** greedy strategy:

1. Pick the first active segment in `candidate_segments_` (`base_segment_index`, which starts as the highest-probability segment in the merge group).
2. Evaluate merges between `base_segment_index` and all other active segments (plus composite conditions involving `base_segment_index`).
3. Apply the lowest-cost-delta merge found for `base_segment_index`, reprocess changed glyph conditions, and repeat with the **same** `base_segment_index` until no further merges reduce cost (and `min_group_size` is met), at which point `base_segment_index` is marked finished and removed from `candidate_segments_`.

While restricting each pass to a single `base_segment_index` avoids maintaining global pairwise state, it has a structural quality limitation: `base_segment_index` ($s_0$) will greedily absorb a lower-probability segment $s_j$ even when $\Delta \text{cost}(s_0, s_j)$ is only marginally negative (e.g. $-5$ bytes) while $s_j$ has a much stronger merge partner $s_k$ (e.g. $\Delta \text{cost}(s_j, s_k) = -150$ bytes). Once $s_0$ absorbs $s_j$, $s_j$'s glyphs are loaded at $s_0$'s higher probability, permanently preventing the $(s_j, s_k)$ merge.

This document describes a plan to prototype **All-Pairs Agglomerative Merging** in `Merger`, gated behind `--experimental_all_pairs_merging`, while keeping runtime and Brotli compression call counts competitive with (or lower than) the single-base approach.

---

## Performance Challenges of Naive All-Pairs Merging

Upgrading to all-pairs merging naively introduces two potential performance cliffs:

1. **Upfront Brotli Compression Cliff**: Evaluating exact cost deltas (`CandidateMerge::ComputeCostDelta<false>`, which compresses candidate glyph sets with Brotli) across all $\binom{N}{2}$ pairs before making the first merge would perform $O(N^2)$ Brotli compressions with a weak pruning threshold (`delta_to_beat = 0.0`). Moreover, every applied merge invalidates up to $2N$ pairs involving the merged segments, discarding most of that upfront Brotli work.
2. **Best-Case Delta (`ComputeCostDelta<true>`) Overhead**: Even without Brotli, calling the general `CandidateMerge::ComputeCostDelta<true>` on all $\binom{N}{2}$ pairs (and $O(N)$ pairs after each merge) is expensive if every evaluation allocates `modified_conditions` and `new_conditions` hash maps, copies `ActivationCondition`s, and computes $O(|s_a| \cdot |s_b|)$ bigram union bounds via `AggregateMergedProbability()`.

---

## Algorithm Design

### 1. Two-Phase Execution (`Phase 1: Cost Reduction` $\rightarrow$ `Phase 2: Minimum Group Size`)

In the current single-base merger, a segment that starts below `min_group_size` (e.g. 1 codepoint when `min_group_size = 4`) begins its scan with `delta_to_beat = nullopt`, meaning positive-cost-delta candidates cannot be pruned until a negative-cost-delta candidate is encountered.

In all-pairs mode, segment merging is split into two phases:

* **Phase 1 (All-Pairs Cost-Reducing Merges, $\Delta \text{cost} < 0$)**: A global priority queue evaluates and applies merges with strictly negative cost deltas ($\Delta \text{cost} < 0.0$) across all active segments above `optimization_cutoff_segment_`. Because Phase 1 only accepts negative deltas, every candidate evaluation has an immediate upper bound of `delta_to_beat <= 0.0` (tightened further by the current heap minimum), enabling aggressive pruning of positive-delta pairs across the entire merge group.
* **Phase 2 (Minimum Group Size Enforcement)**: Once Phase 1 exhausts all negative-cost-delta merges, any segment that already meets `MinimumGroupSize()` is marked finished in $O(1)$. Remaining undersized segments are merged using `MergeSegmentWithCosts()` in descending probability order to meet `MinimumGroupSize()`. If a forced merge in Phase 2 produces a segment with new negative-cost-delta opportunities, Phase 1 can resume for that segment.
* **Patch Merging**: As before, once all segment merges complete, `TryNextPatchMerge()` runs if `UsePatchMerges()` is enabled.

### 2. Lazy Two-Stage Priority Queue (`BEST_CASE` $\rightarrow$ `EXACT`)

Instead of computing exact Brotli-backed cost deltas when inserting candidates into the priority queue, a single min-heap stores both **lower-bound (`is_exact = false`)** and **exact (`is_exact = true`)** entries:

```cpp
struct QueuedCandidate {
  double cost_delta;
  bool is_exact;
  // Canonical set of all segments participating in the merge (size >= 2).
  // The surviving base segment is always *segments.min().
  ift::common::SegmentSet segments;
  uint32_t created_epoch;
  std::optional<CandidateMerge> evaluated_merge;  // Present when is_exact == true.
};
```

#### Deterministic Total Ordering
To guarantee run-to-run determinism, heap entries are ordered by:
1. `cost_delta` ascending (most negative delta first).
2. `is_exact` (`true` before `false`, so an exact candidate tied with a lower bound is popped and applied immediately).
3. `segments` lexicographically ascending (`SegmentSet::operator<`).

#### Pop-and-Refine Loop
When searching for the next Phase 1 merge:
1. Pop the top `QueuedCandidate` from the min-heap.
2. **Staleness check**: If any $s \in \text{candidate.segments}$ has `segment_epoch_[s] > candidate.created_epoch` (meaning $s$ was removed or had its conditions/glyphs modified after this entry was computed), discard the entry and continue.
3. **If `is_exact == false`**:
   * Determine `delta_to_beat`: `0.0` (since Phase 1 requires $\Delta \text{cost} < 0.0$). Note: we cannot use the next heap entry's `cost_delta` as `delta_to_beat` if that entry is still `is_exact == false`, because its exact delta may rise when refined; however, we *can* track the best `is_exact == true` delta currently in the heap as `best_exact_delta_in_heap` and pass `std::min(0.0, best_exact_delta_in_heap)` as `delta_to_beat`.
   * Evaluate `CandidateMerge::AssessSegmentMerge(*this, *segments.min(), segments, baseline_with_delta_to_beat)`.
   * If a valid `CandidateMerge` is returned with `CostDelta() < 0.0`, push `(merge.CostDelta(), is_exact = true, segments, current_epoch_, merge)` back into the heap.
4. **If `is_exact == true`**:
   * Because its exact cost delta is $\le$ the `best_case` lower bound of every other candidate in the heap, it is the globally best merge. Apply `candidate.evaluated_merge->Apply(*this)` and return its `InvalidationSet`.

**Why this reduces Brotli calls**: In single-base mode, if `s_0` only has a weak best merge (e.g. $\Delta = -3$), `delta_to_beat` is $-3$, so `s_0` runs Brotli on every candidate with `best_case_delta < -3`. In the lazy global heap, the strongest merges across the entire group (e.g. $\Delta = -200$) are popped first; pairs with weaker `best_case_delta` values never run Brotli until the global best delta rises to meet them, by which time many of their constituent segments have already been merged elsewhere.

### 3. Probability-Ordered Row Pruning & Inert Fast-Path

In single-base merging, only the lowest-indexed segment ever merges, so iterating `candidate_segments_` in index order automatically iterates in non-increasing probability order and satisfies `P(base) >= P(other)`. Under all-pairs merging, any segment $s$ can merge and increase its probability.

To preserve `BestCaseInertProbabilityThreshold` pruning and avoid $O(N^2)$ calls to `AggregateMergedProbability()`:

1. **Per-Segment Cached Metadata**: `Merger` maintains flat vectors indexed by `segment_index_t`:
   * `cached_agg_prob_[s]`: `AggregateProbability(s)`
   * `cached_exclusive_size_[s]`: `patch_size_cache->GetPatchSize(ExclusiveGlyphs(s))` (or `0` if empty)
2. **Probability-Ordered Active Set**: Active segments below `optimization_cutoff_segment_` with non-empty exclusive glyphs are maintained in an ordered structure sorted by `(cached_agg_prob_[s] descending, s ascending)`.
3. **Row Pruning via `BestCaseInertProbabilityThreshold`**:
   * Any exclusive pair $\{u, v\}$ is oriented during candidate generation so that $u$ is higher-ranked in probability order ($P_{\text{agg}}(u) \ge P_{\text{agg}}(v)$).
   * When scanning lower-probability partners $v$ for segment $u$ in descending order of $P_{\text{agg}}(v)$, once $u$ is inert and $P_{\text{agg}}(v) \le \text{BestCaseInertProbabilityThreshold}(\text{size}_u, P_{\text{agg}}(u), 0.0)$, all subsequent inert segments $v'$ also have $P_{\text{agg}}(v') \le P_{\text{agg}}(v)$ and cannot achieve a negative cost delta with $u$, allowing the scan to `break` early (or skip remaining inert segments).
4. **$O(1)$ Pre-Check & Inert Fast-Path in `ComputeCostDelta`**:
   * When both segments $u$ and $v$ are inert, their only modified conditions are their two unitary exclusive conditions `(u)` and `(v)`, which collapse into a single unitary condition `(min(u, v))`.
   * Under `best_case_size_reduction_fraction` ($r_{\text{best}}$), the best-case merged patch size is:
     $$S_{\text{best}}(u, v) = \max(S_u, S_v) + \max\left(\lfloor (\min(I_u, I_v)) \cdot r_{\text{best}} \rfloor, \text{BEST\_CASE\_MERGE\_SIZE\_DELTA}\right)$$
     where $I_s = \max(0, S_s - \text{base\_overhead})$.
   * Since $P_{\text{agg}}(u \cup v) \ge \max(P_{\text{agg}}(u), P_{\text{agg}}(v)) = P_{\text{agg}}(u)$, we can check in ~5 arithmetic operations—**before** calling `AggregateMergedProbability()` or allocating any hash maps—whether:
     $$P_{\text{agg}}(u) \cdot (S_{\text{best}}(u, v) - S_u) - P_{\text{agg}}(v) \cdot (S_v + k_{\text{net}}) \ge \text{delta\_to\_beat}$$
     If this holds, the pair cannot possibly pass `ComputeCostDelta<true>` and is rejected in nanoseconds.
   * If it passes the pre-check, the inert fast-path computes `AggregateMergedProbability({u, v})` and evaluates the 3-term cost delta directly without constructing `modified_conditions` or `new_conditions` maps.

### 4. Composite Condition Candidates

In single-base mode, `CollectCompositeCandidateMerges(base_segment_index)` scans `OrderedConditions()` for every base segment, potentially evaluating a 3-segment condition $\{s_1, s_2, s_3\}$ multiple times.

In all-pairs mode (when `target_mode != UNICODE_RANGE`):
* Each composite condition $c$ with triggering segments $S = c.\text{TriggeringSegments()}$ satisfying $|S| > 2$, $*S.\text{min}() < \text{optimization\_cutoff\_segment\_}$, $S \cap \text{candidate\_segments\_} \neq \emptyset$, and $S \subseteq \text{inscope\_segments\_}$ defines a canonical candidate merge set $S$ (with `base = *S.min()`).
* Unique composite sets $S$ are evaluated with `ComputeCostDelta<true>(*this, S, std::nullopt, 0.0)` and, if $< 0.0$, pushed into the same priority queue alongside pairwise candidates.

### 5. Epoch-Based Invalidation Across `TryNextMerge()` Calls

In `ClosureGlyphSegmenter::CodepointToGlyphSegments()`, `context.ReprocessChanged(*maybe_modified)` is called *after* `merger.TryNextMerge()` returns. Consequently, updated glyph conditions and groupings for a merge applied in call $k$ are only finalized at the entry of call $k + 1$.

To handle this cleanly without changing `TryNextMerge()`'s external contract:

1. **Epoch Tracking**:
   * `Merger` maintains a monotonic counter `uint32_t current_epoch_ = 0` and a vector `std::vector<uint32_t> segment_epoch_` initialized to `0`.
   * Removed segments (`to_merge` or `MarkFinished`) have `segment_epoch_[s]` set to `UINT32_MAX`.
2. **Dirty Segment Recording on Merge Application (Call $k$)**:
   * When a merge $M$ into `base` is applied:
     * For an **inert merge** (`input_segments_are_inert_ && new_segment_is_inert`), `CandidateMerge::Apply()` updates `glyph_condition_set` and `glyph_groupings` in-place. Only `base` is added to `dirty_segments_`.
     * For a **non-inert merge**, before `Apply()` invalidates conditions, `Merger` records in `dirty_segments_`: `base`, every segment in $c.\text{TriggeringSegments()}$ for $c \in \bigcup_{s \in M} \text{TriggeringSegmentToConditions}(s)$, and every segment in the pre-merge condition of `invalidated_glyphs_`. It also saves `pending_non_inert_base_ = base` and `pending_invalidated_glyphs_ = invalidated_glyphs_`.
3. **Refreshing Dirty Segments at Next `TryNextMerge()` Entry (Call $k + 1$)**:
   * If `pending_non_inert_base_` is set (meaning `context.ReprocessChanged` just ran), also add to `dirty_segments_` all segments in `TriggeringSegmentToConditions(*pending_non_inert_base_)` and in the post-reprocessing conditions of `pending_invalidated_glyphs_` (intersected with `candidate_segments_`).
   * Increment `++current_epoch_`. For each $s \in \text{dirty\_segments\_}$, set `segment_epoch_[s] = current_epoch_` and refresh `cached_agg_prob_[s]`, `cached_exclusive_size_[s]`, and $s$'s position in the probability-ordered active set.
   * Generate new pairwise candidates involving `dirty_segments_` (avoiding duplicate `{u, v}` evaluations when both $u, v \in \text{dirty\_segments\_}$) and any composite condition candidates intersecting `dirty_segments_`, then clear `dirty_segments_`.

---

## Implementation Plan / TODO

- [x] **Commit 1: Add `--experimental_all_pairs_merging` flag, per-segment cost metadata cache, and inert 2-segment fast-path.** Declare `ABSL_DECLARE_FLAG(bool, experimental_all_pairs_merging)` in `ift/encoder/merger.h` (default `false`), add cached per-segment aggregate probability and exclusive patch size vectors on `Merger`, and add an $O(1)$ pre-check + fast-path for 2-segment inert cost delta evaluations in `CandidateMerge::ComputeCostDelta` (with unit tests in `ift/encoder/candidate_merge_test.cc`).
- [x] **Commit 2: Implement the lazy two-stage all-pairs priority queue and probability-ordered candidate generation in `AllPairsMerger`.** Add `AllPairsMerger` (`ift/encoder/all_pairs_merger.h`/`.cc`) with `QueuedCandidate`, probability-ordered active segment tracking with `BestCaseInertProbabilityThreshold` row pruning, composite condition candidate queuing, and the lazy `BEST_CASE` $\rightarrow$ `EXACT` pop-and-refine loop for Phase 1 cost-reducing merges.
- [x] **Commit 3: Implement epoch-based invalidation and incremental `dirty_segments_` refresh across `TryNextMerge()` calls.** Track affected segments for both inert and non-inert merges, refresh `dirty_segments_` at the start of `TryNextMerge()` after `SegmentationContext::ReprocessChanged()`, and reset all-pairs state in `Merger::ReassignInitSubset()`.
- [ ] **Commit 4: Integrate Phase 2 (`min_group_size` enforcement) and add unit and determinism tests.** Transition to Phase 2 for any undersized segments remaining after Phase 1 completes, and add tests in `ift/encoder/candidate_merge_test.cc` and `ift/encoder/closure_glyph_segmenter_test.cc` verifying that `--experimental_all_pairs_merging=true` avoids single-base greedy traps, handles composite conditions / `min_group_size` / `UNICODE_RANGE` mode, and is deterministic.
