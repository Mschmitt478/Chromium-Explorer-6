#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
exec "${BUILD_DIR:-build-dev}/iexplore" "$@"
