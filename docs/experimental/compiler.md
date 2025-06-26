# Overview of Compiler Implementation

Author: Garret Rieger
Date: Jun 20, 2025

## Overview

The compiler API and command line utility (font2ift) is a sub module in the overall IFT encoder. It's role is to take a
[segmentation plan](../../util/segmentation_plan.proto) and font, then generate an IFT font and associated patches based on
the segmentations and settings specified in the plan. This document describes how the compiler implementation in this
library works.

The compiler library is a low level process that just executes the provided segmentation plan. Most of the interesting
encoding decisions (for example how to segment a font, and how those segments are activated) are made upstream during
the preparation of the plan.

## Concepts

Before diving into the details of the compilation process this section defines some concepts and terminology used
in the description of the encoding process.

### Initial Subset

The initial font subset is the set of code points, features, and design space that the initial IFT font will cover.
The initial IFT font is the first thing loaded by a client and the entry point to IFT extension. Everything in the initial
font subset will be contained by the initial IFT font, and as a result will not need to be added by extension operations.

### Outline Data vs Non Outline Data

For IFT font encoding data in a font is categorized as either outline data or non outline data. Outline data is any data
related to the glyph outlines and outline variations. Specifically: loca, glyf, gvar, CFF, and CFF2 tables. All other
tables are considered non-outline data.

### Segments

A segment is the smallest unit of extension in the IFT font. The IFT font is extended in the client by adding one or
more segments at a time. A segment is specified as a set of code points, layout feature tags, and/or design space that
will be added to the font. Otherwise know as a [subset
definition](https://w3c.github.io/IFT/Overview.html#font-subset-dfn) in the terminology of the IFT specification.

### Table Keyed vs Glyph Keyed

IFT fonts can be extended by two types of patches: [table keyed](https://w3c.github.io/IFT/Overview.html#table-keyed) and
[glyph keyed](https://w3c.github.io/IFT/Overview.html#glyph-keyed). The encoder utilizes glyph keyed patches to extend
tables containing outline data and table keyed patches are used for tables containing non outline data.

### Mode

The encoder has two primary modes of operation: mixed mode and table keyed only.

Mixed mode is used when an encoding configuration contains glyph data (outline) segments. Under mixed mode the generated
IFT font will include both glyph keyed and table keyed patches. When no glyph data segments are configured then the
encoder will instead generate a font which uses only table keyed patches.

### Table Keyed Patch Graph

Table keyed patches are binary diffs which are relative to a specific base. As a result the set of table keyed patches for
an IFT font effectively forms a graph where each extended state of the font is a node and the patches which move
to a further extended state are edges.

Each node in the graph can be uniquely identified by a subset definition describing the code points, features, and design
space that node covers.

Each edge in the graph can be uniquely identified by listing the start node and end node subset
definitions. Alternatively, they can also be described as a starting node and a subset definition describing what is
added to reach the end node. In this case the end node subset definition is the union of the start and delta.

### Jump Ahead

The default behaviour is to have an outgoing edge on each node for each segment that can be added to that node. However,
since only one table keyed patch can be applied at a time this means that a round trip is needed for each segment being
added by a client. As an optimization the encoder supports a setting called jump ahead. When jump ahead is 2 or higher
then additional edges are added that add 'jump ahead' or more segments at once. This allows single round trip extensions
that add multiple segments at the cost of more total patches in the encoding.

### Prefetch Lists

Prefetch lists are an optional feature in the IFT spec. The typical patch map entry will list a single patch which is
to be applied when that entry is matched. However, the entry can also optionally list additional patches which should
be prefetched. For table keyed patch graphs this allows us to instruct the client to fetch in one round trip multiple
edges of the graph. Concretely this can reduce the total number of patches needed by encoding multi segment edges
using a prefetch list of existing single segment patches instead of adding a new unique patch for that edge.

### Edges and Jumps

In the table keyed patch graph patches may add more than one segment at a time (eg. for jump ahead). For each edge the
encoder tracks one or more jumps, where each jump is the application of a single patch. When prefetch lists are in use an
edge may be reached by applying multiple patches sequentially, hence that edge would have multiple associated jumps.  In
the no-prefetch-list case each jump adds exactly one segment at a time.

A jump is described as a base subset definition and a target subset definition. Where the base is the starting point and
target subset definition is the subset definition reached by following the jump (union of base plus the segment(s) being
added).

For example let's say at the current node (A) there are two segments which can be added: B and C. Further the encoder
has chosen to make available patches which can add up to two segments in a single patch. The following edges would then
be included in the encoding:

*  <code>Edge A -> A+B: contains jump +B (base = A, target = A + B).</code>
*  <code>Edge A -> A+C: contains jump +C (base = A, target = A + C).</code>
*  <code>Edge A -> A+B+C: contains jump +B (base = A, target = A + B), followed by jump +C (base = A + B, target = A + B + C).</code>

When prefetch lists aren't in use each edge has exactly one jump:

*  <code>Edge A -> A+B: contains jump +B (base = A, target = A + B).</code>
*  <code>Edge A -> A+C: contains jump +C (base = A, target = A + C).</code>
*  <code>Edge A -> A+B+C: contains jump +B+C (base = A, target = A + B + C).</code>

## Compilation Process Overview

The compilation process is a recursive algorithm that walks the table keyed graph described by the segmentation plan and
generates the required table and glyph keyed patches along the way. Due to the differences in how they operate table
keyed and glyph keyed patches are generated by two fairly independent processes.

First a high level description of the algorithm is provided. The process is described in terms of the operations
performed for an arbitrary node in the table keyed graph. The process describes the implementation of
[Compile(...)](https://github.com/w3c/ift-encoder/blob/main/ift/encoder/compiler.cc#L429)


Inputs:

*  Base subset definition: describes the coverage of the font at the current node.
*  Non outline segmentation: a list of segments along which the non outline data of the font is extended.
*  Outline segmentation: a list of segments along which the outline data of the font is extended. This would typically
   be generated prior to compiling by a segmenter.

Process:

1. Generate (if not previously done) the set of glyph keyed patches needed by this node. Each unique variable design
   space covered by nodes will have an associated set of glyph keyed patches. Each unique glyph keyed patch
   set is assigned a unique integer patch set id which is included in the URLs for each associated patch. Glyph keyed
   patch generation is relatively straightforward as the encoder config directly describes the set of patches and what
   glyph ids each one contains. Patch sets are cached using design space as a key. The patches are formed by first
   instancing the original font to the patch set's design space and then collecting the specified per glyph data into
   each patch. Lastly, each patch set is assigned a unique compatibility ID.
   
2. Generate a unique compatibility id for this node. Will be included in all table keyed patches that apply to this node.
   
3. Generate the list of outgoing edges and jumps. Find the set of segments that are not included in the current node and
   generate an edge for each. Additionally generate any multi segment edges required by the encoder config jump ahead
   and use prefetch list settings. Each edge is described by a subset definition that it adds plus one or more jumps as
   described in the concepts section.
   
4. If in mixed mode, then generate the glyph keyed patch map using the list of patches from step 1. The encoder config
   specifies the specific conditions for each mapping entry.
   
5. Using the list of edges from step 3 generate the table keyed patch map with one mapping entry per edge.

6. Construct the font binary for this node. First subset the original font to the base subset definition and then add the
   mapping tables from steps 4 and 5.
   
7. Cache the node font binary using the base subset definition as the key.

8. For each jump in each edge generate the font binary that would result in following that jump by recursively invoking
   this process where the new base subset definition is the union of this base subset definition and the jumps target
   subset definition. Once we have the jumps font binary, diff the new font binary against the previous one to produce
   the table keyed patch for that jump.
   
   Note: each patch is assigned a unique ID and as an optimization we cache the patch binary by patch id to prevent
   recreating the same patch multiple times. ID's are assigned sequentially as new patches are encountered during
   recursion.
    
With this recursive process the complete encoding can then be generated by invoking the algorithm on the initial font
subset definition. From there the complete graph will be traversed and all required patches will be generated.

### Special Handling, Optimizations

There are a number of interesting special casing and optimizations used by the compiler that are not described
in the high level description given above. This section describes those.

#### Retain Glyph IDs

When generating a font in mixed mode the subsetting operations used to produce the table keyed patches must ensure that
glyph ids stay consistent at every stage of extension. This is accomplished by configuring the subsetter with the retain
gids setting. Additionally, at every node the font must have "max glyphs" in the font set large enough to accept
insertion of any of the reachable glyph keyed patches. Currently, this is handled with a hack of always including the
largest glyph id from the original font in every node subset. In the future if the subsetting library is upgraded to
allow forcing a specific number of glyphs, then this hack could be dropped.

#### gvar special casing

When generating a gvar table for use with glyph keyed patches care must be taken to ensure that the shared tuples in the
gvar header match the shared tuples used in the per glyph data in any of the previously created glyph keyed
patches. However, we also want the gvar table to only contain the glyphs from the nodes base subset. If you run a single
subsetting operation which reduces the glyphs and instances the design space the set of shared tuples may change.

To keep the shared tuples correct we subset gvar in two steps (note: this is specific to harfbuzz subsetting):

1. Run instancing only, keeping everything else, this matches the processing done while populating glyph keyed
   patches and will result in the same shared tuples.
    
2. Subset to the initial subset, with no instancing specified. If there is no specified instancing then harfbuzz will not
   modify shared tuples.
   
#### CFF/CFF2 special casing

A similar issue exists with CFF/CFF2, which also contains common outline data that can't be modified by
glyph keyed patches, but must remain consistent with the glyph keyed patches. For CFF/CFF2 we manually construct the
table. It is made by combining all of the non charstrings data from the original font which has only been instanced to
the nodes design space with the charstrings data for any glyphs retained by the initial subset definition.

To accomplish this we manually craft a new charstring table. This works because the IFT spec requires charstrings data
is at the end of the table and doesn't overlap. so we are free to replace the charstrings table with our own.

Following the advice from the IFT spec (https://w3c.github.io/IFT/Overview.html#cff) we desubroutinize CFF and CFF2
tables prior to doing IFT encoding. This ensures that all outline data for a glyph is self contained in that glyph's
charstring.

#### Long Loca

Unlike the other glyph keyed tables loca cannot have it's offset size changed by the application of a glyph keyed patch.
This means that the loca tables offset size must be configured from the start to be large enough to fit the offsets when
the font is fully extended. The encoder checks the maximum size of the loca table and forces long offsets as needed when
generating the table keyed node subsets.

This fix is not needed for the other glyph keyed tables (gvar, CFF, and CFF2) as they support changing offset sizes
during patch application.

#### WOFF2 Roundtrip

IFT encoded initial fonts can use WOFF2 encoding (see: https://w3c.github.io/IFT/Overview.html#ift-and-compression);
however, some special care must be taken. The WOFF2 encoding and decoding process can change some of the data from the
input font. IFT client extension will operate on the decoded version of a font so we must ensure that table keyed
patches are generated relative to this. The encoder will round trip the initial font through woff2
encoding/decoding. Following spec recommendations we disable glyf/loca transformations.

### Integration Tests

Due to the complex nature of the compiler we use [integration
tests](https://github.com/w3c/ift-encoder/blob/main/ift/integration_test.cc) to test encoder functionality. In these
tests an input font is encoded using the encoder library and test specific configurations. Next, it is extended using
the [fontations IFT client](https://github.com/googlefonts/fontations/tree/main/incremental-font-transfer). After
extension various properties of the extension and extended font are tested to ensure correct behaviour:

* cmap, glyph data presence, and equality is used to ensure the encoded font adds the appropriate glyphs after extension.
* Number of round trips and total patch loads are checked to ensure the encodings are working efficiently (number of
  round trips being the most important).
  
These tests are designed to prove that the encoder can produce IFT encodings which can be correctly interpreted and
extended by a client and result in the desired outcomes in terms of extension and network transfer behaviour.
  
For future improvements it would be a good idea to also incorporate rendering and/or shaping tests into the integration
test suite.  A properly encoded IFT font should have the same shaping and rendering behaviour as the original font. It
should be possible to test for this.

## Future Improvements

The compiler implementation is a work in progress and there are lots of opportunities for future improvements. Here's a
non-exhaustive list of some possibilities:

* Add the default feature list to encodings, see: https://w3c.github.io/IFT/Overview.html#feature-tag-list. The default
  feature list should be imported from the spec and applied to generated font subsets. That is all generated subsets
  should implicitly include these features.

* Finish implementing support for all options in the encoder config schema. There's several options which we do not yet
  have support for.

* Improved patch map compilation to reduce encoded size. The format2 patch map has several tools at it's disposal to
  produce compact encodings which we are not yet fully leveraging. In particular for the table keyed patch map we do not
  utilize child entry indices at all to reuse previously encoded code point sets. This would be quite effective at
  reducing encoding sizes when jump ahead > 1 is used as the same code point set will be repeated multiple times.

* Additionally, some smaller size reductions could be realized by smartly picking entry ids that reduce the total number
  of entry deltas needed in the encoding.
  
* Correct ordering of table keyed patch map entries. For selecting invalidating patches the spec expects that table
  keyed entries are ordered by the size of the referenced patch. We do not currently do this in produced
  encodings. Following the spec expectation will result in better client behaviour when selecting between multiple
  invalidating patches.
  
* For patch map entries that represent multiple segments we construct a disjunctive condition (eg. match if A or B is
  needed); however, these entries are guaranteed to always be a super set of the single segment entries so it would be
  better to use conjunctive matching as the multi segment entries should only ever be selected when all included
  segments are needed.

* Profiling and performance optimizations. The current encoder implementation does include some performance optimizations
  (for example caching expensive operations). However, there are likely some remaining performance improvement
  opportunities. Some time should be taken to profile the current execution and identify if there are any easy
  performance wins.
  
* At a higher level, we currently have a two step process for getting an IFT font. 1. Generate segmentation
  plan, 2. Compile the plan into a font. Ultimately, we would like to have a single tool that would automatically
  execute these two steps to produce an IFT font given nothing more than an input font. This would include automatically
  configuring the segmenter so that little to no configuration needs to be provided.
  
