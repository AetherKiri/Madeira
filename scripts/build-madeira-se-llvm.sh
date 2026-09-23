#!/bin/bash
# Build the small, code-generation-free LLVM library set used by DXMT's AIR converter.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE="${MADEIRA_SE_LLVM_SOURCE:-$ROOT/toolchains/llvm-project/llvm}"
BUILD="${MADEIRA_SE_LLVM_BUILD:-$ROOT/toolchains/llvm-madeira-se-macos-build}"
PREFIX="${MADEIRA_SE_LLVM_PREFIX:-$ROOT/toolchains/llvm-madeira-se-macos}"
JOBS="${MADEIRA_SE_BUILD_JOBS:-2}"

cmake -S "$SOURCE" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DLLVM_TARGETS_TO_BUILD= \
    -DLLVM_ENABLE_PROJECTS= \
    -DLLVM_ENABLE_ASSERTIONS=OFF \
    -DLLVM_ENABLE_ZSTD=OFF \
    -DLLVM_ENABLE_TERMINFO=OFF \
    -DLLVM_ENABLE_LIBXML2=ON \
    -DLLVM_INCLUDE_TESTS=OFF \
    -DLLVM_INCLUDE_BENCHMARKS=OFF \
    -DLLVM_INCLUDE_EXAMPLES=OFF \
    -DLLVM_BUILD_TOOLS=OFF \
    -DLLVM_BUILD_UTILS=OFF
cmake --build "$BUILD" --target install-llvm-headers install-llvm-libraries \
    --parallel "$JOBS"
echo "Madeira-SE DXMT LLVM libraries ready: $PREFIX"
