#!/bin/bash
# Compile and verify both embeddable x86 targets against AArch64 TCTI.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
QEMU_SOURCE="${MADEIRA_SE_QEMU_SOURCE:-$REPO_ROOT/.deps/qemu-utm}"
BUILD_DIR="${MADEIRA_SE_QEMU_BUILD_DIR:-$REPO_ROOT/build/madeira-se-qemu}"
BUILD_JOBS="${MADEIRA_SE_BUILD_JOBS:-2}"
BUILD_LOG="${MADEIRA_SE_QEMU_BUILD_LOG:-$REPO_ROOT/build/madeira-se-qemu-build.log}"

if [[ $# -gt 1 ]]; then
    echo "usage: $0 [build-directory]" >&2
    exit 2
fi
if [[ $# -eq 1 ]]; then
    BUILD_DIR="$1"
fi
if [[ ! "$BUILD_JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: MADEIRA_SE_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi
if [[ ! -f "$BUILD_DIR/build.ninja" || ! -f "$BUILD_DIR/config-host.h" ]]; then
    echo "error: QEMU TCTI build is not configured: $BUILD_DIR" >&2
    echo "run scripts/configure-qemu-madeira-se.sh first" >&2
    exit 1
fi
if ! grep -q '^#define CONFIG_TCG_THREADED_INTERPRETER' \
    "$BUILD_DIR/config-host.h"; then
    echo "error: QEMU build directory is not configured for TCTI" >&2
    exit 1
fi
if ! grep -q '^#define CONFIG_SHARED_LIBRARY_BUILD' \
    "$BUILD_DIR/config-host.h"; then
    echo "error: QEMU build directory is not configured for shared libraries" >&2
    exit 1
fi
if [[ ! -x "$BUILD_DIR/pyvenv/bin/python3" ]]; then
    echo "error: QEMU build Python environment is missing" >&2
    exit 1
fi

# tcti-gadget-gen.py uses /usr/bin/env python3 and Python 3.10 match syntax.
# Keep QEMU's configured Python first so Ninja cannot fall back to macOS 3.9.
export PATH="$BUILD_DIR/pyvenv/bin:/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"

echo "Building QEMU TCTI x86 shared libraries with -j$BUILD_JOBS"
if ! ninja -C "$BUILD_DIR" -j"$BUILD_JOBS" \
    libqemu-x86_64-softmmu.dylib \
    libqemu-i386-softmmu.dylib >"$BUILD_LOG" 2>&1; then
    echo "error: QEMU TCTI build failed; tail of $BUILD_LOG:" >&2
    tail -n 240 "$BUILD_LOG" >&2
    exit 1
fi

for target in libqemu-x86_64-softmmu.dylib libqemu-i386-softmmu.dylib; do
    artifact="$BUILD_DIR/$target"
    if [[ ! -f "$artifact" ]]; then
        echo "error: missing QEMU smoke artifact: $artifact" >&2
        exit 1
    fi
    file_description="$(file "$artifact")"
    if [[ "$file_description" != *"Mach-O 64-bit dynamically linked shared library arm64"* ]]; then
        echo "error: $target is not an arm64 Mach-O shared library" >&2
        exit 1
    fi
    exported_symbols="$(nm -gU "$artifact")"
    for symbol in \
        _qemu_init \
        _qemu_cleanup \
        _madeira_se_qemu_tcti_smoke \
        _madeira_se_qemu_tcti_backend; do
        if ! grep -q " $symbol$" <<<"$exported_symbols"; then
            echo "error: $target does not export ${symbol#_}" >&2
            exit 1
        fi
    done
    if ! grep -q ' _tcg_qemu_tb_exec$' <<<"$(nm "$artifact")"; then
        echo "error: $target does not contain the TCTI execution entry" >&2
        exit 1
    fi
    echo "verified $target"
done

if ! grep -q 'CONFIG_TCG_THREADED_INTERPRETER' "$BUILD_DIR/config-host.h"; then
    echo "error: linked probes were not configured for threaded interpretation" >&2
    exit 1
fi
if ! grep -q 'The tcg interpreter does not need execute permission' \
    "$QEMU_SOURCE/tcg/region.c"; then
    echo "error: pinned QEMU no longer contains the non-executable TCTI buffer path" >&2
    exit 1
fi
if ! grep -R -q 'gadget_qemu_ld_leul_unaligned_mode5023_off32_i32' \
    "$BUILD_DIR/tcg"; then
    echo "error: profile-guided MemOp fastpath gadgets were not generated" >&2
    exit 1
fi
if ! grep -R -q 'gadget_qemu_st_ub_unaligned_mode5e03_off32_i32' \
    "$BUILD_DIR/tcg"; then
    echo "error: byte-store MemOp fastpath gadgets were not generated" >&2
    exit 1
fi

PROBE_SOURCE="$REPO_ROOT/madeira-se/tools/qemu_shared_probe.c"
PROBE_BINARY="$BUILD_DIR/madeira-se-qemu-shared-probe"
CORE_BUILD_DIR="${MADEIRA_SE_CORE_BUILD_DIR:-$BUILD_DIR/madeira-se-host}"
if [[ ! -f "$PROBE_SOURCE" ]]; then
    echo "error: shared-library probe source is missing: $PROBE_SOURCE" >&2
    exit 1
fi
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror "$PROBE_SOURCE" \
    -o "$PROBE_BINARY"
for architecture in i386 x86_64; do
    artifact="$BUILD_DIR/libqemu-$architecture-softmmu.dylib"
    if ! perl -e 'alarm shift; exec @ARGV' 20 "$PROBE_BINARY" "$artifact"; then
        echo "error: QEMU $architecture shared-library initialization failed" >&2
        exit 1
    fi
done

cmake -S "$REPO_ROOT/madeira-se" -B "$CORE_BUILD_DIR" \
    -DMADEIRA_SE_BUILD_TESTS=OFF \
    -DMADEIRA_SE_BUILD_TOOLS=ON >"$BUILD_LOG.core-configure" 2>&1
cmake --build "$CORE_BUILD_DIR" --parallel "$BUILD_JOBS" \
    --target \
        madeira-se-qemu-backend-probe \
        madeira-se-qemu-runtime-probe >"$BUILD_LOG.core-build" 2>&1
for architecture in i386 x86_64; do
    artifact="$BUILD_DIR/libqemu-$architecture-softmmu.dylib"
    if ! perl -e 'alarm shift; exec @ARGV' 30 \
            "$CORE_BUILD_DIR/madeira-se-qemu-backend-probe" "$artifact"; then
        echo "error: QEMU $architecture backend execution probe failed" >&2
        exit 1
    fi
    if ! perl -e 'alarm shift; exec @ARGV' 30 \
            "$CORE_BUILD_DIR/madeira-se-qemu-runtime-probe" "$artifact"; then
        echo "error: QEMU $architecture standalone runtime probe failed" >&2
        exit 1
    fi
done

echo "QEMU i386/x86-64 TCTI embedding smoke test passed."
echo "The generated translation buffer is RW data; precompiled gadgets live in Mach-O __TEXT."
