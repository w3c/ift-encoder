#!/bin/bash
#
# Checks that all headers referenced from public api headers are also
# in the public set.

public_hdrs=$(bazel query 'labels(hdrs, attr("visibility", ".*//visibility:public.*", kind("cc_.* rule", //...)))' 2>/dev/null | sed -e 's|^//||' -e 's|:|/|')
echo "$public_hdrs" | sort > /tmp/public_hdrs.txt

echo "Public header list:"
cat /tmp/public_hdrs.txt
echo ""

exit_code=0

if grep -q "^ift/common/try\.h$" /tmp/public_hdrs.txt; then
  echo "Error: ift/common/try.h should not be in the public headers list."
  exit_code=1
fi

for hdr in $public_hdrs; do
  if [[ ! -f "$hdr" ]]; then
    continue
  fi

  includes=$(grep -h -E '^[ \t]*#[ \t]*include[ \t]+"[^"]+"' "$hdr" | sed -E 's/.*"([^"]+)".*/\1/')

  for inc in $includes; do
    if [[ -f "$inc" ]]; then
      if ! grep -q "^${inc}$" /tmp/public_hdrs.txt; then
        echo "Error: Public header '$hdr' includes non-public header '$inc'"
        exit_code=1
      fi
    fi
  done
done

rm /tmp/public_hdrs.txt
exit $exit_code
