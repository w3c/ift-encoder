#!/bin/bash
# Usage: ./update-goldens.sh <path to hb-subset>
for s in ab abcd Awesome Meows; do
  $1 Roboto-Regular.ttf --text=$s --output-file=Roboto-Regular.$s.ttf --retain-gids
done

