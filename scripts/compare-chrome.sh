#!/usr/bin/env bash
set -euo pipefail
if [[ $# != 1 ]]; then
    echo "Usage: scripts/compare-chrome.sh /path/to/captured-window.png" >&2
    exit 2
fi
reference=docs/screenshots/wayland.png
candidate=$(realpath "$1")
cd "$(dirname "$0")/.."
geometry=$(magick identify -format '%wx%h' "$reference")
if [[ $(magick identify -format '%wx%h' "$candidate") != "$geometry" ]]; then
    echo "Capture the window at the reference size ($geometry)." >&2
    exit 2
fi
temp=$(mktemp -d)
trap 'find "$temp" -maxdepth 1 -type f -delete; rmdir "$temp"' EXIT
width=${geometry%x*}
for bar in reference candidate; do
    magick "${!bar}" -crop "$((width - 8))x100+4+29" +repage "$temp/$bar.png"
done
magick compare -fuzz 5% -metric AE "$temp/reference.png" "$temp/candidate.png" null:
