
# Complex Condition Finding in Closure Glyph Keyed Segmenter

Author: Garret Rieger__
Date: Dec 17, 2025

## Introduction

Before reading this document is recommended to first review the [closure glyph
segmentation](./closure_glyph_segmentation.md) document.

In closure glyph segmentation the closure analysis step is capable of locating glyph activation conditions that are
either fully disjunctive or fully conjunctive (eg. A or B or C). It is not capable of finding conditions that are a mix
of conjunction and disjunction (eg. (A and B) or (B and C)). These are referred to as complex conditions. By default
glyphs with complex conditions are assigned to a patch that is always loaded, since the true conditions are not known.

This document describes an algorithm which can be used to find the complete set of segments which are a part of the
complex condition for a glyph. If the set of segments which are present in a condition is known, then we can form a
purely disjunctive condition using those segments which is guaranteed to be a superset of the true condition. That is it
will always activate at least when the true condition would. This property allows the superset condition to be used for
a patch in place of the true condition without violating the closure requirement.

For example if we had a glyph with a activation condition of ((A and B) or (B and C)) then this process will find the set
of segments {A, B, C} which would form the superset condition (A or B or C). In a segmentation we could then have a
patch with condition (A or B or C) which loads the glyph and this would satisfy the closure requirement. In the future
if we decide to develop an analysis to find the true condition then the segment set found by this process could be used
to narrow down the search space to only those segments involved in the condition.

## Foundations

The algorithm is based on the following assertions:

1. For any complex activation condition of a glyph, a disjunction over all segments appearing in that condition
   will always activate at least when the original condition does.
   
2. Given some fully disjunctive condition, we can verify that condition is sufficient to meet the glyph closure
   requirement for a glyph by the following procedure: compute a glyph closure of the union of all segments except for
   those in the condition. If the glyph does not appear in this closure, then the condition satisfies the closure
   requirement for that glyph. This is called the “additional conditions” check.
   
3. The glyph closure of all segments will include the glyphs that we are analyzing.

4. We have a glyph which has some true activation condition. If we compute a glyph closure of some combination of
   segments, then adding or removing a segment, which is not part of the activation condition, to the glyph closure input
   will have no affect on whether or not the glyph appears in the closure output.

## The Algorithm

For each glyph with a complex condition we can use the above to find the complete set of segments which are part of the
glyph's complex condition. A condition which is a disjunction across these segments will satisfy the closure requirement
for that glyph.

### Finding a Sub Condition

The algorithm works by identifying a single sub condition at a time, this section describes the algorithm for
finding a single sub condition.

Inputs:

* Segments to exclude from the analysis.
* `glyph` to analyze.

Algorithm:

1. Start with a set of all segments except those to be excluded, called `to_test`.
2. Initialize a second set of segments, `required`, to the empty set.
3. Remove a segment `s` from `to_test` and compute the glyph closure of `to_test U required`.
4. If `glyph` is not found in the closure then add `s` to `required`.
5. If `to_test` is empty, then return the  sub condition `required`.
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
5. If `glyph` is found in the closure, then more conditions still exist. Go back to step 2.
6. Return the complete condition, `condition`.

### Initial Font

Any time a closure operation is executed by the above two algorithms it's necessary to union the subset definition 
for the initial font into the closure input. That's because the closure of the initial font affects what's reachable
by the segments.

### Why this works

* Any segments which are not part of the true condition will not impact the glyph's presence in the closure (assertion
  (4)).  As a result they will never be moved into the `required` set and will not be returned by `Finding a Sub
  Condition`.  Thus any segments returned by `Finding a Sub Condition` are part of the true condition.
  
* Each iteration of `Finding a Sub Condition` is guaranteed to select at least one segment since we know that the
  initial closure always starts with the glyph in it, and the closure of no segments will not have the glyph in it.  So
  at some point during the algorithm the glyph must be found to not be present. In the first iteration this is a result
  of assertion (3). For subsequent iterations this is guaranteed by the "additional conditions" check prior to starting
  the iteration.
  
* Since all returned segments from `Finding a Sub Condition` are excluded from future calls, there will be a finite
  number of `Finding a Sub Condition` executions which return only segments part of the true condition.
  
* Lastly, the algorithm terminates only once the additional conditions check finds no additional conditions,
  guaranteeing we have found the complete superset disjunctive condition.
  
## Making it More Performant

As described above this approach can be slow since it processes glyphs one at a time. Improvements to performance
can be made by processing glyphs in a batch. This can be done with a recursive approach where after each segment test 
the set of input glyphs gets split into those that require the tested segment and those that don't. Each of the splits spawns
a new recursion (if there is at least one glyph in the split).

Also from the closure analysis run by the segmenter we may have discovered some partial conditions for glyphs. These
can be incorporated as a starting point into the complex condition analysis.

Furthermore we can reduce the amount of segments we need to test by checking which segments can interact in some way
with the GSUB table. Segments that don't interact with GSUB can't by part of a conjunctive condition, so these can
always be found via the standard closure analysis procedures. Then the search can be limited to just the set of segments
which interact with GSUB and were not identified during regular closure analysis.


## Integrating into the Segmentation Algorithm

Initially the complex condition analysis has been added as a final step after merging. If after merging unmapped glyphs
are present, then the complex condition analysis is run on those glyphs and the fallback patch is replaced with one or
more patches based on the results of complex condition analysis.

However, ideally complex condition analysis would be run before merging so that the patches it generates can participate
in the merging process. This will require incremental updates to the complex condition analysis results, but that should
be straightforward. Implementing this is planned for the near future.



