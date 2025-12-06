#!/bin/bash

FONT_NAME=$(basename $1)
./bazel-bin/util/closure_glyph_keyed_segmenter_util \
    --input_font="$1" \
    --config=util/fallback_analysis/fallback_count_config.txtpb \
    --noinclude_initial_codepoints_in_config \
    --nooutput_segmentation_analysis \
    --nooutput_segmentation_plan \
    --output_fallback_glyph_count 2> /dev/null | grep num_fallback_glyphs | \
      awk "{print \"$FONT_NAME,\", \$2, \$3 }"
