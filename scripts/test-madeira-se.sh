#!/bin/sh
# Build and run the standalone Madeira-SE core tests.
#
# Copyright (C) 2026 The Madeira contributors
# SPDX-License-Identifier: GPL-3.0-or-later

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${MADEIRA_SE_BUILD_DIR:-$ROOT/build/madeira-se-core}

cmake -S "$ROOT/madeira-se" -B "$BUILD_DIR" \
  -DMADEIRA_SE_BUILD_TESTS=ON \
  -DMADEIRA_SE_BUILD_TOOLS=ON
cmake --build "$BUILD_DIR" --parallel "${MADEIRA_SE_BUILD_JOBS:-4}"
ctest --test-dir "$BUILD_DIR" --output-on-failure

if [ "$#" -gt 0 ]; then
  exec "$BUILD_DIR/madeira-se-probe" "$1"
fi
