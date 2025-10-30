
# Merging in the Closure Glyph Keyed Segmenter

Author: Garret Rieger  
Date: Oct 27, 2025
   
## Introduction

Merging is a sub-problem of the overall [glyph segmentation problem](closure_glyph_segmentation.md).
The goal of merging is to take a fine grained glyph segmentation (eg. with one segment per
codepoint) and improve it's overall performance by merging segments and/or patches
together. Merging, if performed carefully, can improve performance by reducing the total number of
patches needed by end users and thereby reduce per patch network and format overhead, and increase
compression efficiency. However, if not done carefully it can make the overall performance of a
segmentation worse. For example if a low frequency segment is merged together with a high frequency
one this will cause the data for the low frequency segment to be loaded much more often then prior
to the merge.

The merging process takes a valid glyph segmentation, analyzes it to find merging opportunities and
then performs the merges to produce a new more performant segmentation that still respects the
closure requirement.

This document provides a detailed description of the specific merger implementation that is used
as part of the [closure glyph segmenter](closure_glyph_segmentation.md).

## Implementation Status

The current merging implementation is far enough along to produce working and performant segmentations.
The IFT fonts used in [IFT Demo](https://garretrieger.github.io/ift-demo/) are produced using
codepoint frequency data and the current merger implementation without utilizing hand tuned subsets
like prior versions of the demo.

However, there are still some major areas that are unimplemented. The biggest missing piece is support
for doing merging with multiple overlapping scripts. This will be needed to produce segmentations
that support more than one CJK scripts simultaneously.

Additionally, the merger requires some manual configuration (via segmenter_config.proto) to produce good
results. Configuration involves selecting the sets of frequency data to utilize and configuring various
thresholds that trade off analysis speed vs segmentation quality. The eventual goal is to have merger
configuration selected automatically based on the input font.

A more complete list of future areas for development can be found in the last section of this document.

## Segmentation Cost

In order to assess the impact of merges we need a way to quantify the performance of a candidate
segmentation. A straightforward metric is to calculate the expected number of bytes transferred
on average by a specific glyph segmentation.

A glyph segmentation, $S$, is given by a list of patches $p_1, ..., p_n$ where each patch $p_i$ has
an activation condition $c_i$ which describes when the patch is needed. The expected number of
bytes transferred for a single page view is given by:

$\text{cost} (S) = \sum P(c_i) \times (\text{size} (p_i) + k)$

Where:
* $P(c_i)$ is the probability that an activation condition is activated by a random page view. Later
  sections discuss different approaches for estimating these probabilities.
* $\text{size} (p_i)$ is the size of the glyph keyed patch in bytes (post Brotli compression).
* $k$ is the fixed overhead cost of a network request in bytes. It's a tunable parameter, by default
  set to 75 bytes.

## Algorithm Overview

At a high level the merging algorithm works like this:

1. Given a codepoint segmentation, the segmenter generates an initial glyph segmentation.
2. The initial glyph segmentation is analyzed to see if any patches should be moved into the initial
   font.  If good candidates are found the glyphs are moved and the glyph segmentation is
   recalculated.
3. Each pair of patches is analyzed to see if merging them would lower the overall cost.
4. The pair which reduces the cost by the largest amount is selected and merged.
5. The glyph segmentation is updated to reflect the merge.
6. Step 3, 4, and 5 are repeated until no merges remain that would reduce overall cost.

The remaining sections discuss the various pieces in more detail.

## The Details

### Merge Types

There are two distinct type of merges that the merger can choose to make:

* Segment Merge: a segment merge joins two or more input segments (codepoint/feature based)
  together via union to produce a single new input segment. The glyph segmentation is recomputed
  with respect to this new input segment. This typically results in the patches related to the
  merged input segments becoming one single patch. For example consider a case where there are two
  input segments one with the 'f' codepoint and the other with 'i'. In the glyph segmentation there
  are three patches. One with the f glyph, one with the i glyph, and one with the fi ligature glyph.
  Joining {f} and {i} input segments into {f, i} would result in the glyph segmentation containing
  only one patch with all three glyphs.
  
* Patch Merge: a patch merge is a more targeted merge that joins together two or more specific
  patches.  Only the patches being merged are joined, all other patches are unaffected. A patch
  merge is executed by removing the patches to be merged and producing a new patch which has the
  union of their glyphs.  The new patch is assigned an activation condition which is the union of
  the merged patches conditions.  In the example from segment merge, the patch with the f glyph
  could be merged with the patch with the i glyph to produce a new patch containing both glyphs and
  an activation condition of (f OR i). The patch with the fi ligature glyph would be left
  untouched. Patch merges are useful in situations where a segment merge would pull together (via
  closure) too many unrelated/low frequency patches.

For patch pairs involving only exclusive patches the merger will use only a segment merge. For patch
pairs that involve at least one non-exclusive patch (those with two or more segments in the activation
condition) the merger evaluates the cost reduction of performing either merge type and utilizes the
merge type which reduces cost the most.

### Initial Font Merging

When there are patches with a near 100% probability of being needed it can be quite beneficial to
move these into the initial font. This eliminates various bits of overhead associated with having
the glyphs as a conditionally loaded patch, eliminates a network round trip so they are immediately
available to the user, and because they are nearly always needed there's no negative impact in
having them always loaded.

Before the more general merging procedure all patches in the initial glyph segmentation are analyzed
to see what the expected cost reduction is to move the glyphs of that patch into the initial font.
Any patches where the reduction is significant (configurable threshold) are moved to the initial font.

Additionally the merger may be configured to also move the fallback glyphs into the initial font as
these by definition are always needed.

### Assessing Cost Deltas

The core part of the merging algorithm is assessing a candidate merge to see how it would affect the
overall segmentation cost. Since each patch contributes to the cost function independently we can
easily compute the change to the total cost function by looking at only the patches affected by a
candidate merge. In the merging algorithm implementation this is referred to as a 'cost delta'.

A merge can have the following effects:

1. Modify the glyphs in a patch (removal or addition).
2. Remove or add patches.
3. Modify the activation conditions of a patch.

To find the delta we locate all existing patches that will be affected by the merge and subtract
their cost.  Next for any of those that are being modified but not removed add back the cost
associated with the modified versions.  Lastly add the cost associated with any new patches.  To
locate affected patches we utilize the current activation condition list to find other patches which
have conditions interacting with the segments to be merged.

Since this relies on the discovered activation conditions this approach fails to capture changes
where glyphs in the fallback segment become part of a discoverable activation conditions as a result
of the merge.  In these cases the cost deltas will be overestimated (less cost reduction). More work
is needed to improve the accuracy of the cost delta calculation for this case. In practice this
affects very few patch pairs so doesn't have a large impact on quality.

Patch sizes used in the delta computation are computed by actually forming the glyph keyed patches
including applying brotli compression. Using real compression to get patch sizes allows the merger
to account for cases where particular glyphs have redundant data which brotli compression can
capitalize on.

In the case of initial font merges the cost delta also adds the number of bytes the initial font
increases by to the delta value.

### Estimating Activation Condition Probabilities

To compute costs we also need to have an estimate for the probability of an activation condition being activated.
An activation condition is a boolean condition involving the presence of one or more codepoints or features. See
the [closure glyph segmentation document](closure_glyph_segmentation.md) for more details.

If we know the probabilities that codepoints are present on web pages we can use standard probability
conjunction/disjunction rules to estimate the probability of an overall condition matching. This approach
requires data on the frequency of codepoint occurrence across the web. The merger utilizes the data set
in https://github.com/w3c/ift-encoder-data.

Two different approaches are utilized for estimating probabilities:

1. Frequency unigrams: only data on the individual occurrence of codepoints is utilized. Information
   on co-occurrence is not utilized. This makes the assumption that the individual codepoint
   probabilities are independent (which is likely not true in reality). Conjunctions can be
   computed by multiplying together the individual probabilities. Disjunctions are computed by
   inverting the product of the probabilities of not seeing the individual codepoints. This approach
   is simple, fast, and only requires a data set with individual codepoint frequencies. However, due
   to the not entirely valid assumption of independence it has no guarantees of generating the actual correct
   probabilities.
   
2. Frequency bigrams: this is a more sophisticated approach which utilizes code point pair
   probabilities (ie. $P(a \cap b)$, the probability that a page has both codepoints 'a' and 'b'
   present) to produce a more rigorous probability estimate. Unlike the unigram approach this does
   not make an assumption of independence. Since it only utilizes bigrams, and not trigrams (and so on)
   we are limited to producing a probability bound instead of a single probability value. For disjunctions
   across multiple codepoints we use formula (3) and (4) from
   [Bounds for the Probability of a Union, with Applications](https://projecteuclid.org/journals/annals-of-mathematical-statistics/volume-39/issue-6/Bounds-for-the-Probability-of-a-Union-with-Applications/10.1214/aoms/1177698049.full) 
   to generate the upper and lower bounds. For segment conjunctions a simpler approach is used since
   we don't have pairwise segment probabilities: $\sum P(s_i) - (n - 1) \leq P(intersection) \leq min(P(s_i))$.
   The downside to the bigram approach is that it is significantly more computationally costly then the unigram
   approach.
   
The merger has both methods implemented and allows the approach to be selected via configuration
depending on the specific needs of a particular segmentation run.

When computing cost with probability bounds a single value needs to be selected for use in the calculation.
There are three straightforward choices:
1. Use the lower bound.
2. Use the upper bound.
3. Use the average of lower and upper.

Currently in the implementation option (1) is used, but this remains an open question which needs more research
of which of 1, 2, or 3 tends to produce the best results.

### Multiple Frequency Data Sets and Merge Groups

Code point frequency data is typically collected within the scope of a particular language and/or
writing script. When doing merge optimization for a font that supports multiple languages/scripts,
then multiple sets of frequency data may be used as inputs to the merging process.

There are two primary challenges that arise when more than one codepoint frequency data set is used:

1. For codepoints which are unique to a single frequency data set we don't have frequency
   information for pairings with codepoints in other frequency data sets. For example if we have
   frequency data for latin codepoints and cyrillic codepoints, we would be missing frequencies for
   pairs involving 1 latin and 1 cyrillic codepoint.
   
2. For codepoints which occur in multiple frequency data sets we now have multiple available
   frequency values and need a way to determine how to select one and/or aggregate them. For example
   say we had a font which supports both Japanese and Chinese and we'd like to produce a
   segmentation which works equally well for each. Both languages utilize many shared codepoints
   (see [han unification](https://en.wikipedia.org/wiki/Han_unification)) but they occur with vastly
   different frequencies depending on the language.
   
When producing segmentations we aim to treat each language with equal importance. For example we
could solve some of the above two issues by normalizing the frequency data sets with each other
using language prevalence data, but that would unfairly optimize segmentations for high use
languages at the cost of performance for lower use ones.

Note: this is an active area of development, so the rest of this section describes a speculative
solution to the problem.

The proposed solutions is to modify the total cost function for a segmentation to evaluate costs
against each frequency data set independently (each weighted equally):

```
total cost(glyph segmentation) =
  cost(glyph segmentation evaluated against language 1 frequencies) +
  ...                                                               +
  cost(glyph segmentation evaluated against language 2 frequencies)
```

The costs delta computations will be modified to compute a delta against each language/script which
intersects the merge being considered. In cases where a pair probability is needed that spans two
languages or isn't present in the current data set, the probability can be considered to be 0 or
near zero.

The current implementation only implements support for mostly disjoint scripts/languages. For each
frequency data set we identify the set of patches which involve only codepoints that exclusively
belong to that frequency data set.  Merges are only assessed for pairs of patches in this set. As a
result, we won't ever need to compute probabilities which span frequency data sets. This approach
appears to work well in practice for mostly disjoint writing scripts (for example Latin and
Cyrillic).

Future development will add support for evaluating multiple deltas when dealing with patches that
interact with multiple frequency data sets.

For initial font merges it does not always make sense to consider all languages for merging into the
initial font. Languages that are expected to be used infrequently should not have their high
frequency codepoints moved into the initial font as this will cause them to be always loaded
regardless of the language currently in use. To deal with this the configuration for the merging
process has a setting to opt-in a frequency data for initial merging. This allows specific languages
to be prioritized for placement into the initial font depending on the intended use case for the
specific font. Eventually we want to find a way to automate this.

## Practical Matters

This section describes some of the practical implementation concerns and optimization techniques used
in the current merger implementation.

Brotli compression for determining the sizes of patches resulting from merges is currently where the
vast majority of time is spent by the merging algorithm. If the high level algorithm described above
is implemented as specified this requires that at least $O(n^2)$ Brotli compression operations are
performed. Where $n$ is the number of patches in the initial segmentation.

As a result the focus of most optimization implemented so far are around reducing the total number
of Brotli operations needed.

### Inert Segments

In a segmentation it's common that there are segments which are inert. Inert segments, for the
purpose of glyph closure, don't interact with any other segments. If two inert segments are merged,
then the glyph closure of the new segment will always be exactly the union of the exclusive glyphs from
the two segments.

Inert segments are detected during closure analysis by looking for segments that have only exclusive
glyphs and do not show up in any other AND or OR conditions.

We can exploit the inertness of segments to make optimizations to the merger. In particular
when merging two or more segments that are all inert, we know the new merged segment will
also be likely inert. This can be exploited to simplify various operations involved in assessing
and applying merges.

For example: because we know inert segments don't interact we can accumulate multiple inert
candidate merges in a single pass and apply the merges as a batch. This is possible because merging
two inert segments will have little to no impact on the cost delta associated with a merge of two
different inert segments. This is currently used to great effect during the initial font merging
phase, and I plan to explore utilizing the same technique during candidate merge selection.

### Best Case Probability Threshold

When assessing a particular merge, there are cases where it's pretty obvious that the merge is not
going to produce a reduction in cost. If we can identify these cases then we can reject them early
and skip computing the merged patch size, and the resulting Brotli compression operation.

To find candidates which can be skipped, we consider what the best possible outcome of a merge is.
Given two existing patches of size A and B, then the best possible outcome of compressing a combined
patch would be the data from one patch is entirely redundant and gets encoded for free. Then the
only increase in size comes from a small increase in overhead in the uncompressed portion of the
patch header. The best case size is defined as:

```
best case size = max(size(A), size(B)) + patch header overhead
```

Given the best case merged size, we can then compute the best possible cost delta that could be
realized for a candidate merge. From there we can prune candidates which can't possibly result in a
negative cost delta.

Currently for ease of calculations we limit this optimization to apply only to merges involving
inert segments; however, the approach should be expanded to cover all patches.

### Low Impact Cutoff

Many scripts have a long tail of code points that are very infrequently needed. Patches that
are conditional on those low frequency code points have a very low probability of being used.
Since the cost contribution of a patch is Probability * Size, patches with near zero
probabilities contribute almost nothing to the total cost. Therefore as an optimization we
should avoid spending excessive effort in optimizing patches that contribute little to
the overall cost.

The merger implementation has a configurable optimization cutoff where the set of patches
whose total contribution to the overall cost is less than the threshold are ignored 
when looking for merge candidates. These patches are only considered for merging when
minimum group sizes need to be reached (discussed in a later section). When merging is needed
for low impact patches the merge selection picks the next available patch to merge instead
of searching for the lowest delta pairing.

This cutoff effectively reduces the size of $n$ in the $O(n^2)$ brotli operation count.

### Brotli Quality

Since Brotli is a performance bottleneck the merger allows the quality level used to be
configured. Running at a lower quality of 8 or 9 can significantly speed up run times
versus using quality 11. However, this comes with the downside that it makes the 
cost deltas less accurate. This can in turn impact the overall quality of the segmentation.
Lower qualities will typically overestimate patch sizes resulting in less merging than
when run with a higher quality.

### Incremental Closure Updates

When non-inert merges are made the closure analysis needs to be repeated to reflect the effects of
the merges. Fortunately we can utilize the current closure analysis results to identify which
patches and glyphs will be affected by the merge.

The current implementation allows for a closure analysis to be partially invalidated on a set of
glyphs and segments and then only recompute the closure analysis for those.  This significantly
reduces the cost of closure analysis on each iteration of the merging algorithm.

### Avoid the 'Except Segment' in Closure

While closure analysis is currently dwarfed by brotli compression times, it is still costly overall
and there are some additional improvements that could be made. The technique described in this
section is not yet implemented, but is a planned improvement.

During closure analysis the most costly part is evaluating the glyph closure of the 'except
segment'. The except segment is the union of all segments except for the one being analyzed. The
except segment closure is primarily needed to locate glyphs needed via conjunctive conditions.

In harfbuzz glyph closure there are currently only two sources for conjunctive conditions: GSUB and
cmap 14. Any glyphs which do not appear in anyway in either of these two tables cannot have any
conjunctive conditions. Furthermore since the GSUB and cmap14 closure stages happen prior to
composite glyph, MATH, and COLR glyph expansions, we can identify codepoints which will not be
subject to conjunctive conditions by checking if the glyphs they map are present in the non
GSUB/cmap14 glyph set. Finally we can then identify input segments which won't have conjunctive
conditions, and skip the except segment portion of the closure analysis for them. Some additional
changes are needed as well to identify disjunctive conditions without the except segment, but that
is doable since since disjunctive glyphs will always appear in the closure of a segment.

More work is needed to validate if the approach described here will work in practice and if the
claims are valid with respect to the harfbuzz glyph closure implementation. Once we're confident in
the validity implementation will be needed. We can currently locate the set of glyphs that are
present in a GSUB table using the harfbuzz
[hb_ot_layout_lookup_collect_glyphs](https://github.com/harfbuzz/harfbuzz/blob/4d1ce4a6554af5219f2ccf85a0a67c552122b46b/src/hb-ot-layout.h#L343)
method. New harfbuzz functionality may be needed to find the set of glyphs in the cmap14 sub table.

### Merging with No Frequency Data

For a particular font when segmenting and merging there may be codepoints in the font that are not covered by the
supplied frequency data. We'd still like to merge these, but don't have frequency data to guide
the merging process. For these an alternative heuristic based merging strategy is used instead.
For now the heuristic is pretty straightforward:

1. We have a configured minimum patch and maximum patch size.
2. The merger tries to increase the size of any patches below this size.
3. Candidates for merging are pairs of exclusive patches, or the list of segments involved
   in composite activation conditions.
4. Since we don't have frequency info we don't have much to distinguish the candidates, so
   the first encountered candidate is tried and used as long as it doesn't raise patch
   size beyond a configured maximum.

### Minimum Group Sizes

The IFT specification recommends a [minimum group
size](https://w3c.github.io/IFT/Overview.html#encoding-privacy) for patch activation conditions to
help preserve privacy. The current merger implementation has this as a configurable setting. When no
minimum group size is configured the merger will never select a merge which has a positive cost
delta. When a minimum group size is configured then the merger will accept positive cost delta
merges for patches that do not meet the configured minimum group size. When merging to meet minimum
group sizes the merger will still seek out the lowest, least positive, cost delta candidate.

Minimum group sizes are currently only assessed for exclusive patches, more work is needed to assess
if patches with composite activation conditions meet minimum group sizes. This is straightforward
for disjunctive conditions, but a little more complicated for conjunctive conditions.

### Caching

Caching is used throughout the merger implementation to accelerate slow operations. The
following caches are used:
* Patch size cache: caches a mapping from glyph set to associated patch size. Helps reduce calls to brotli.
* Glyph closure cache: caches a mapping from subset definition to glyph set from computing glyph closure
  on the subset definition.
* Activation Probabilities: are cached with on the Segment objects. We may also want to consider caching
  activation condition probabilities.

## Areas for Further Development

Two main areas of focus remain for further development of the merger implementation:

1. Continue to improve performance, particularly for CJK fonts which suffer the most from the $O(n^2)$
   brotli operations runtime. This will mostly entails finding more ways to prune out patch size estimation
   operations from the analysis. Some promising avenues are more aggressive use of inertness and more 
   aggressive best case threshold pruning. An additional avenue for improving performance is introducing
   parallelization to the implementation which is currently single threaded. The algorithm
   should be relatively amenable to parallelization, though I'd like to leave parallelization as a last
   resort since it would add significant complexity.
   
2. Implementing missing functionality and improving the quality of produced segmentations:
   * Support for merging scripts that overlap.
   * Better minimum group size handling.
   * Explore the best approach to utilizing probability bounds in cost computations.
   * Better support for layout features: segments with layout features are currently supported, but
     we do not yet have support for integrating frequency data for layout features. As a result
     merging of segments with layout features are handled exclusively by the heuristic merger.
   * Automatic configuration: implement analysis of the input font to generate the segmenter configuration
     automatically.
   * More accurate cost delta assessment, particularly including interactions with the fallback patch.
