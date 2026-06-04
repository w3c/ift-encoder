# Dependency Graph Glyph Condition Extraction

Author: Garret Rieger  
Date: June 3rd, 2026  
Updated: Aug 5th, 2026

## Introduction

This document describes how the [dependency graph](dependency_graph.md) is used to find activation conditions for glyphs
in a font. For more details see [Segmenter concepts](segmenter.md#concepts) which are used in this document, so it's recommended to review that before reading this.

## Representing Activation Conditions

An activation condition is a boolean expression in terms of segment presence. To keep the conditions normalized all
boolean conditions are stored in monotonic [conjunctive normal form](https://en.wikipedia.org/wiki/Conjunctive_normal_form)

The [activation condition](../ift/encoder/activation_condition.h) class implements a couple of operations to help in this analysis:

* `ActivationCondition::Or(A, B)`: joins two activation conditions in a new condition `(A) OR (B)`. The new condition is
  automatically normalized and simplified to give a canonical representation.

* `ActivationCondition::And(A, B)`: joins two activation conditions in a new condition `(A) AND (B)`. The new condition is
  automatically normalized and simplified to give a canonical representation.

## The Algorithm

1. Initialize a map from graph node to activation condition: all nodes which are part of the initial font definition are
   marked as always true. One node is added per segment which an activation condition of itself. All other nodes are left
   unmapped.

2. Precompute the set of incoming edges for each node of the dependency graph.

3. Find the [strongly connected components](https://en.wikipedia.org/wiki/Strongly_connected_component) of the dependency graph.
   The implementation uses [Tarjan's algorithm](https://en.wikipedia.org/wiki/Tarjan%27s_strongly_connected_components_algorithm)

4. Process the components in topological order (which is determined as a by product of Tarjan's algorithm).

   *  If a component has only one node: construct the condition for the node by joining the activation condition for
      each parent node (via incoming edges).  Parent node conditions are joined with disjunction. Conjunctive edges have
      multiple parent nodes, these conditions are joined first using conjunction. Simplify and normalize the combined
      condition into conjunctive normal form.

   *  Otherwise if a component has more than one node: Iterate through the nodes of the component. Propagate conditions
      as described in the one node case. Repeat this iteration until no node conditions change.

## Closure Phasing

The intention of this condition extraction is to mirror font subsetting closure, which happens in phases that each
process a specific dependency type. As a result the extraction algorithm must also be phased. The above algorithm is run
once per subsetting closure phase and only works with graph edges that are inscope for that phase.

## Caveats

Since this approach relies on the dependency graph, which in some cases over approximates glyph conditions, the extracted
conditions may also over approximate the true conditions. This primarily affects glyphs reachable via contextual GSUB substitutions.

## Implementation

The implementation of dependency graph condition extraction can be found in [dependency_closure.cc](../ift/encoder/dependency_closure.cc).
