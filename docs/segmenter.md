# Glyph Keyed Patch Segmentations during IFT Encoding

Author: Garret Rieger  
Date: Aug 5th, 2026  
Updated: Aug 5th, 2026

## Introduction

A key part of encoding an [IFT](https://w3c.github.io/IFT/Overview.html) font is splitting the
outline (eg. glyf, gvar) data into a set of patches which are loaded by the client as needed. Each
patch contains the data associated with one or more glyph ids. Patch loads are triggered by a client
based on which code points and layout features are being rendered.

Fonts can contain complex substitution rules that can swap the glyphs in use depending on the
specifics of the text. Thus when generating a segmentation for glyphs across patches it must be done
in a way that ensures the correct glyphs for the client's text are available.

In an IFT font each patch has an associated activation condition which is a boolean expression using
conjunction and disjunction on the presence of Unicode code points and layout features.
(eg. $(a ∨ b ∨ c) ∧ (d ∨ e ∨ f)$ ). Note: this implementation has chosen to represent conditions
throughout using [conjunctive normal](https://en.wikipedia.org/wiki/Conjunctive_normal_form) form.

To illustrate the problem here's a common example: let's say we have a Latin font which contains a
ligature which replaces the individual glyphs for `f` and `i` with the `fi` ligature glyph. We decide
to place the `f` glyph in one patch and the `i` glyph in another. When the client is rendering text with
both an `f` and `i` character it will load both patches, however this makes it possible for the `fi` glyph
to be used. Thus we need to make the `fi` glyph available in this case. One way to do that is to
form a third patch containing the ligature glyph and assign the activation condition (`f and i`).

Because the IFT font will use Unicode code points in the activation conditions, it will be typical
to express a desired segmentation of the original font using Unicode code points. The remainder of
this document describes a procedure generating a code point segmentation and the corresponding set
of glyph patches with load conditions described in terms of those Unicode code points.

The code that implements the procedures in this document can be found in
[closure_glyph_segmenter.cc](../../ift/encoder/closure_glyph_segmenter.cc)

There is also a command line utility to generate segmentations:
[util/gen_ift_segmentation_plan.cc](../../util/gen_ift_segmentation_plan.cc)

## Concepts

*Segment*: a segment is the fundamental unit that the font is extended by. Each segment is defined by a set
of code points and layout features. When a font is extended to support a particular segment
then all glyphs needed to render any text which is a subset of that segments (and any previously loaded ones)
will be loaded into the font.

*Initial Font*: An IFT font can choose to include zero or more glyphs in the font that is initially loaded by
the client. These glyphs are then always available and don't need to be patched in. This is useful for glyphs
that are needed with near 100% probability as it eliminates all of the overhead associated with having them
separately in a patch.

*Segmentation Plan*: the output of the segmenter. It is composed of three main things:

1. Initial font definition: the set of glyphs to be included in the initial font.

2. Segment definitions: a list of one or more segments and the codepoints/layout feature sets that they are composed
   of.

3. Patches: each patch is defined by a set of glyph ids and the activation condition for when the patch is needed.
   The condition is a conjunctive normal form boolean expression in terms of segments.

For this particular segmenter implementation a segmentation plan's segment and patch definitions are always
disjoint. As a result each glyph is only found in exactly one patch. Segmentation plans can be encoded
in protobuf format using [segmentation_plan.proto](../ift/config/segmentation_plan.proto)

*Closure Requirement*: segmentation plans produced by the segmenter must meet the "closure requirement":
The set of glyphs contained in patches loaded for a font subset definition (a set of Unicode
code points and a set of layout feature tags) through the patch map tables must be a superset of
those in the glyph closure of the font subset definition.

## Segmenting Algorithm Overview

The high level segmenting algorithm works as follows:

1. Start with each Unicode code point and layout feature support by a font as an individual segment.
2. Analyze glyph conditions with respect to the starting segments. This generates per glyph
   an activation condition in terms of segments.
3. Group glyphs which have identical activation conditions, this forms the initial patch set.
4. Iteratively merge patches into the initial font (guided by a cost function).
4. Iteratively merge segments and/or patches (guided by a cost function) to improve the segmentation
   and reduces overhead.

In the proceeding sections each of these steps will be discussed in more details.

## Condition Analysis

Detecting the conditions under which a glyph is needed can be done in a couple of different
ways. The segmentation implementation currently has two main approaches:

1. Infer glyph conditions via [font subsetting closure](experimental/closure_glyph_segmentation.md).
2. Infer glyph conditions via a [glyph dependency graph](dependency_graph.md) and [dependency graph condition extraction](dependency_graph_condition_extract.md).

Both approaches ultimately aim to detect conditions that each glyph is needed which will satisfy the glyph closure
requirement. The dependency graph is the default and recommended method for finding conditions. It's generally faster
and produces more detailed conditions.

## Merging

Merging is a sub-problem of the overall segmentation problem.  The goal of merging is to take a fine grained glyph
segmentation (eg. with one segment per codepoint) and improve it's overall performance by merging segments and/or
patches together. Merging, if performed carefully, can improve performance by reducing the total number of patches
needed by end users and thereby reduce per patch network and format overhead, and increase compression
efficiency. However, if not done carefully it can make the overall performance of a segmentation worse. For example if a
low frequency segment is merged together with a high frequency one this will cause the data for the low frequency
segment to be loaded much more often then prior to the merge.

The merging process starts with a valid glyph segmentation, analyzes it to find merging opportunities and then performs
the merges to produce a new more performant segmentation that still respects the closure requirement.

### Segmentation Cost Function

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
  set to 200 bytes.

### Segment Merging

A segment merge joins two or more segments together via union to produce a single new segment. The glyph segmentation is
recomputed with respect to this segment. This typically results in the patches related to the merged segments becoming
one single patch. For example consider a case where there are two segments one with the `f` codepoint and the other with
`i`. In the glyph segmentation there are three patches. One with the `f` glyph, one with the `i` glyph, and one with the
`fi` ligature glyph.  Joining {f} and {i} input segments into {f, i} would result in the glyph segmentation containing
only one patch with all three glyphs.

### Patch Merging

A patch merge is a more targeted merge that joins together two or more specific patches.  Only the patches being merged
are joined, all other patches are unaffected. A patch merge is executed by removing the patches to be merged and
producing a new patch which has the union of their glyphs. The new patch is assigned an activation condition which is
the union of the merged patches conditions.  In the example from segment merge, the patch with the f glyph could be
merged with the patch with the i glyph to produce a new patch containing both glyphs and an activation condition of (f
OR i). The patch with the `fi` ligature glyph would be left untouched. Patch merges are useful in situations where a
segment merge would pull together (via closure) too many unrelated/low frequency patches.

### Initial Font Merging

When there are patches with a near 100% probability of being needed it can be quite beneficial to
move these into the initial font. This eliminates various bits of overhead associated with having
the glyphs as a conditionally loaded patch, eliminates a network round trip so they are immediately
available to the user, and because they are nearly always needed there's no negative impact in
having them always loaded.

Before the more general merging procedure all patches in the initial glyph segmentation are analyzed
to see what the expected cost reduction is to move the glyphs of that patch into the initial font.
Any patches where the reduction is significant (configurable threshold) are moved to the initial font.

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

Patch sizes used in the delta computation are computed by actually forming the glyph keyed patches
including applying brotli compression. Using real compression to get patch sizes allows the merger
to account for cases where particular glyphs have redundant data which brotli compression can
capitalize on.

In the case of initial font merges the cost delta also adds the number of bytes the initial font
increases by to the delta value.

### Estimating Activation Condition Probabilities

To compute costs we also need to have an estimate for the probability of an activation condition being activated.
An activation condition is a boolean condition involving the presence of one or more codepoints or features.

If we know the probabilities that codepoints are present on web pages we can use standard probability
conjunction/disjunction rules to estimate the probability of an overall condition matching. This approach
requires data on the frequency of codepoint occurrence across the web. The merger utilizes the data set
in [w3c/ift-encoder-data](https://github.com/w3c/ift-encoder-data).

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

Currently in the implementation option (3) is used, but this remains an open question which needs more research
of which of 1, 2, or 3 tends to produce the best results.

## Multiple Frequency Data Sets and Merge Groups

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
specific font.

### Merging Practical Matters

This section describes some of the practical implementation concerns and optimization techniques used
in the current segmenter implementation.

Brotli compression for determining the sizes of patches resulting from merges is currently where the
vast majority of time is spent by the merging algorithm. If the high level algorithm described above
is implemented as specified this requires that at least $O(n^2)$ Brotli compression operations are
performed. Where $n$ is the number of patches in the initial segmentation.

As a result the focus of most optimization implemented so far are around reducing the total number
of Brotli operations needed.

#### Quality Levels

Many of the optimizations described in the proceeding sections are configurable to allow performance/quality trade offs
to be made. To help ease the configuration of the segmenter we define a set of quality levels (like is typical in
compression libraries) that automatically configure the various optimization settings.  Where lower quality levels
result in faster segmentation, but produce less optimized results.  Additionally, the auto configuration module is also
capable of automatically selecting a reasonable quality level based on the number of codepoints found in the input font.

More details about the specific quality level definitions can be found in
(auto_segmenter_config.cc)[../ift/config/auto_segmenter_config.cc]

#### Inert Segments

In a segmentation it's common that there are segments which are inert. Inert segments, for the
purpose of glyph closure, don't interact with any other segments. If two inert segments are merged,
then the glyph closure of the new segment will always be exactly the union of the exclusive glyphs from
the two segments.

Inert segments are detected during condition analysis by looking for segments that have interact with
only their base segment.

We can exploit the inertness of segments to make optimizations to the merger. In particular
when merging two or more segments that are all inert, we know the new merged segment will
also be inert. This can be exploited to simplify various operations involved in assessing costs
and applying merges.

For example: because we know inert segments don't interact we can accumulate multiple inert
candidate merges in a single pass and apply the merges as a batch. This is possible because merging
two inert segments will have little to no impact on the cost delta associated with a merge of two
different inert segments. This is currently used to great effect during the initial font merging
phase.

#### Best Case Probability Threshold

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

#### Low Impact Cutoff

Many scripts have a long tail of code points that are very infrequently needed. Patches that
are conditional on those low frequency code points have a very low probability of being used.
Since the cost contribution of a patch is `Probability * Size`, patches with near zero
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

#### Brotli Quality

Since Brotli is a performance bottleneck the merger allows the quality level used to be
configured. Running at a lower quality of 8 or 9 can significantly speed up run times
versus using quality 11. However, this comes with the downside that it makes the
cost deltas less accurate. This can in turn impact the overall quality of the segmentation.
Lower qualities will typically overestimate patch sizes resulting in less merging than
when run with a higher quality.

When more substantial speedups are needed brotli compression can be completely disabled
and instead estimated using an expected compression ratio. The expected compression ratio
is calculated by looking at the average compression ratio for glyph data in the input font.
The biggest downside of this approach is that it will completely miss cases where redundant
glyph data is present.

#### Incremental Updates

When non-inert merges are made the condition analysis needs to be repeated to reflect the effects of
the merges. Fortunately we can utilize the current condition analysis results to identify which
patches and glyphs will be affected by the merge.

The current implementation allows for a condition analysis to be partially invalidated on a set of
glyphs and segments and then only recompute the condition analysis for those.  This significantly
reduces the cost of closure analysis on each iteration of the merging algorithm.

#### Merging with No Frequency Data

When segmenting and merging there may be codepoints in the font that are not covered by the
supplied frequency data. We'd still like to merge these, but don't have frequency data to guide
the merging process. For these an alternative heuristic based merging strategy is used instead.
For now the heuristic is pretty straightforward:

1. We have a configured minimum patch and maximum patch size.
2. The merger tries to increase the size of any patches that are below this size and not covered by frequency data.
3. Candidates for merging are pairs of exclusive patches, or the list of segments involved
   in composite activation conditions.
4. Since we don't have frequency info we don't have much to distinguish the candidates, so
   the first encountered candidate is tried and used as long as it doesn't raise patch
   size beyond a configured maximum.

#### Minimum Group Sizes

The IFT specification recommends a [minimum group
size](https://w3c.github.io/IFT/Overview.html#encoding-privacy) for patch activation conditions to
help preserve privacy. The current merger implementation has this as a configurable setting. When no
minimum group size is configured the merger will never select a merge which has a positive cost
delta. When a minimum group size is configured then the merger will accept positive cost delta
merges for patches that do not meet the configured minimum group size. When merging to meet minimum
group sizes the merger will still seek out the lowest, least positive, cost delta candidate.

#### Caching

Caching is used throughout the merger implementation to accelerate slow operations. The
following caches are used:
* Patch size cache: caches a mapping from glyph set to associated patch size. Helps reduce calls to brotli.
* Glyph closure cache: caches a mapping from subset definition to glyph set from computing glyph closure
  on the subset definition.
* Activation Probabilities: are cached on the Segment objects and disjunctive segment sub-group probabilities
  are stored in a dedicated cache.

## Future Work

See this projects [https://github.com/w3c/ift-encoder/issues](issue tracker) for planned future work.
