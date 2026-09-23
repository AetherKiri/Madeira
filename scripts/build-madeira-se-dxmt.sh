#!/bin/bash
# Cross-build DXMT's i386/x86-64 PE modules and ARM64 macOS Wine bridge.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DXMT_SOURCE="${MADEIRA_SE_DXMT_SOURCE:-$ROOT/research/dxmt}"
WINE_BUILD="${MADEIRA_SE_WINE_GUEST_BUILD_DIR:-$ROOT/build/madeira-se-wine-guest}"
LLVM_PREFIX="${MADEIRA_SE_LLVM_PREFIX:-$ROOT/toolchains/llvm-madeira-se-macos}"
TOOLCHAIN="${MADEIRA_SE_LLVM_MINGW:-$ROOT/toolchains/llvm-mingw-20260421-ucrt-macos-universal}"
OUTPUT="${MADEIRA_SE_DXMT_BUILD_DIR:-$ROOT/build/madeira-se-dxmt}"
MESON="${MESON:-$ROOT/build/madeira-se-tools-venv/bin/meson}"
JOBS="${MADEIRA_SE_BUILD_JOBS:-2}"

# CommandLineTools does not expose the separately installed Metal compiler.
# Prefer the full Xcode developer directory when the caller did not choose one.
if [[ -z "${DEVELOPER_DIR:-}" && -d /Applications/Xcode.app/Contents/Developer ]]; then
    export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi

if [[ ! -x "$MESON" ]]; then
    MESON="$(command -v meson || true)"
fi
if [[ -z "$MESON" || ! -x "$MESON" ]]; then
    echo "error: Meson 1.3 or later is required" >&2
    exit 1
fi
for path in "$LLVM_PREFIX/include/llvm/IR/Module.h" \
            "$LLVM_PREFIX/lib/libLLVMCore.a" \
            "$WINE_BUILD/tools/winebuild/winebuild"; do
    if [[ ! -e "$path" ]]; then
        echo "error: missing DXMT prerequisite: $path" >&2
        exit 1
    fi
done

mkdir -p "$OUTPUT/config" "$OUTPUT/runtime/i386-windows" \
    "$OUTPUT/runtime/x86_64-windows" "$OUTPUT/runtime/aarch64-unix"
cat > "$OUTPUT/config/native.ini" <<EOF
[binaries]
c = 'clang'
cpp = 'clang++'
EOF

write_cross_file()
{
    local architecture="$1"
    local triplet="$2"
    local family="$3"
    cat > "$OUTPUT/config/$architecture.ini" <<EOF
[binaries]
c = '$TOOLCHAIN/bin/$triplet-clang'
cpp = '$TOOLCHAIN/bin/$triplet-clang++'
ar = '$TOOLCHAIN/bin/$triplet-ar'
strip = '$TOOLCHAIN/bin/$triplet-strip'
windres = '$TOOLCHAIN/bin/$triplet-windres'

[properties]
needs_exe_wrapper = true

[host_machine]
system = 'windows'
cpu_family = '$family'
cpu = '$family'
endian = 'little'
EOF
}

configure_and_build()
{
    local architecture="$1"
    local triplet="$2"
    local family="$3"
    local build_dir="$OUTPUT/$architecture"
    local setup_mode=""

    write_cross_file "$architecture" "$triplet" "$family"
    if [[ -f "$build_dir/meson-private/coredata.dat" ]]; then
        setup_mode="--reconfigure"
    fi
    if [[ -n "$setup_mode" ]]; then
        "$MESON" setup "$setup_mode" "$build_dir" "$DXMT_SOURCE" \
            --cross-file "$OUTPUT/config/$architecture.ini" \
            --native-file "$OUTPUT/config/native.ini" \
            --buildtype release \
            -Dnative_llvm_path="$LLVM_PREFIX" \
            -Dwine_build_path="$WINE_BUILD" \
            -Dwine_builtin_dll=true \
            -Denable_tests=false \
            -Denable_nvapi=false \
            -Denable_nvngx=false
    else
        "$MESON" setup "$build_dir" "$DXMT_SOURCE" \
            --cross-file "$OUTPUT/config/$architecture.ini" \
            --native-file "$OUTPUT/config/native.ini" \
            --buildtype release \
            -Dnative_llvm_path="$LLVM_PREFIX" \
            -Dwine_build_path="$WINE_BUILD" \
            -Dwine_builtin_dll=true \
            -Denable_tests=false \
            -Denable_nvapi=false \
            -Denable_nvngx=false
    fi
    "$MESON" compile -C "$build_dir" -j "$JOBS"
}

copy_module()
{
    local architecture="$1"
    local module="$2"
    local source_directory="$module"
    if [[ "$module" == d3d10core ]]; then
        source_directory=d3d10
    fi
    local source="$OUTPUT/$architecture/src/$source_directory/$module.dll"
    if [[ ! -f "$source" ]]; then
        echo "error: DXMT did not produce $source" >&2
        exit 1
    fi
    ditto "$source" "$OUTPUT/runtime/$architecture-windows/$module.dll"
}

configure_and_build x86_64 x86_64-w64-mingw32 x86_64
configure_and_build i386 i686-w64-mingw32 x86
for architecture in i386 x86_64; do
    for module in winemetal dxgi d3d11 d3d10core d3d9; do
        copy_module "$architecture" "$module"
    done
done

WINEMETAL_SO="$OUTPUT/x86_64/src/winemetal/unix/winemetal.so"
if [[ ! -f "$WINEMETAL_SO" ]] || ! file "$WINEMETAL_SO" | grep -q 'arm64'; then
    echo "error: DXMT did not produce an ARM64 winemetal.so" >&2
    exit 1
fi
ditto "$WINEMETAL_SO" "$OUTPUT/runtime/aarch64-unix/winemetal.so"
# Wine's builtin loader resolves the PE-side winemetal.dll through the
# architecture directory.  Keep a Unix-side companion there as well so the
# builtin module can load its ARM64 implementation in a staged bundle.
for architecture in i386-windows x86_64-windows; do
    ln -sfn ../aarch64-unix/winemetal.so "$OUTPUT/runtime/$architecture/winemetal.so"
done
ditto "$DXMT_SOURCE/dxmt.conf" "$OUTPUT/runtime/dxmt.conf"
echo "Madeira-SE DXMT runtime ready: $OUTPUT/runtime"
