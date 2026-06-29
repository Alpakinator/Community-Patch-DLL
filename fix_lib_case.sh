#!/bin/bash
# fix_lib_case.sh — Create case-insensitive symlinks for all .lib files
# so both uppercase and lowercase references resolve on Linux.
set -eu

LIBDIR="${1:-/opt/win-sdk/Lib}"

if [ ! -d "$LIBDIR" ]; then
    echo "ERROR: $LIBDIR not found"
    exit 1
fi

count=0
while IFS= read -r -d '' f; do
    d="$(dirname "$f")"
    b="$(basename "$f")"
    u="$(echo "$b" | tr '[:lower:]' '[:upper:]')"
    l="$(echo "$b" | tr '[:upper:]' '[:lower:]')"

    if [ "$b" != "$u" ] && [ ! -e "$d/$u" ]; then
        ln -sf "$b" "$d/$u"
        count=$((count + 1))
    fi
    if [ "$b" != "$l" ] && [ ! -e "$d/$l" ]; then
        ln -sf "$b" "$d/$l"
        count=$((count + 1))
    fi
done < <(find "$LIBDIR" -type f -iname '*.lib' -print0)

echo "Created $count case-insensitive symlinks in $LIBDIR"
echo "Total .lib files: $(find "$LIBDIR" -name '*.lib' | wc -l)"
