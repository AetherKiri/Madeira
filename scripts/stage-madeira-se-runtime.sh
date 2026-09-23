#!/bin/bash
# Assemble the standalone Madeira-SE resource bundle used by the runner or app.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DESTINATION="${1:-$ROOT/build/madeira-se-runtime}"
HOST_DIR="${MADEIRA_SE_WINE_HOST_BUILD_DIR:-$ROOT/build/madeira-se-wine-native-host}"
GUEST_DIR="${MADEIRA_SE_WINE_GUEST_BUILD_DIR:-$ROOT/build/madeira-se-wine-guest}"
QEMU_DIR="${MADEIRA_SE_QEMU_BUILD_DIR:-$ROOT/build/madeira-se-qemu}"
CORE_DIR="${MADEIRA_SE_BUILD_DIR:-$ROOT/build/madeira-se-core}"
DXMT_DIR="${MADEIRA_SE_DXMT_DIR:-$ROOT/build/madeira-se-dxmt/runtime}"
MODE="${MADEIRA_SE_STAGE_MODE:-link}"

find_native_library()
{
    local override="$1"
    shift
    if [[ -n "$override" && -f "$override" ]]; then
        printf '%s\n' "$override"
        return 0
    fi
    local candidate
    for candidate in "$@"; do
        if [[ -f "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

FREETYPE_DYLIB="$(find_native_library "${MADEIRA_SE_FREETYPE_DYLIB:-}" \
    /opt/homebrew/opt/freetype/lib/libfreetype.6.dylib \
    /usr/local/opt/freetype/lib/libfreetype.6.dylib)" || {
    echo "error: libfreetype.6.dylib was not found" >&2
    exit 1
}
PNG_DYLIB="$(find_native_library "${MADEIRA_SE_PNG_DYLIB:-}" \
    /opt/homebrew/opt/libpng/lib/libpng16.16.dylib \
    /usr/local/opt/libpng/lib/libpng16.16.dylib)" || {
    echo "error: libpng16.16.dylib was not found" >&2
    exit 1
}

if [[ "$MODE" != link && "$MODE" != copy ]]; then
    echo "error: MADEIRA_SE_STAGE_MODE must be link or copy" >&2
    exit 2
fi

remove_path()
{
    # shutil handles directories, regular files and broken symlinks without
    # following a link into a user's checkout.
    python3 - "$1" <<'PY'
from pathlib import Path
import shutil
import sys

path = Path(sys.argv[1])
if path.is_symlink() or path.is_file():
    path.unlink()
elif path.exists():
    shutil.rmtree(path)
PY
}

copy_path()
{
    # copytree(..., symlinks=False) dereferences the build tree's generated
    # links, including the DXMT Unix companion links, so copy-mode bundles do
    # not retain paths into the checkout.
    python3 - "$1" "$2" <<'PY'
from pathlib import Path
import shutil
import sys

source = Path(sys.argv[1])
target = Path(sys.argv[2])
if source.is_dir():
    shutil.copytree(source, target, symlinks=False)
else:
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target, follow_symlinks=True)
PY
}

for artifact in \
    "$HOST_DIR/loader/wine" "$HOST_DIR/server/wineserver" \
    "$HOST_DIR/dlls/ntdll/ntdll.so" "$GUEST_DIR/dlls/ntdll/i386-windows/ntdll.dll" \
    "$GUEST_DIR/dlls/ntdll/x86_64-windows/ntdll.dll" \
    "$QEMU_DIR/libqemu-i386-softmmu.dylib" \
    "$QEMU_DIR/libqemu-x86_64-softmmu.dylib" \
    "$CORE_DIR/libmadeira_se_runtime.dylib" "$CORE_DIR/madeira-se-run" \
    "$DXMT_DIR/aarch64-unix/winemetal.so" \
    "$DXMT_DIR/i386-windows/d3d9.dll" \
    "$DXMT_DIR/i386-windows/d3d11.dll" \
    "$DXMT_DIR/i386-windows/dxgi.dll" \
    "$DXMT_DIR/i386-windows/d3d10core.dll" \
    "$DXMT_DIR/i386-windows/winemetal.dll" \
    "$DXMT_DIR/x86_64-windows/d3d9.dll" \
    "$DXMT_DIR/x86_64-windows/d3d11.dll" \
    "$DXMT_DIR/x86_64-windows/dxgi.dll" \
    "$DXMT_DIR/x86_64-windows/d3d10core.dll" \
    "$DXMT_DIR/x86_64-windows/winemetal.dll"; do
    if [[ ! -e "$artifact" ]]; then
        echo "error: missing build artifact: $artifact" >&2
        exit 1
    fi
done

mkdir -p "$DESTINATION"
stage_tree()
{
    local source="$1"
    local name="$2"
    local target="$DESTINATION/$name"
    remove_path "$target"
    if [[ "$MODE" == link ]]; then
        ln -s "$source" "$target"
    else
        copy_path "$source" "$target"
    fi
}

stage_tree "$HOST_DIR" host-arm64
stage_tree "$GUEST_DIR" guest
stage_tree "$DXMT_DIR" dxmt
remove_path "$DESTINATION/native-libs"
mkdir -p "$DESTINATION/native-libs"
if [[ "$MODE" == link ]]; then
    ln -s "$FREETYPE_DYLIB" "$DESTINATION/native-libs/libfreetype.6.dylib"
    ln -s "$PNG_DYLIB" "$DESTINATION/native-libs/libpng16.16.dylib"
else
    ditto "$FREETYPE_DYLIB" "$DESTINATION/native-libs/libfreetype.6.dylib"
    ditto "$PNG_DYLIB" "$DESTINATION/native-libs/libpng16.16.dylib"
    chmod u+w "$DESTINATION/native-libs/libfreetype.6.dylib" \
        "$DESTINATION/native-libs/libpng16.16.dylib"
    FREETYPE_PNG_NAME="$(otool -L "$DESTINATION/native-libs/libfreetype.6.dylib" | \
        awk '/libpng/{print $1; exit}')"
    if [[ -n "$FREETYPE_PNG_NAME" ]]; then
        install_name_tool -change "$FREETYPE_PNG_NAME" \
            @loader_path/libpng16.16.dylib \
            "$DESTINATION/native-libs/libfreetype.6.dylib"
    fi
    install_name_tool -id @rpath/libfreetype.6.dylib \
        "$DESTINATION/native-libs/libfreetype.6.dylib"
    install_name_tool -id @rpath/libpng16.16.dylib \
        "$DESTINATION/native-libs/libpng16.16.dylib"
    # install_name_tool invalidates Homebrew's ad-hoc signature.  Re-sign the
    # two copied dylibs so dyld does not terminate a signed Wine host with
    # SIGKILL when it first loads CoreText/FreeType resources.  The final app
    # archive will be signed again with its distribution identity.
    codesign --force --sign - \
        "$DESTINATION/native-libs/libfreetype.6.dylib" \
        "$DESTINATION/native-libs/libpng16.16.dylib" >/dev/null
fi
remove_path "$DESTINATION/qemu"
mkdir -p "$DESTINATION/qemu"
if [[ "$MODE" == link ]]; then
    ln -s "$QEMU_DIR/libqemu-i386-softmmu.dylib" \
        "$DESTINATION/qemu/libqemu-i386-softmmu.dylib"
    ln -s "$QEMU_DIR/libqemu-x86_64-softmmu.dylib" \
        "$DESTINATION/qemu/libqemu-x86_64-softmmu.dylib"
else
    ditto "$QEMU_DIR/libqemu-i386-softmmu.dylib" \
        "$DESTINATION/qemu/libqemu-i386-softmmu.dylib"
    ditto "$QEMU_DIR/libqemu-x86_64-softmmu.dylib" \
        "$DESTINATION/qemu/libqemu-x86_64-softmmu.dylib"
fi
remove_path "$DESTINATION/madeira-se-runtime.dylib"
if [[ "$MODE" == link ]]; then
    ln -s "$CORE_DIR/libmadeira_se_runtime.dylib" \
        "$DESTINATION/madeira-se-runtime.dylib"
else
    ditto "$CORE_DIR/libmadeira_se_runtime.dylib" \
        "$DESTINATION/madeira-se-runtime.dylib"
fi
remove_path "$DESTINATION/madeira-se-run"
if [[ "$MODE" == link ]]; then
    ln -s "$CORE_DIR/madeira-se-run" "$DESTINATION/madeira-se-run"
else
    ditto "$CORE_DIR/madeira-se-run" "$DESTINATION/madeira-se-run"
fi

cat > "$DESTINATION/manifest.txt" <<EOF
Madeira-SE resource bundle
mode=$MODE
host=$HOST_DIR
guest=$GUEST_DIR
qemu=$QEMU_DIR
runtime=$CORE_DIR/libmadeira_se_runtime.dylib
dxmt=$DXMT_DIR
freetype=$FREETYPE_DYLIB
libpng=$PNG_DYLIB
EOF
echo "Madeira-SE bundle ready: $DESTINATION ($MODE)"
