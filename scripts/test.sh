#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
scripts/build.sh
ctest --test-dir "${BUILD_DIR:-build-dev}" --output-on-failure
