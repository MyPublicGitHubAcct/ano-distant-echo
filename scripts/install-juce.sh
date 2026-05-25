#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"

cd "$REPO_ROOT"

cmake -B "$BUILD_DIR" -G Ninja -DBUILD_JUCE_PLUGIN=ON
cmake --build "$BUILD_DIR" --target install-juce
