#!/bin/bash
# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
# test.sh — build the unit-test runner, then run it; prints when it was built,
# so a green claim can be checked (ctest runs, it does not build).
#   ./test.sh [gtest args]
# JUCE is found via CMAKE_PREFIX_PATH (default ~/local/juce); JOBS sets build parallelism.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="${STEMDECK_BUILD:-$HERE/build}"
RUNNER="$BUILD/tests/stemdeck-tests"

cmake -S "$HERE" -B "$BUILD" -DSTEMDECK_TESTS=ON \
    -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH:-$HOME/local/juce}"
cmake --build "$BUILD" --target stemdeck-tests -j"${JOBS:-4}"
echo "=== stemdeck-tests built $(date -r "$RUNNER" '+%F %T') ==="
"$RUNNER" "$@"
