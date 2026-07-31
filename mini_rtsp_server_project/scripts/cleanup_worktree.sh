#!/bin/sh
# Preview by default. Use --apply only after reviewing the list.

set -eu

mode="dry-run"
if [ "${1:-}" = "--apply" ]; then
    mode="apply"
elif [ "$#" -gt 0 ]; then
    echo "Usage: $0 [--apply]" >&2
    exit 2
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

list_candidates() {
    find . -type f \( \
        -name '*.bak' -o \
        -name '*.backup' -o \
        -name '*.old' -o \
        -name '*.orig' -o \
        -name '*.rej' -o \
        -name '*_before_*.c' -o \
        -name '*_before_*.h' -o \
        -name '*.before_*' -o \
        -name '*.failed-*' -o \
        -name '*.failed_*' -o \
        -name '*.patch' \
    \) -print

    for file in \
        ./README.txt \
        ./COMPLETE_OVERLAY_README.txt \
        ./INTEGRATION_NOTES.md \
        ./INTEGRATION_NOTES_V2.md
    do
        [ -e "$file" ] && printf '%s\n' "$file"
    done
}

candidates=$(list_candidates | sort -u)

if [ -z "$candidates" ]; then
    echo "No historical/debug files found."
    exit 0
fi

echo "Historical/debug files:"
printf '%s\n' "$candidates"

if [ "$mode" = "dry-run" ]; then
    echo
    echo "Dry run only. Re-run with --apply to delete these local files."
    exit 0
fi

printf '%s\n' "$candidates" | while IFS= read -r path
do
    [ -n "$path" ] && rm -f -- "$path"
done

echo "Cleanup completed."
