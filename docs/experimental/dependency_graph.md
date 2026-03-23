# Closure Dependency Graph

Author: Garret Rieger  
Date: Mar 23, 2026  

## Introduction

This document provides some information on the [DependencyGraph](../../ift/dep_graph/dependency_graph.h) representation
of glyph closure dependencies in a font.

`DependencyGraph` is a wrapper around the harfbuzz dependency graph API. It unifies glyph to glyph dependency information
with additional dependency information from:
* UVS mappings specified in `cmap 14`.
* Segment and initial font definitions from a segmentation information object.

`DependencyGraph` provides methods for traversing the dependency graph, and optionally supports doing the traversal
implementing glyph closure constraints. This allows the dependency graph traversal to be used to mimic harfbuzz's glyph
closure process.

## Edges

The dependency graph is a directed graph where edges encode dependency information. There are two main types of edges:

* Disjunctive edge from source to destination: implies that if source is present then destination is needed as well.

* Conjunctive edge from source to destination: each conjunctive edge has additional context information attached to it.
  context can be a set of features, code points, and/or glyphs. It implies that if source and all of the context items
  are present then destination is needed as well. As an example for the ligature `f + i -> fi` there would be an edge
  from `f -> fi` with context `{i}` and an edge from `i -> fi` with context `{f}`. For ligatures, features, and UVS
  dependencies the graph will always contain edges for each member of the conjunction (see the ligature example). For
  contextual GSUB substitutions this is not the case.
  
## Init Font

The dependency graph is configured with an initial font definition. For the graph traversal all glyphs, code points and
features in the initial font definition are considered to be already reached.  For example if a font has an `fi`
ligature substitution and `i` is in the initial font then the normally conjunctive edge from `f -> fi` will become
disjunctive due to the `i` context requirement already being satisfied.

## Nodes

* `Segment`: segment nodes are the entry point into the graph. There is one segment node per segment in the segmentation
  information object. Each segment has an out going edge for each code point and layout feature in the segment's
  definition.
  
* `Unicode code point`: each code point node can have two types of out going edges derived from the font's `cmap`
   table. The first type is for regular `cmap` mappings of code point to glyph. The second type is a conjunctive edge
   derived from the `cmap 14` sub table, which maps from code point pairs to a substitution glyph.  Beyond `cmap` a code
   point may also have outgoing disjunctive edges to other code points which model bidi substitutions made in harfbuzz's
   glyph closure.
   
* `Glyph`: each glyph node can have out going edges to additional glyphs that are derived from various tables in the
  font (eg. COLR, GSUB, MATH, glyf). These can be disjunctive or conjunctive. Conjunctive edges in GSUB may have a layout
  feature as part of the edge's context if that layout feature is not part of the initial font. This layout feature is
  the feature which activates the lookup associated with the edge.
  
* `Feature`: each feature node can have outgoing edges to any glyphs where the feature is part of the conjunctive
  condition as discussed above.
   
## Traversal

Graph traversal takes a set of starting nodes and traverses the graph to discover which nodes can be reached. There are
two main traversal modes:

* Enforced Context: during traversal before a conjunctive edge can be traversed it's context requirements must have been
  reached. This mimics harfbuzz's glyph closure, but may still overestimate it in some cases.
  

* Non-enforced Context: conjunctive edges are always traversed even if their requirements are not met. This mode is
  useful for modeling all glyphs that could potentially be reached from a starting point.

All graph traversals phase the traversals in the same way that harfbuzz glyph closure does, for example glyf composite
edges will only be traversed after all GSUB edges have been resolved. This is required for the traversal to accurately
mimic harfbuzz closure.
