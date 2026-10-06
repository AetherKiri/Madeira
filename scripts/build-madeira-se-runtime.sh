#!/usr/bin/env bash
# Build the small Madeira-SE host runtime used by Wine's no-JIT provider.
# QEMU remains a separately signed backend; this library only owns the stable
# CPU/Wine transport and its process-wide host state.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SDK_NAME="${MADEIRA_SE_SDK:-macosx}"
ARCH="${MADEIRA_SE_ARCH:-arm64}"
OUT_DIR="${MADEIRA_SE_RUNTIME_BUILD:-$REPO_ROOT/build/madeira-se-runtime}"
APP_RUNTIME="${MADEIRA_SE_RUNTIME_APP_LIB:-$REPO_ROOT/app/Madeira/libmadeira_se_runtime.a}"
JOBS="${MADEIRA_SE_BUILD_JOBS:-2}"

# CommandLineTools does not include the iPhoneOS SDK.  On developer Macs the
# full Xcode installation is normally present even when xcode-select still
# points at CommandLineTools; select it automatically for target builds while
# preserving an explicit DEVELOPER_DIR supplied by CI.
if [[ -z "${DEVELOPER_DIR:-}" && -d /Applications/Xcode.app/Contents/Developer ]]; then
    export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi

case "$SDK_NAME" in
    iphoneos) TARGET_TRIPLE="${ARCH}-apple-ios" ;;
    iphonesimulator) TARGET_TRIPLE="${ARCH}-apple-ios-simulator" ;;
    macosx) TARGET_TRIPLE="${ARCH}-apple-macos" ;;
    *) echo "error: unsupported Apple SDK: $SDK_NAME" >&2; exit 2 ;;
esac

if ! [[ "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: MADEIRA_SE_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi

SDK_PATH="$(xcrun --sdk "$SDK_NAME" --show-sdk-path)"
CC="$(xcrun --sdk "$SDK_NAME" --find clang)"
AR="$(xcrun --sdk "$SDK_NAME" --find ar)"
RANLIB="$(xcrun --sdk "$SDK_NAME" --find ranlib)"

mkdir -p "$OUT_DIR/obj"
objects=()
for source in "$REPO_ROOT"/madeira-se/src/*.c; do
    name="$(basename "$source" .c)"
    object="$OUT_DIR/obj/$name.o"
    echo "compile $name ($SDK_NAME/$ARCH)"
    "$CC" -target "$TARGET_TRIPLE" -isysroot "$SDK_PATH" \
        -O2 -g0 -fPIC -fvisibility=hidden \
        -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -I"$REPO_ROOT/madeira-se/include" -c "$source" -o "$object"
    objects+=("$object")
done

"$AR" rcs "$OUT_DIR/libmadeira_se_runtime.a" "${objects[@]}"
"$RANLIB" "$OUT_DIR/libmadeira_se_runtime.a"

# A signed app can use the shared form when the backend is bundled as a
# nested, code-signed library. The static archive is the App Store default and
# lets the linker keep the Wine host in the main executable.
"$CC" -target "$TARGET_TRIPLE" -isysroot "$SDK_PATH" \
    -dynamiclib -install_name "@rpath/libmadeira_se_runtime.dylib" \
    -Wl,-no_compact_unwind "${objects[@]}" -o "$OUT_DIR/libmadeira_se_runtime.dylib"

nm -gU "$OUT_DIR/libmadeira_se_runtime.a" | grep -q '_madeira_se_wine_cpu_dispatch_message$'
nm -gU "$OUT_DIR/libmadeira_se_runtime.a" | grep -q '_madeira_se_wine_qemu_runtime_start$'
if [[ "$SDK_NAME" == "iphoneos" ]]; then
    cp "$OUT_DIR/libmadeira_se_runtime.a" "$APP_RUNTIME"
    echo "staged static runtime: $APP_RUNTIME"
fi
file "$OUT_DIR/libmadeira_se_runtime.a" "$OUT_DIR/libmadeira_se_runtime.dylib"
echo "Madeira-SE runtime built in $OUT_DIR"
