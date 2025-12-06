#!/bin/bash
TARGET_DIR=$1

bazel build -c opt util:closure_glyph_keyed_segmenter_util
echo file, num_fallback_glyphs, total_glyphs
find $TARGET_DIR \( -iname "*.ttf" -or -iname "*.otf" \) -print0 | xargs -0 -n 1 -P 110 \
  ./util/fallback_analysis/count-fallback-glyphs-for-font.sh
