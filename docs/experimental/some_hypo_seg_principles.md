# Some Hypothetical Segmentation Principles

Author: Skef Iterum
Date: April 4, 2025
Last updated: April 30, 2025

# Introduction

These are some hypothetical principles for segmentation and the reasoning
behind them. They are hypothetical in that at the time of writing they
have not been validated empirically.

One reason to put them on the table early is so that future performance tests
can be constructed with these ideas in mind, attempting to validate or
invalidate them rather than not taking them into account.

# Patch Principles

## Background Principles

1. The most common kind of document impression (a "viewing" of a document) is
   written in a single language, and when there are language variants that
   affect writing (such as Hong Kong Chinese vs Mainland Chinese), in a single
   language variant. Some of those documents will contain brief sections of
   different languages (phrases written in another language and then
   translated, names of people, places, or things).

   The second most common kind of document impression is written in two
   languages. And so forth.

2. In practice, codepoint frequency is font-relative.

   Obviously when a font does not map a given codepoint, that font is unlikely
   to be used to render that codepoint. Most clients will support some kind of
   codepoint rendering fallback but fallbacks are poor aesthetically and will
   typically be avoided by someone choosing a specific font.

   Perhaps less obviously, a font is less likely to render some codepoint that
   is high-frequency if it lacks support for certain other codepoints that are
   high-frequency. For example, suppose a font supports some codepoints that
   are high frequency in general because they are high frequency in Japanese,
   but lacks support for other high frequency Japanese codepoints. Following BP
   1 that font is unlikely to be used for a document written in Japanese.
   Therefore, one would not normally expect the Japanese codepoints the font
   does support to be used with high frequency.

3. Spatial locality for high (to medium?) frequency codepoints is
   script/langauge-specific.

   This follows from BP 1. Loading codepoint that is high-frequency for langauge
   *X* is highly predictive of needing other codepoints that are high-frequency
   for *X*, but far less predictive of needing codepoints that are high-frequency
   for other languages.

4. Spatial locality for (language-relative) low (to medium?) frequency
   codepoints is weak.

   A codepoint can of course be *in general* low-frequency while still being
   language-relative high frequency. The language itself may just be used less
   often. In such cases loading one language-relative high frequency codepoint
   is still predictive of loading another such codepoint. This isn't important
   to account for in the grand scheme of things, given the general low frequency,
   but it still helps in the cases where those codepoints are used.

   What doesn't help much are further attempts to exploit the locality of
   low-frequency codepoints. Loading one isn't very predictive of loading
   another.  There can be exceptions, such as the box-building codepoints, or
   the circled numbers, but a) these are arguably quasi-scripts and b) such
   glyphs may be more commonly used as quasi-emojis than for their intended
   purposes.

## Segmentation Principles

1. Glyphs should be segmented by script.

   This is the most basic principle that follows from BP 1.

2. Glyphs should be sub-segmented by language-specific high frequency, relative to
   "supported" languages.

   This follows from BP 1-4. Loading a glyph that is high-frequency in a given
   language is highly predictive of loading another glyph that is high frequency
   in that language, *if the font has general support for that language*. This
   suggests a procedure like: Identify the languages "supported" by the font,
   segment the high-frequency glyphs for those languages together.

3. Glyphs that are high-frequency in one supported language but medium frequency
   in another supported language should be sub-segmented separately.

   Even if Japanese is used more frequently than Chinese, a font that supports
   both may be used for either, so it's better not to bias Japanese over Chinese
   when that is avoidable. Therefore, you shouldn't mix a codepoint that is
   high frequency in both Chinese and Japanese in with a codepoint that is high
   frequency in Japanese but not in Chinese. And further with Mainland Chinese
   versus Hong Kong Chinese and with Korean. Instead, permute the languages
   and make separate segments of shared high-frequency codepoints.

   The same can also be true of alphabetic languages, perhaps making segments for
   the "base" Latin glyphs and then accounting for pre-made accented codepoints
   for German, Vietnamese, French, and so forth separately.

4. High-frequency segments will be "lumpier".

   This follows from SP 2-3.

   When the collection of glyphs that make sense to segment together by high frequency
   codepoint is large enough, one can of course sub-divide them to meet total size or
   glyph number targets. However, when such groupings are not large enough it is
   better to leave the segments as they are than to merge them with other segments
   just to match such targets. The handling of high-frequency codepoints is where
   most of the value for IFT comes from, some lumpiness is fine.

5. Segments containing glyphs that are not high-frequency relative to any language
   should be smaller.

   This follows from BP 4. High-frequency codepoints are loaded with a general
   expectation that you'll need a bunch of them (given BP 1 and the assumption
   assumption that most documents aren't tiny). Therefore the size of such segments
   should generally lean larger to reduce overhead. Loading codepoints that aren't
   high-frequency relative to any language isn't very predictive, however, so the
   chance of needing other codepoints in the segment is low.

   The optimal size should be determined by other factors: One glyph per
   codepoint is bad from a privacy standpoint and will greatly increase the
   overhead of a "load the whole font" operation. So there will likely be some
   happy-medium target that is lower than that for high-frequency codepoints.

# Feature Patch Principles

## Background Principles

1. As noted in the spec, layout features divide into two categories: those that
   may be used implicitly during shaping and those that would only be used if
   specified somehow in a document. The spec encourages an encoder to include
   layout features of the former type in the base font.

2. Explicit use of a layout feature in a document is still the rare exception.

## Table-keyed feature patch principles

1. Following background principle 2, the primary reason for a table-keyed feature
   patch should be to reduce the size of the base font (and table-keyed patches
   to the base font). So a good initial heuristic is: Add up the size of shaping
   data for all non-default layout features in the font. If that does not exceed
   a threshold, don't make any table-keyed feature patches. If it does, start with
   a plan to make a table-keyed "segment" for all of the non-default shaping data.
   Treat this analogously to a script/locale table-keyed segment.

2. When making at least one table-keyed "segment", examine the size of each layout
   feature taken individually to see if any exceeds a threshold. That threshold
   should be substantial, perhaps larger than the threshold for making a feature
   segment at all. (This is because of the combinatorial aspect of table-keyed
   patches.) If it does exceed the threshold, start by putting it into its
   own segment. Then examine the other non-default layout features to see if any
   has substantial overlap with that one. If so, move those to its segment as well.

## Glyph-keyed feature patch principles

1. Duplicating a glyph already in a non-feature segment into a "feature
   segment" is of dubious value. Better to separate out glyphs only loaded when
   using a non-default layout tag from those sometimes loaded otherwise. Handle
   the latter with appropriate patch table entries to load them in the segments
   they were already added to. If the glyph is duplicated in more than one
   segment, use reasonable principles (Which segment is smaller? Which segment
   has more than one such glyph?) to pick one.

2. Special-case the aalt-style features. (These are characterized by support
   either or both of single- and alt-substitution. Most have "alt" in their
   names but not all names with "alt" follow this pattern.) Minimize the
   impact of and optimization for aalt-style features as much as possible.
   One way to do this is to set them aside until the end of the analysis
   and then build additional segments (if necessary) and add patch table
   entries to pull the glyphs from whatever segments they wound up in.

   Consider lowering the memory cost for aalt-style patch table entries by
   putting those entries into the table-keyed layout feature segment.
   This will mean an extra round-trip to load the glyphs for an aalt-style
   layout feature, but such cases will be so uncommon that this is fine.

3. That leaves glyphs only loaded using a non-default, non-aalt layout feature.
   First consider the simplified case of glyphs only loaded by a single layout
   feature tag: These should be grouped together into segments according to the
   rules of the glyphs they substitute for, but at coarser grain when this
   makes sense. So, don't tend to group glyphs of different "source
   frequencies" or "source scripts" into the same patches, but if there are
   multiple segments of the same frequency and script(s), group them into
   segments of the target size for that frequency (e.g. larger for
   high-frequency source glyphs).

4. This leaves the problem of glyphs loaded by more than one layout feature.
   Consider those loaded by a given pair of features: If there are only a few
   such glyphs, consider duplication. If there are a sufficient number, pull
   them out into their own segment(s) loaded by either (or both) features.
   For such segments it is probably OK to override source-script and
   source-frequency grouping for the sake of multi-layout-tag grouping.

5. This leaves the problem of glyphs loaded by more than 2 layout features.
   Considering that the aalt-style features were already special-cased, this
   shouldn't be common. It may be best to look at the reasons for this
   kind of sharing across existing fonts and develop heuristics for addressing
   those cases. Then provide a backup heuristic, which could be as simple as
   "just duplicate".
