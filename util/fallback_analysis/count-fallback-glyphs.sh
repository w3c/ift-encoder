#!/bin/bash
#
# Usage: ./util/fallback_analysis/count-fallback-glyphs.sh <directory> <concurrency>
#
# <directory> will be scanned recursively to locate all font files of the form *.ttf or *.otf.
# For each found font a closure analysis will be performed and the number of fallback glyphs
# and the compressed size in bytes of the fallback glyph patch will be output.
#
# <concurrency> sets the number of parallel executions of the closure analysis.
#
# Fallback glyphs are those that have conditions which could not be detected with the current
# closure analysis and as a result will always be loaded.
TARGET_DIR=$1
CONCURRENCY=$2

bazel build -c opt util:closure_glyph_keyed_segmenter_util
echo "file; num_fallback_glyphs; total_glyphs; fallback_glyphs_compressed_bytes; all_glyphs_compressed_bytes"
find $TARGET_DIR \( -iname "*.ttf" -or -iname "*.otf" \) -print0 | xargs -0 -n 1 -P $CONCURRENCY \
  ./util/fallback_analysis/count-fallback-glyphs-for-font.sh
