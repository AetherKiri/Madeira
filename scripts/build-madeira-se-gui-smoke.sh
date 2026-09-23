#!/bin/bash
# Build the tiny i386/x86-64 GUI, CoreAudio and DXMT regression programs.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TOOLCHAIN="${MADEIRA_SE_LLVM_MINGW:-$ROOT/toolchains/llvm-mingw-20260421-ucrt-macos-universal}"
OUTPUT_DIR="${MADEIRA_SE_GUI_SMOKE_DIR:-$ROOT/build/madeira-se-gui-smoke}"

mkdir -p "$OUTPUT_DIR"
for architecture in i386 x86_64; do
    if [[ "$architecture" == i386 ]]; then
        compiler="$TOOLCHAIN/bin/i686-w64-mingw32-clang"
    else
        compiler="$TOOLCHAIN/bin/x86_64-w64-mingw32-clang"
    fi
    if [[ ! -x "$compiler" ]]; then
        echo "error: missing compiler $compiler" >&2
        exit 1
    fi
    "$compiler" -O1 -Wall -Wextra -Werror -mwindows \
    "$ROOT/madeira-se/tools/gui_smoke.c" -o "$OUTPUT_DIR/gui-smoke-$architecture.exe" \
        -luser32 -lgdi32
    echo "built $OUTPUT_DIR/gui-smoke-$architecture.exe"
    "$compiler" -O1 -Wall -Wextra -Werror -mwindows \
        "$ROOT/madeira-se/tools/fps_smoke.c" -o "$OUTPUT_DIR/fps-smoke-$architecture.exe" \
        -lopengl32 -luser32 -lgdi32
    echo "built $OUTPUT_DIR/fps-smoke-$architecture.exe"
    "$compiler" -O1 -Wall -Wextra -Werror \
        "$ROOT/madeira-se/tools/audio_smoke.c" -o "$OUTPUT_DIR/audio-smoke-$architecture.exe" \
        -lwinmm
    echo "built $OUTPUT_DIR/audio-smoke-$architecture.exe"
    "$compiler" -O1 -Wall -Wextra -Werror \
        "$ROOT/madeira-se/tools/d3d11_smoke.c" -o "$OUTPUT_DIR/d3d11-smoke-$architecture.exe" \
        -ld3d11 -ldxgi
    echo "built $OUTPUT_DIR/d3d11-smoke-$architecture.exe"
    "$compiler" -O1 -Wall -Wextra -Werror \
        "$ROOT/madeira-se/tools/d3d9_smoke.c" -o "$OUTPUT_DIR/d3d9-smoke-$architecture.exe" \
        -ld3d9 -luser32
    echo "built $OUTPUT_DIR/d3d9-smoke-$architecture.exe"
done
