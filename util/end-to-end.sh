#!/bin/bash
# This script does an end to end test on an IFT font encoding.
#
# Given a font, an encoder config, and a text sample it produces the IFT encoded font.
# Extends it with an IFT client for the text sample. Then finally compares the rendering
# of that text sample by the extended font against the original input font.
#
# Comparison is made using a locally compiled version of hb-shape. Additional, if there 
# is an installed copy of hb-view that's also used to compare actual rendering (ie. `which hb-view` returns something).
#
# Usage:
# end-to-end.sh <path to font> <path to config> <text string>

FONT=$(readlink -f $1)
CONFIG=$(readlink -f $2)
TEXT_SAMPLE=$3
WORKING_DIR=$(mktemp -d)

# TODO(garretrieger): take an optional design space position
# TODO(garretrieger): take an optional list of feature tags

# Locate hb-view if available
HB_VIEW=$(which hb-view)

# Create the IFT encoding
bazel run //util:font2ift -- \
  --input_font="$FONT" --config="$CONFIG" \
  --output_path="$WORKING_DIR" --output_font="ift_font.ttf" 2> /dev/null 1> /dev/null

# Run an extesion on it using the provided text
bazel run @fontations//:ift_extend -- \
  --font=$WORKING_DIR/ift_font.ttf --text="$TEXT_SAMPLE" -o $WORKING_DIR/extended.ttf 2> /dev/null 1> /dev/null

# Compare shaping
bazel run @harfbuzz//:hb-shape -- \
  $FONT --text="$TEXT_SAMPLE" > $WORKING_DIR/original_shaping.txt 2> /dev/null
bazel run @harfbuzz//:hb-shape -- \
  $WORKING_DIR/extended.ttf --text="$TEXT_SAMPLE" > $WORKING_DIR/extended_shaping.txt 2> /dev/null

diff -u $WORKING_DIR/original_shaping.txt $WORKING_DIR/extended_shaping.txt > $WORKING_DIR/diff.txt

SHAPE_RETCODE=$?
if [ $SHAPE_RETCODE -ne 0 ]; then
  echo "Shaping Comparison: FAILED"
  cat $WORKING_DIR/diff.txt
  echo ""
else
  echo "Shaping Comparison: SUCCESS"
fi

VIEW_RETCODE=0
if [[ -n "$HB_VIEW" ]]; then
  $HB_VIEW $FONT --text="$TEXT_SAMPLE" -o $WORKING_DIR/original.svg -O svg
  $HB_VIEW $WORKING_DIR/extended.ttf --text="$TEXT_SAMPLE" -o $WORKING_DIR/extended.svg -O svg
  diff -u $WORKING_DIR/original.svg $WORKING_DIR/extended.svg > $WORKING_DIR/diff.txt
  VIEW_RETCODE=$?
  if [ $VIEW_RETCODE -ne 0 ]; then
    echo "Rendering Comparison: FAILED"
    cat $WORKING_DIR/diff.txt
  else
    echo "Rendering Comparison: SUCCESS"
  fi
  rm $WORKING_DIR/*.svg
else
  echo "Rendering Comparison: SKIPPED (hb-view not found)"
fi

rm $WORKING_DIR/*.ttf
rm $WORKING_DIR/*.txt
rm $WORKING_DIR/*.ift_tk
rm $WORKING_DIR/*.ift_gk
rmdir $WORKING_DIR

if [ $SHAPE_RETCODE -ne 0 ] || [ $VIEW_RETCODE -ne 0 ]; then
  exit -1
fi
exit 0