#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B "${BUILD_DIR:-build-dev}" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON -DRUN_GUI_TESTS="${RUN_GUI_TESTS:-OFF}"
cmake --build "${BUILD_DIR:-build-dev}" --parallel "${BUILD_JOBS:-4}"
