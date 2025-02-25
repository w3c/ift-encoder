# Converting Unicode Code Point to Glyph Keyed Segmentations during IFT Encoding using Subsetter Glyph Closure

Author: Garret Rieger  
Date: Jan 27, 2025

## Introduction

A key part of encoding an [IFT](https://w3c.github.io/IFT/Overview.html) font is splitting the
outline (eg. glyf, gvar) data into a set of patches which are loaded by the client as needed. Each
patch contains the data associated with one or more glyph ids. Patch loads are triggered by a client
based on which code points and layout features are being rendered.

Fonts can contain complex substitution rules that can swap the glyphs in use depending on the
specifics of the text. Thus when generating a segmentation for glyphs across patches it must be done
in a way that ensures the correct glyphs for the client's text are available.

In an IFT font each patch has an associated activation condition which is a boolean expression using
conjunction and disjunction on the presence of unicode code points and layout features.
(eg. $(a ∧ b ∧ c) ∨ (d ∧ e ∧ f)$ ).

To illustrate the problem here's a common example: let's say we have a Latin font which contains a
ligature which replaces the individual glyphs for f and i with the fi ligature glyph. We decide
to place the f glyph in one patch and the i glyph in another. When the client is rendering text with
both an f and i character it will load both patches, however this makes it possible for the fi glyph
to be used. Thus we need to make the fi glyph available in this case. One way to do that is to
form a third patch containing the ligature glyph and assign the activation condition (f and i).

Because the IFT font will use unicode code points in the activation conditions, it will be typical to
express a desired segmentation of the original font using unicode code points. The remainder of
this document describes a procedure for converting a desired unicode code point segmentation into a
set of glyph patches with load conditions described in terms of those provided unicode segments.

Notably, this document does not aim to describe a solution to producing the unicode code point
segmentation which is of high importance to the production of performant overall glyph
segmentations.

The code that implements the procedures in this document can be found in
[closure_glyph_segmenter.cc](../ift/encoder/closure_glyph_segmenter.cc)

There is also a command line utility to generate segmentations:
[util/closure_glyph_keyed_segmenter_util](../util/closure_glyph_keyed_segmenter_util.cc)

At the end of this document you can find a couple of [examples](#examples) which illustrate how the
procedures work in practice.

## Status

Development of a robust glyph segmentation process that produces performant, low over head
segmentations is an area of active development in the ift encoder. The current prototype
implementation in [closure_glyph_segmenter.cc](../ift/encoder/closure_glyph_segmenter.cc) can produce
segmentations that satisfy the closure requirement, but does not yet necessarily produce ones that
are performant.

The approach laid out in this document is just one possible approach to solving the problem. This
document aims primarily to describe how the prototype implementation in
[closure_glyph_segmenter.cc](../ift/encoder/closure_glyph_segmenter.cc) functions, and is not intended to
present a the final (or only) solution to the problem. There are several unsolved problems and
remaining areas for development in this particular approach:

* Input segmentation generation: selecting good quality input code point segmentations is critically
  important to achieving high quality glyph segmentations. A high quality code point segmentation
  will need to balance keeping interacting code points together with also keeping code points that
  are commonly used together. This document and the implementation make no attempt to solve this
  problem yet.

* Patch merging: is the process of combining patches from a found segmentation together in order to
  reduce overall overhead (eg. if there was a patch containing only one glyph the overhead cost of
  the patch format and network transfer would dominate, therefore it may make sense to merge into
  another similar patch). A very basic patch merging process has been included in the implementation
  but there is lots of room for improvement. Notably, it does not yet handle conditional patch
  merging.  Additionally, a more advanced heuristic is needed for selecting which patches to merge.

* [Multi segment analysis](#multi-segment-dependencies): the current implementation only does single
  segment analysis which in some cases leaves sizable fallback glyph sets. How to implement multi
  segment analysis is an open question and more development is needed.
  
One of the main down sides to this approach is it's reliance on a subsetting closure function which
are computationally costly. Complex fonts which can require hundreds of closure operation which as a
result can be slow to process. So another area of open research is if a non closure based approach
could be developed that is computationally cheaper (for example by producing a segmentation by
working directly with the substitution and dependencies encoded in a font).

## Goals

The segmentation procedure described in this document aims to achieve the following goals:

* Given a desired segmentation of the font expressed in terms of Unicode code points find a
  corresponding grouping of glyphs to be made into a set of patches, where each patch adds the data
  for that set of glyphs.

* Determine the activation conditions for each of those patches. An activation condition is a boolean
  expression using conjunction and disjunction with the presence of Unicode code points being the boolean
  values. The input unicode code point segmentations are used to form the conditions.

* Optimize for minimal data transfer by avoiding duplicating glyphs across patches where possible.

* The chosen glyph segmentation and activation conditions must satisfy the closure requirement:

  The set of glyphs contained in patches loaded for a font subset definition (a set of Unicode
  code points and a set of layout feature tags) through the patch map tables must be a superset of
  those in the glyph closure of the font subset definition.
  
## Subsetter Glyph Closures

Font subsetters run into a similar problem faced here and solve it with an operation called the glyph
closure. This takes a subset definition (set of code points and set of feature tags) and calculates
all glyphs that code be reached by any text using any combination of the input code points.

Closure is a significantly complex process so to save ourselves significant effort we can re-use
existing subsetter closure implementations to help form segmentations in this process.

For the purposes of this document we define a function:

```
closure(font, subset_definition) -> {glyph id set}
```

Where subset_definition is a set of unicode code points and layout feature tags. Subset definitions may
also alternately be referred to as input segments.

## Segment Closure Analysis

This section defines a procedure to analyze an input segment and determine how it interacts with
glyphs in the font. It is an extension of the chunking algorithm in Skef’s [binned IFT
prototype](https://github.com/adobe/binned-ift-reference). Notably it incorporates new capabilities
of the current IFT spec such as the ability to have conjunction and disjunction in activation
conditions for patches.

Given:

* An input font, $F$.
* A list of segments $s_0, …, s_n$. Where:
  * $s_0$ is the subset definition for the initial IFT font.
  * $s_i \neq s_0$ is the segment to be analyzed.

It generates three sets of glyph ids:

1. Exclusive Glyph Set: these glyphs are needed exclusively by this segment. That is they are only
   needed if and only if the $s_i$ is present.
   
2. Conjunctive Glyph Set: the presence of this input segment is a requirement for these glyphs to be
   needed, but there may be additional segments required as well. The condition for the 
   set of glyphs to be needed is: ($s_i ∧ …$).
   
3. Disjunctive Glyph Set: these glyphs are needed when the input segment is present, but the input
   segment is not a requirement for the glyphs to be needed. The condition for the 
   set of glyphs to be needed is: ($s_i ∨ …$)

The process utilizes a subsetter glyph closure computation to group glyphs together based on how
they are activated by the closure.

First compute the following sets:

* Set $A = \text{closure} (F, \bigcup_{0}^{n} s_j)$
* Set $B = \text{closure} (F, \bigcup_{j \neq i} s_j)$
* Set $I = \text{closure} (F, s_0 \cup s_i) - \text{closure} (F, s_0)$, the set of glyphs needed for
  the closure of $s_i$.
* Set $D = A - B$, the set of glyphs that are dropped when $s_i$ is removed.

Then we know the following:

* Glyphs in $I$ should be included whenever $s_i$ is activated.
* $s_i$ is necessary for glyphs in $D$ to be required, but other segments may be required aswell.

Furthermore we can intersect $I$ and $D$ to produce three sets:

1. $D - I$: the activation condition for these glyphs is ($s_i ∧ …)$ where … is one or more additional segments.
2. $I - D$: the activation conditions for these glyphs is $(s_i ∨ …)$ where … is one or more additional segments.
3. $D \cap I$: the activation conditions for these glyphs is only $s_i$.

## Segmenting Glyphs Based on Closure Analysis

For each glyph in the input font we track the following information:

* The glyph is either exclusive to a segment if it appears in the Exclusive Glyph Set of a segment, or;
* The glyph has an associated set of segments which can be either conjunctive or disjunctive.

If the set is conjunctive, then this glyph is needed when all segments in the set are present, and
possibly some additional undiscovered conditions. However, we can ignore these additional conditions
since they are conjunctive and will only further narrow the activation condition. Otherwise if the
set is disjunctive, then the glyph is needed when at least one segment in the set is present or
possibly due to some additional undiscovered conditions. These potential undiscovered conditions are
important, and later on the process will attempt to rule them out.

Run the Segment Closure Analysis on each segment in $s_1, …, s_n$. For each glyph in the:
* Exclusive Glyph Set of $s_i$ - mark that glyph as exclusive to $s_i$.
* Conjunctive Glyph Set of $s_i$ - add $s_i$ to that glyph's conjunctive set of segments.
* Disjunctive Glyph Set of $s_i$ - add $s_i$ to that glyph's conjunctive set of segments.

After performing this analysis for all segments there may be some glyphs that were not associated
with any segments, these have more complicated activation conditions and may need further analysis
which is discussed later on.

Next, we can use the per glyph information to form the glyph groupings:

1. For each unique exclusive segment, $s_i$, collect the associated set of glyphs into a group. This forms
   an exclusive patch whose activation condition is $s_i$. 

2. For each unique conjunctive segment set, $C$, collect the group of glyphs that is marked with that set. This
   group forms a conditional patch whose activation condition is $\bigwedge s_j \in C$.
   
3. For each unique disjunctive segment set, $C$, collect the group of glyphs that is marked with that set. This
   group forms a conditional patch whose activation condition is $(\bigvee s_j \in C) \vee \text{additional conditions}$. As
   noted above we will need to rule out the additional conditions, the process for this follows.

   Compute the Segment Closure Analysis for a new composite segment $\bigcup s_i \notin C$. Remove any glyphs from the
   group that are found in $I - D$. These glyphs may appear as a result of additional conditions and so
   need to be considered unmapped. If the group has no remaining glyphs don’t make a patch for it.
   
4. Lastly, collect the set of glyphs that are not part of any patch formed in steps 1 through 3. These form a fallback patch
   whose activation condition is $\bigvee_{1}^{n} s_j$.
   
## Fallback Patch

An output of the segmentation process above will include a fallback patch which is activated on the
presence of any input segment. In practice it will be slightly more efficient to instead move all of
the glyphs in the fallback patch into the initial font. This has the same overall effect as using a
fallback patch. In some cases the encoder may be purposely trying to minimize and/or eliminate glyph
data in the initial font. In these cases leaving them in the fallback patch may make the most sense.

## Merging

In some cases the patch set produced above may result in some patches that contain a small number of
glyphs. Because there is a per patch overhead cost (from network and patch format overhead) it may
be desirable to merge some patches together in order to meet some minimum size target. Patches can
be merged so long as it's done in a way that preserves the glyph closure requirement.

There are two types of patches that can be merged: exclusive and conditional. The procedure for
merging is dependent on the type.  The next two sections provide some guidelines for merging the two
types together.

### Merging Input Segments

This section outlines a procedure to find and merge input code point segments in order to increase
the sizes of one or more exclusive patches. It searches for other input segments that interact with
the one that needs to be enlarged. Keeping interacting code points together in a single segment since
it reduces the number of conditional patches needed, thereby reducing overall overhead.

Starting with:

* A set of patches and activation conditions produced by the "Segmenting Glyphs Based on Closure
  Analysis" procedure.
* A desired minimum and maximum patch size in bytes.

Then, for each segment $s_i$ in $s_1$ through $s_n$ ordered by expected frequency of use (high to low):

1. If the associated exclusive patch does not exist or it is larger than the minimum size skip this segment.

2. Locate one or more segments to merge:

   a. Sort all glyph patches by their conditions. First on the number of segments in the condition ascending and
      then by the segment frequency (high to low).
      
   b. Return the set of segments in the condition of the first patch from that list where $s_i$
   appears somewhere in the patch’s condition.
   
   c. If no such patch was found then instead select another exclusive patch which has the closest
   frequency to $s_i$ and return the associated segment.

3. Generate a new $s'_i$ which is the union of $s_i$ and all returned segments from step 2. Add it
   to the input segment list.

4. Remove the $s_i$ and all segments from step 2 from all per glyph conditions and the input segment list.

5. Re-run the "Segment Closure Analysis" closure test on $s'_i$ and update per glyph conditions as needed.

6. Based on the updated per glyph conditions, recompute the overall patch and condition sets
   following "Segmenting Glyphs Based on Closure Analysis".  If the new patch for $s'_i$ is larger
   than the maximum allowed size, undo the changes from steps 3-6 and go back to step 2 to continue
   searching for more candidate segments. Ignore the previously selected group.

7. Re-run this process to find and fix the next patch which is too small. If none remain then the
   process is finished.


### Merging Conditional Patches

At a high level two conditional patches can be merged together by creating a new patch containing
the union of the glyphs in the two and assigning a new condition with is a super set of the two
original conditions. Merging patches in this way avoids duplicating glyphs, but results in more
relaxed overall activation conditions meaning some of the glyphs will be loaded when not strictly
needed.

Merged conditions for conditional patches can be created by adding a disjunction between the two
overall conditions. For example if the two conditions were ($s_1 \wedge s_2$), ($s_2 \wedge s_3$)
then a new condition $(s_1 \wedge s_2) \vee (s_2 \wedge s_3)$ could be used for a combined
patch. This condition will activate the combined patch when either of the original conditions would
have matched. When selecting patches to merge, patches that have a smaller symmetric difference
between the segments in their conditions should be prioritized as that will minimize the widening of
the activation condition.

There are also two other options for merging conditional patches, though these are generally less
preferable than the merging procedure described above:

1. Move the patch's glyphs into the initial font or merge with the fallback patch. This removes the
   patch at the cost of always loading the glyph's data. This may be useful when there is a very
   small patch with a wide activation condition. In this case it may not be desirable to merge with
   other larger conditional patches due to excessive widening of their activation conditions.
   
2. Duplicate the patch's glyphs into the segments that make up the patch's condition. This
   eliminates the patch at the cost of duplicating glyph data. It may be useful in cases of small
   patches with narrow activation conditions.
   
More research is needed in this area, and ultimately its likely that a selection heuristic which
takes into account segment frequency to assess the impact of condition widening will need to be
developed.

## Multi Segment Dependencies

Note: this section is somewhat speculative as this functionality has not yet been implemented.
More research and exploration is definitely needed.

The Segmenting Glyphs Based on Closure Analysis procedure places any glyphs whose conditions aren't
discovered into a fallback patch.  Glyphs will end up in the fallback patch if there exists more
complex activation conditions that involve more than one segment at a time. For example consider a
glyph which is added to the closure when code points from ($s_1 \wedge s_2) \vee (s_3 \wedge s_4$)
are present.  This glyph would not show up in either sets $I$ or $D$ for any single segment since
that would require at the union of at least two segments (for example $s_1$ and $s_2$) to be closure
tested. If the fallback patch is large than it may be necessary to analyze the dependencies further
to try and determine what the more complex activation conditions are.

To discover these multi segment dependencies it’s necessary to test all combinations of two or more
segments with the closure analysis. Each combination of segments is unioned together to produce a
new segment and then the closure test is performed and new groups and activation conditions are
formed as described above (these are added to the previously determined patches).

Unfortunately this approach will likely lead to a combinatorial explosion, so mitigations will be
needed to reduce the amount of combinations to test. Some suggestions:

* It's not necessary to test all combinations. Once a sufficient number of fallback glyphs have been
  categorized the combination testing can stop at any point. With remaining glyphs left in the
  fallback patch. Given this, the remaining points provide suggestions for better targeting the
  combinations to test in order to focus on combinations that are likely interacting.

* In most cases complex dependencies should be isolated within a script, and for most scripts (other
  than CJK) there should be a reasonably small number of segments per script. So if we limit the
  multi segment combinations to within a script it should produce a reasonable number of
  combinations to test while still being able to capture most dependencies.

* Analysis of the font’s underlying GSUB rules could yield groups of code points which may possibly
  interact. These could be used to prune segment combinations that don’t share interacting
  code points. Alternatively these groups could be used to guide initial segment selection where
  code points that appear to interact are placed in the same segment, thus getting a higher hit rate
  in the single segment analysis.

* The performance of a segmentation is likely driven solely by the high frequency code points. So
  divide the font into a high frequency set and low frequency set of code points. Where a more
  extensive multi segment dependency check is done for only the high frequency segments.

## Examples

### UVS Selectors

This example demonstrates how the proposed algorithm can detect the appropriate segmentations and activation conditions for a font that has closure glyphs which activate when two segments are simultaneously present.

The original font contains:

* Codepoints `{a, …, z}` and variation selector `VS1`.
* Each of a through z has an alternate form when combined with `VS1`. eg. `a + VS1` subs in glyph `a.alt`
* Four input segments are defined:
  * `s_0 = {A}       # initial font`
  * `s_1 = {a, …, m}`
  * `s_2 = {n, …, z}`
  * `s_3 = {VS1}`

Performing the per segment closure test will have the following results:

| `S` | `A` | `B` | `I` | `D = A - B` |
| :---- | :---- | :---- | :---- | :---- |
| `s1` | `{A, a..z, a.alt..z.alt}` | `{A, n..z,  n.alt..z.alt}` | `{a..m}` | `{a..m, a.alt..m.alt}` |
| `s2` | `{A, a..z, a.alt..z.alt}` | `{A, a..m,  a.alt..m.alt}` | `{n..z}` | `{n..z, n.alt..z.alt}` |
| `s3` | `{A, a..z, a.alt..z.alt}` | `{A, a..z}` | `{}` | `{a.alt..z.alt}` |

And the derived sets:

| `S` | `D - I` | `I - D` | `D n I` |
| :---- | :---- | :---- | :---- |
| `s1` | `{a.alt..m.alt}` | `{}` | `{a..m}` |
| `s2` | `{n.alt..z.alt}` | `{}` | `{n..z}` |
| `s3` | `{a.alt..z.alt}` | `{}` | `{}` |

Based on these we then we assign conditions to each glyph:

| `glyph` | `Exclusive` | `Conjunctive Set` | `DisjunctiveR Set` |
| :---- | :---- | :---- | :---- |
| `a` | `{s1}` | `{}` | `{}` |
| `…` | `…` | `…` | `…` |
| `m` | `{s1}` | `{}` | `{}` |
| `n` | `{s2}` | `{}` | `{}` |
| `…` | `…` | `…` | `…` |
| `z` | `{s2}` | `{}` | `{}` |
| `a.alt` | `{}` | `{s1, s3}` | `{}` |
| `…` | `…` | `…` | `…` |
| `m.alt` | `{}` | `{s1, s3}` | `{}` |
| `n.alt` | `{}` | `{s2, s3}` | `{}` |
| `…` | `…` | `…` |  `…` |
| `z.alt` | `{}` | `{s2, s3}` | `…` |

Lastly, we group glyphs by condition sets to produce the final mappings and glyph keyed patches:

| `Segment Set` | `Mapping Key (codepoints)` | `Patch Contains Glyphs` |
| :---- | :---- | :---- |
| `{s1}` | `{a..m}` | `{a..m}` |
| `{s2}` | `{n..z}` | `{a..z}` |
| `{s1, s3}` | `{a..m} AND {VS1}` | `{a.alt..m.alt}` |
| `{s2, s3}` | `{n..z} AND {VS1}` | `{n.alt..z.alt}` |

### Shared Component

This example demonstrates how the proposed algorithm can detect the appropriate segmentations and
activation conditions for a font that has closure glyphs which activate when at least one of several
segments are present.

The original font contains:

* Codepoints `{a, o, 0xe2, 0xf4}`.
* Simple glyphs 1, 2,  and 5.
* Glyph 3 is a composite glyph using 1 + 5.
* Glyph 4 is a composite glyph using 2 + 5.
* With cmap:
  * a: 1
  * o: 2
  * 0xe2: 3
  * 0xf4: 4
* Two input segments are defined:
  * `s0 = {} # initial font`
  * `s1 = {a, 0xe2}`
  * `s2 = {o, 0xf4}`

Performing the per segment closure test will have the following results:

| `S` | `A` | `B` | `I` | `D = A - B` |
| :---- | :---- | :---- | :---- | :---- |
| `s1` | `{1, 2, 3, 4, 5}` | `{2, 4, 5}` | `{1, 3, 5}` | `{1, 3}` |
| `s2` | `{1, 2, 3, 4, 5}` | `{1, 3, 5}` | `{2, 4, 5}` | `{2, 4}` |

And the derived sets:

| `S` | `D - I` | `I - D` | `D n I` |
| :---- | :---- | :---- | :---- |
| `s1` | `{}` | `{5}` | `{1, 3}` |
| `s2` | `{}` | `{5}` | `{2, 4}` |

Based on these we then we assign conditions to each glyph:

| `glyph` | `Exclusive Set` | `Conjunctive Set` | `Disjunctive Set` |
| :---- | :---- | :---- | :---- |
| `1` | `{s1}` | `{}` | `{}` |
| `2` | `{s2}` | `{}` | `{}` |
| `3` | `{s1}` | `{}` | `{}` |
| `4` | `{s2}` | `{}` | `{}` |
| `5` | `{}`   | `{}` | `{s1, s2}` |

Lastly, we group glyphs by unique set to produce the final mappings and glyph keyed patches:

| `Exclusive Segment Set` | `Mapping Key (codepoints)` | `Patch Contains Glyphs` |
| :---- | :---- | :---- |
| `{s1}` | `{a, 0xe2}` | `{1, 3}` |
| `{s2}` | `{o, 0xf4}` | `{2, 4}` |
| `Disjunctive Set` | `Mapping Key (codepoints)` | `Patch Contains Glyphs` |
| `{s1, s2}` | `{a, o, 0xe2, 0xf4}` | `{5}` |
