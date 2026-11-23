#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
id=io.github.Mschmitt478.ChromiumExplorer
builder=(flatpak-builder)
if ! command -v flatpak-builder >/dev/null; then
    builder=(flatpak run org.flatpak.Builder)
fi
"${builder[@]}" --user --install-deps-from=flathub --force-clean --repo=repo build-flatpak "packaging/$id.yml"
flatpak build-bundle repo "$id.flatpak" "$id"
