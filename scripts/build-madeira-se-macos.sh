#!/usr/bin/env bash
# Build and verify the standalone Apple Silicon macOS Madeira-SE runtime.
#
# This target is a native ARM64 Wine host. Windows PE32 and PE32+ modules are
# supplied by the split Wine guest tree and execute through the no-JIT QEMU
# TCTI libraries. Wine/DXMT are deliberately checked as inputs here: their
# configure/build scripts are independent and can take considerably longer
# than the macOS core build.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

CORE_BUILD_DIR="${MADEIRA_SE_CORE_BUILD_DIR:-$REPO_ROOT/build/madeira-se-core}"
RUNTIME_BUILD_DIR="${MADEIRA_SE_RUNTIME_BUILD_DIR:-$REPO_ROOT/build/madeira-se-runtime-macos}"
QEMU_BUILD_DIR="${MADEIRA_SE_QEMU_BUILD_DIR:-$REPO_ROOT/build/madeira-se-qemu}"
WINE_HOST_BUILD_DIR="${MADEIRA_SE_WINE_HOST_BUILD_DIR:-$REPO_ROOT/build/madeira-se-wine-native-host}"
WINE_GUEST_BUILD_DIR="${MADEIRA_SE_WINE_GUEST_BUILD_DIR:-$REPO_ROOT/build/madeira-se-wine-guest}"
DXMT_RUNTIME_DIR="${MADEIRA_SE_DXMT_RUNTIME_DIR:-$REPO_ROOT/build/madeira-se-dxmt/runtime}"
MANIFEST_DIR="${MADEIRA_SE_MACOS_MANIFEST_DIR:-$REPO_ROOT/build/madeira-se-macos}"
JOBS="${MADEIRA_SE_BUILD_JOBS:-2}"
RUN_TESTS="${MADEIRA_SE_MACOS_RUN_TESTS:-1}"

if [[ "$(uname -s)" != Darwin ]]; then
    echo "error: the standalone macOS target must be built on Darwin" >&2
    exit 1
fi
if [[ "$(uname -m)" != arm64 ]]; then
    echo "error: the current macOS target requires an Apple Silicon host (arm64)" >&2
    exit 1
fi
if [[ -z "${DEVELOPER_DIR:-}" && -d /Applications/Xcode.app/Contents/Developer ]]; then
    export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi
if ! [[ "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: MADEIRA_SE_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi

require_file() {
    if [[ ! -f "$1" ]]; then
        echo "error: missing required artifact: $1" >&2
        return 1
    fi
}

require_macho_arm64() {
    local artifact="$1"
    require_file "$artifact"
    if ! file -b "$artifact" | grep -Eq 'Mach-O.*(arm64|ARM64)'; then
        echo "error: expected an ARM64 Mach-O artifact: $artifact" >&2
        file -b "$artifact" >&2 || true
        return 1
    fi
}

require_pe() {
    local artifact="$1"
    local pattern="$2"
    require_file "$artifact"
    if ! file -b "$artifact" | grep -Eq "$pattern"; then
        echo "error: guest artifact has the wrong architecture: $artifact" >&2
        file -b "$artifact" >&2 || true
        return 1
    fi
}

echo "Configuring Madeira-SE core in $CORE_BUILD_DIR"
cmake -S "$REPO_ROOT/madeira-se" -B "$CORE_BUILD_DIR" \
    -DMADEIRA_SE_BUILD_TESTS=ON \
    -DMADEIRA_SE_BUILD_TOOLS=ON \
    -DMADEIRA_SE_BUILD_SHARED_RUNTIME=ON
cmake --build "$CORE_BUILD_DIR" --parallel "$JOBS"
if [[ "$RUN_TESTS" != 0 ]]; then
    ctest --test-dir "$CORE_BUILD_DIR" --output-on-failure
fi

echo "Building the macOS SDK runtime in $RUNTIME_BUILD_DIR"
MADEIRA_SE_SDK=macosx \
MADEIRA_SE_ARCH=arm64 \
MADEIRA_SE_RUNTIME_BUILD="$RUNTIME_BUILD_DIR" \
    "$SCRIPT_DIR/build-madeira-se-runtime.sh"

CORE_RUNNER="$CORE_BUILD_DIR/madeira-se-run"
CORE_LIBRARY="$CORE_BUILD_DIR/libmadeira_se_runtime.0.1.0.dylib"
QEMU_I386="$QEMU_BUILD_DIR/libqemu-i386-softmmu.dylib"
QEMU_X86_64="$QEMU_BUILD_DIR/libqemu-x86_64-softmmu.dylib"
HOST_LOADER="$WINE_HOST_BUILD_DIR/loader/wine"
HOST_WINESERVER="$WINE_HOST_BUILD_DIR/server/wineserver"
HOST_NTDLL="$WINE_HOST_BUILD_DIR/dlls/ntdll/ntdll.so"
HOST_WINEMAC="$WINE_HOST_BUILD_DIR/dlls/winemac.drv/winemac.so"

require_macho_arm64 "$CORE_RUNNER"
require_macho_arm64 "$CORE_LIBRARY"
require_macho_arm64 "$RUNTIME_BUILD_DIR/libmadeira_se_runtime.dylib"
require_macho_arm64 "$QEMU_I386"
require_macho_arm64 "$QEMU_X86_64"
require_macho_arm64 "$HOST_LOADER"
require_macho_arm64 "$HOST_WINESERVER"
require_macho_arm64 "$HOST_NTDLL"
require_macho_arm64 "$HOST_WINEMAC"

for symbol in _get_win_data _release_win_data _macdrv_get_client_view \
    _macdrv_view_create_metal_view _macdrv_view_get_metal_layer \
    _macdrv_view_release_metal_view; do
    if ! nm -gU "$HOST_WINEMAC" | grep -q "$symbol$"; then
        echo "error: $HOST_WINEMAC does not export $symbol" >&2
        exit 1
    fi
done

for architecture in i386 x86_64; do
    if [[ "$architecture" == i386 ]]; then
        pe_pattern='PE32 executable.*Intel 80386'
    else
        pe_pattern='PE32\+ executable.*x86-64'
    fi
    for module in apisetschema/"$architecture"-windows/apisetschema.dll \
        ntdll/"$architecture"-windows/ntdll.dll \
        kernelbase/"$architecture"-windows/kernelbase.dll \
        kernel32/"$architecture"-windows/kernel32.dll \
        win32u/"$architecture"-windows/win32u.dll; do
        require_pe "$WINE_GUEST_BUILD_DIR/dlls/$module" "$pe_pattern"
    done
    require_pe "$WINE_GUEST_BUILD_DIR/programs/wineboot/$architecture-windows/wineboot.exe" "$pe_pattern"
    for module in d3d10core.dll d3d11.dll d3d9.dll dxgi.dll winemetal.dll; do
        require_pe "$DXMT_RUNTIME_DIR/$architecture-windows/$module" "$pe_pattern"
    done
done
require_macho_arm64 "$DXMT_RUNTIME_DIR/aarch64-unix/winemetal.so"

mkdir -p "$MANIFEST_DIR"
cat >"$MANIFEST_DIR/manifest.env" <<EOF
# Generated by scripts/build-madeira-se-macos.sh
MADEIRA_SE_CORE_BUILD_DIR=$CORE_BUILD_DIR
MADEIRA_SE_RUNTIME_BUILD_DIR=$RUNTIME_BUILD_DIR
MADEIRA_SE_QEMU_BUILD_DIR=$QEMU_BUILD_DIR
MADEIRA_SE_WINE_HOST_BUILD_DIR=$WINE_HOST_BUILD_DIR
MADEIRA_SE_WINE_GUEST_BUILD_DIR=$WINE_GUEST_BUILD_DIR
MADEIRA_SE_DXMT_RUNTIME_DIR=$DXMT_RUNTIME_DIR
MADEIRA_SE_HOST_ARCH=arm64
MADEIRA_SE_GUEST_ARCHS=i386,x86_64
EOF

echo "macOS Madeira-SE artifacts verified"
echo "manifest: $MANIFEST_DIR/manifest.env"
