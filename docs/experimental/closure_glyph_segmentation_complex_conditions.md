
# Complex Condition Finding in Closure Glyph Keyed Segmenter

Author: Garret Rieger__
Date: Dec 17, 2025

## Introduction

Before reading this document is recommended to first review the
[closure glyph segmentation](./closure_glyph_segmentation.md) document. This document borrows concepts and terms from it.

In closure glyph segmentation the closure analysis step is capable of locating glyph activation conditions that are
either fully disjunctive or fully conjunctive (eg. `(A or B or C)`). It is not capable of finding conditions that are a mix
of conjunction and disjunction (eg. `(A and B) or (B and C)`). These are referred to as complex conditions. By default
glyphs with complex conditions are assigned to a patch that is always loaded, since the true conditions are not known.

This document describes an algorithm which can be used to find purely disjunctive conditions which are supersets of
complex conditions. A superset condition is one that will activate at least whenever the true condition would. This
property allows the superset condition to be used for a patch in place of the true condition without violating the
closure requirement.

For a given complex condition there typically exists more than one possible superset disjunctive condition. The
algorithm will find one of them, but not necessarily the smallest one. The found superset condition will always only
contain only segments which appear in the original condition.

For example if we had a glyph with a activation condition of `((A and B) or (B and C))` then this process will find one
of the possible superset conditions such as `(A or C)`, `(A or B)`, `(B or C)`, or `(B)`. In a segmentation we could
then have a patch with the found condition which loads the glyph and this would satisfy the closure requirement.

## Foundations

The algorithm is based on the following assertions:

1. Given some fully disjunctive condition for a glyph, we can verify that the condition is a superset of the true
   condition for the glyph and meets the closure requirement by the following procedure: compute a glyph closure of the
   union of all segments except for those in the condition. If the glyph does not appear in this closure, then the
   condition satisfies the closure requirement for that glyph and is a superset of the true condition. This is called
   the “additional conditions” check.
   
2. The glyph closure of all segments includes the glyph that we are analyzing.

3. We have a glyph which has some true activation condition. If we compute a glyph closure of some combination of
   segments, then adding or removing a segment, which is not part of the activation condition, to the glyph closure input
   will have no affect on whether or not the glyph appears in the closure output.

4. The closure of no segments contains only glyphs from the initial font.

## The Algorithm

For a glyph with a complex condition we can use the above to find a superset disjunctive condition for that
glyph's complex condition. These conditions will satisfy the closure requirement for each glyph.

### Finding a Sub Condition

The algorithm works by identifying a single sub condition at a time, this section describes the algorithm for
finding a single sub condition.

Inputs:

* Segments to exclude from the analysis.
* `glyph` to analyze.

Algorithm:

1. Start with a set of all segments except those to be excluded, called `to_test`.
2. Initialize a second set of segments, `sub_condition`, to the empty set.
3. Remove a segment `s` from `to_test` and compute the glyph closure of `to_test U sub_condition`.
4. If `glyph` is not found in the closure then add `s` to `sub_condition`.
5. If `to_test` is empty, then return the  sub condition `sub_condition`.
6. Otherwise, go back to step 3.

### Finding the Complete Condition

This section describes the algorithm which finds the complete condition, it utilizes `Finding a Sub Condition`.

Inputs:

* `glyph` to analyze.

Algorithm:

1. Initialize a set of segments `condition` to the empty set.
2. Execute the `Finding a Sub Condition` algorithm with `condition` as the excluded set.
3. Union the returned set into `condition`.
4. Compute the glyph closure of all segments except those in `condition`.
5. If `glyph` is found in the closure, then more sub conditions still exist. Go back to step 2.
6. Return the complete condition, `condition`.

### Initial Font

Any time a closure operation is executed by the above two algorithms it's necessary to union the subset definition for
the initial font into the closure input. This is required because the closure of the initial font affects what's
reachable by the segments.

### Why this works

Here we show this procedure is guaranteed to find a disjunctive superset of a glyph's true condition which includes
only segments from the true condition, when the glyph is not already in the initial font:

* For each call to `Finding a Sub Condition` glyph will be in the closure of all non-excluded segments. For the first
  call this is guaranteed by assertion (2). For subsequent calls this is guaranteed by the "additional conditions" check
  which gates execution.

* Any segments which are not part of the true condition will not impact the glyph's presence in the closure (assertion
  (3)). Further by the previous point we know that at the start of `Finding a Sub Condition` the closure of all
  non-excluded segments will contain glyph. Thus testing a segment which is not part of the true condition will never
  result in glyph missing from the closure, and won't be added to `sub_condition`. Therefore `Finding a Sub Condition`
  will only ever return segments that are part of the true condition.
  
* `Finding a Sub Condition` will always return at least one segment: if when the last segment is tested `sub_condition`
  is still the empty set, then the closure will be on no segments and will not have glyph in it. This is a result of
  assertion (4) and the premise that glyph is not already in the initial font. As a consequence the returned
  `sub_condition` will always have at least one segment in it.
  
* Since all returned segments from `Finding a Sub Condition` are excluded from future calls, there will be a finite
  number of `Finding a Sub Condition` executions which return only segments part of the true condition.
  
* Lastly, the algorithm terminates only once the additional conditions check finds no additional conditions,
  guaranteeing we have found a superset disjunctive condition (assertion (1)).
  
## Making it More Performant

As described above this approach can be slow since it processes glyphs one at a time. Improvements to performance
can be made by processing glyphs in a batch. This can be done with a recursive approach where after each segment test 
the set of input glyphs gets split into those that require the tested segment and those that don't. Each of the splits spawns
a new recursion (if there is at least one glyph in the split).

Also from the closure analysis run by the segmenter we may have discovered some partial conditions for glyphs. These
can be incorporated as a starting point into the complex condition analysis.

More substantial performance improvements could be realized by using a dependency graph generated from the font. This
could come in two forms:

1. Determine what segments interact with GSUB in some way and use that to scope the analysis. Segments that don't
   interact with GSUB can be discovered via regular closure analysis as they will only ever have disjunctive conditions.
   After completing a scoped analysis, a final additional conditions check against all segments could be used to ensure
   we have actually arrived at a superset condition. This is more speculative and would need more research to validate the
   approach.

2. Using a actual dependency graph, even if it's no fully accurate, to generate a set of initial activation
   conditions. Then as needed when additional conditions check fails, we could find any additional segments for the
   superset condition using this process.

## Integrating into the Segmentation Algorithm

Initially the complex condition analysis has been added as a final step after merging. If after merging unmapped glyphs
are present, then the complex condition analysis is run on those glyphs and the fallback patch is replaced with one or
more patches based on the results of complex condition analysis.

However, ideally complex condition analysis would be run before merging so that the patches it generates can participate
in the merging process. This will require incremental updates to the complex condition analysis results, but that should
be straightforward. Implementing this is planned for the near future.



