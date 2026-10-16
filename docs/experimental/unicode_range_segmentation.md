# `@font-face unicode-range` Segmentation Mode

Author: Garret Rieger
Date: Oct 7th, 2026

## Introduction

Note: this is a highly experimental exploration.

While the IFT encoder's segmentation plan generator ([segmenter.md](segmenter.md)) was designed to partition a font into glyph-keyed and table-keyed patches for Incremental Font Transfer (IFT), the same dependency analysis and frequency-guided merging framework can also be used to produce font subset segmentations for serving fonts via CSS `@font-face` `unicode-range`.

With `@font-face` `unicode-range`, a font is split into a collection of standalone WOFF2 font subsets that browsers load on demand based on codepoint presence on the page. Supporting `unicode-range` as an optional target mode introduces several key differences compared to standard IFT segmentation:

| Aspect | `TargetMode::IFT` (Default) | `TargetMode::UNICODE_RANGE` |
| :--- | :--- | :--- |
| **Client Loading Model** | Single font instance incrementally extended by patches | Independent `@font-face` WOFF2 font files matched per character by `unicode-range` |
| **Initial Segment (`config.initial_segment`)** | Loaded once as the base font; initial font merging moves high-probability patches into it | Included in **every** subset (union of `initial_segment` codepoints/glyphs/features + default features and the patch's glyphs; useful for UVS codepoints, etc.). Initial font merging is disabled |
| **Layout Features** | Optional feature patches + table-keyed patches | Codepoints only; each subset statically retains the default layout features plus any explicitly requested in `config.initial_segment.features`. Optional feature segments and table-keyed segments are disallowed |
| **Unmapped Glyphs** | Configurable (defaults to `MOVE_TO_INIT_FONT`) | Must be `FIND_CONDITIONS` or `PATCH` (`AutoSegmenterConfig` sets `FIND_CONDITIONS`; `MOVE_TO_INIT_FONT` is rejected) |
| **Patch Merging** | Optional (`experimental_use_patch_merges`) | Disabled; only segment merges are performed |
| **Interacting Glyphs & Disjoint Conditions** | Can be placed in separate patches (e.g. `fi` in patch `f AND i`, shared component `A` in `A OR À`) | Determined via dependency graph / closure analysis; `MakeActivationConditionsDisjoint` merges triggering segments of non-exclusive conditions whose probability $\ge$ `disjoint_conditions_probability_threshold`. Conditions below the threshold are skipped |
| **Final Subsets & Activation Conditions** | CNF expressions (`(s1 OR s2) AND s3`) | Only **exclusive** patches (`(s_i)`) are emitted in the final plan (any remaining non-exclusive patches are ignored), yielding strictly disjunctive and mutually disjoint `unicode-range` sets |
| **Patch Size / Cost Model** | Brotli-compressed glyph-keyed patch | Standalone WOFF2 font subset for `initial_segment` $\cup$ patch glyphs (`hb_subset` + WOFF2 encoding with `glyf` transform) |

---

## Configuration & Validation

A `TargetMode` enum (`IFT`, `UNICODE_RANGE`) in [segmenter_config.proto](../ift/config/segmenter_config.proto) selects the segmentation target mode (`target_mode` on `SegmenterConfig`, defaulting to `IFT`), alongside a `disjoint_conditions_probability_threshold` setting. The auto-configuration generator (`AutoSegmenterConfig::GenerateConfig`) also accepts `TargetMode` and automatically configures appropriate defaults when `UNICODE_RANGE` is selected:

* Disables optional feature segment generation (`generate_feature_segments = false`) and table-keyed segment generation (`generate_table_keyed_segments = false`).
* Sets `unmapped_glyph_handling = FIND_CONDITIONS` so all glyphs in the full closure receive activation conditions.
* Leaves initial font merging thresholds unset and skips IFT-specific base segmentation plan settings (`jump_ahead`, `use_prefetch_lists`).

To ensure invalid configurations are caught regardless of whether a caller goes through `SegmenterConfigUtil` or invokes `ClosureGlyphSegmenter` directly, `ClosureGlyphSegmenter` validates that when `target_mode == UNICODE_RANGE`:

1. `unmapped_glyph_handling` is set to either `FIND_CONDITIONS` or `PATCH` (`MOVE_TO_INIT_FONT` is rejected).
2. No input segment definitions contain layout features (optional feature loading is unsupported).
3. Initial font merging is not enabled on any merge group.
4. Patch merging is disabled on all merge groups.
5. `generate_table_keyed_segments` is not enabled.

### Initial Segment Handling

In `UNICODE_RANGE` mode, there is no separate initial font loaded prior to patches. Instead, `config.initial_segment` defines a common base subset (codepoints, glyphs, and layout features, combined with the default layout feature list) that is included in **every** generated font subset.

This is particularly useful for codepoints such as Unicode Variation Selectors (UVS) that may combine with many otherwise unrelated base characters across the font: placing them in `initial_segment` makes them available in every subset and prevents them from creating cross-subset dependencies during condition analysis.

---

## Disjoint Activation Conditions & Interacting Glyphs

In CSS `@font-face`, each subset is treated as a separate font face during text shaping, and `unicode-range` matching activates subsets purely on per-character codepoint presence. This imposes two requirements on the segmentation:

1. **Interacting glyphs must reside in the same subset**: If a substitution (such as an `fi` ligature with conjunctive condition `f AND i`) or a shared glyph component (such as base glyph `A` with disjunctive condition `A OR À`) spans multiple segments, splitting those segments into separate `@font-face` subsets would either prevent the substitution from occurring across subset boundaries or require overlapping `unicode-range` definitions.
2. **Activation conditions must be mutually disjoint and exclusive**: Each final font subset must correspond to a single segment (a disjoint set of codepoints forming its `unicode-range`).

### `MakeActivationConditionsDisjoint`

After initial condition analysis (and unmapped glyph condition finding when `FIND_CONDITIONS` is used), the segmenter runs a `MakeActivationConditionsDisjoint` pass before starting general cost-based or heuristic merging:

1. Inspect all non-exclusive patch conditions (conditions involving more than one triggering segment, whether conjunctive or disjunctive).
2. Group segments into connected components by unioning the triggering segments of each non-exclusive condition.
3. Merge the segments within each connected component into a single representative segment and update the segmentation context.

When all non-exclusive conditions participate in this pass, every multi-segment interaction collapses into a single exclusive segment condition, leaving all surviving segments inert.

### Breaking Low-Probability Interactions

In some fonts, rare ligatures or complex contextual rules can chain together large numbers of segments into a single connected component, limiting how finely the font can be split. To control this trade-off, `MakeActivationConditionsDisjoint` supports a configurable `disjoint_conditions_probability_threshold`:

* For each non-exclusive patch condition, its activation probability is evaluated against the frequency data of the covering cost-based merge group(s).
* If the condition's probability is below `disjoint_conditions_probability_threshold`, that condition is skipped during `MakeActivationConditionsDisjoint` and does not force its triggering segments to be merged.
* During the subsequent merging phase, only segment merges are performed (patch merges are disabled). If the triggering segments of a skipped condition are not later merged by cost-based or heuristic segment merging, the remaining non-exclusive patch is ignored when constructing the final segmentation plan: **only exclusive patches are emitted into the final `unicode-range` segmentation plan**.

---

## WOFF2 Subset Cost Function

In standard IFT mode, the cost function evaluates candidate merges using the Brotli-compressed size of glyph-keyed patches (which only contain outline tables such as `glyf`/`gvar`/`CFF`/`CFF2` plus a small patch header).

In `UNICODE_RANGE` mode, each patch corresponds to a complete, standalone WOFF2 font subset. The subset for a patch is defined as the union of `initial_segment` (including default layout features) and the patch's glyphs (along with their associated codepoints). Because every WOFF2 subset must include shared font tables (`cmap`, `head`, `hhea`, `maxp`, `OS/2`, subsetted `GSUB`/`GPOS`, `fvar`, etc.), per-subset table overhead is substantially higher than in glyph-keyed patches.

To reflect this in the merger's cost delta calculations (`cost(S) = sum P(c_i) * (size(p_i) + k)`):

* **Exact WOFF2 Sizing (`brotli_quality > 0`)**: A WOFF2-backed `PatchSizeCache` subsets the font to `initial_segment` $\cup$ the candidate patch's glyphs and encodes the resulting font with WOFF2 (including the WOFF2 `glyf` transform) at the configured Brotli quality. When evaluating a merge of two segments $A$ and $B$, the cost delta naturally captures the byte savings of deduplicating the shared non-outline font tables across the two subsets.
* **Estimated WOFF2 Sizing (`brotli_quality == 0`)**: For fast low-quality levels where per-candidate Brotli compression is disabled, subset sizes are estimated using the WOFF2 size of the base `initial_segment` subset (capturing fixed per-subset table overhead) plus the raw outline size of the patch's glyphs scaled by the font's overall WOFF2 outline compression ratio.

---

## Output `SegmentationPlan` Representation

The resulting `SegmentationPlan` protobuf directly encodes the `@font-face` `unicode-range` subsets for consumption by a downstream subset compiler:

* `initial_codepoints`, `initial_glyphs`, and `initial_features` specify the common base definition (from `config.initial_segment`) to be unioned into every generated subset alongside default layout features.
* Each entry in `glyph_patch_conditions` is an exclusive condition referencing a single segment `s_i` (`segments[s_i].codepoints` defines the `unicode-range` for that `@font-face` rule) and activating patch `p_i`.
* `glyph_patches[p_i]` lists the disjoint set of glyphs belonging to subset `p_i` (to be unioned with `initial_segment` when producing the subset font).
* `non_glyph_segments` and table-keyed patch fields remain empty.
