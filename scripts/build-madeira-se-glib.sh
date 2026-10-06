#!/usr/bin/env bash
# Build the static arm64 iOS GLib dependency used by QEMU TCTI.
#
# QEMU's normal macOS build can use Homebrew GLib. An App Store binary cannot
# link that dylib, so the iOS build uses GLib and its small dependency set as
# static target libraries. The generated prefix is consumed through
# MADEIRA_SE_QEMU_PKG_CONFIG_PATH by configure-qemu-madeira-se.sh.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOCK_FILE="$REPO_ROOT/madeira-se/deps/glib.lock"
SDK_NAME="${MADEIRA_SE_SDK:-iphoneos}"
ARCH="${MADEIRA_SE_ARCH:-arm64}"
MIN_VERSION="${MADEIRA_SE_MIN_IOS_VERSION:-17.0}"
BUILD_DIR="${MADEIRA_SE_GLIB_BUILD_DIR:-$REPO_ROOT/build/madeira-se-glib-ios}"
PREFIX="${MADEIRA_SE_GLIB_PREFIX:-$REPO_ROOT/build/madeira-se-glib-ios-prefix}"
JOBS="${MADEIRA_SE_BUILD_JOBS:-2}"

# shellcheck source=/dev/null
source "$LOCK_FILE"
SOURCE_DIR="${MADEIRA_SE_GLIB_SOURCE:-$REPO_ROOT/.deps/glib-$GLIB_VERSION}"
[[ "$BUILD_DIR" = /* ]] || BUILD_DIR="$REPO_ROOT/$BUILD_DIR"
[[ "$PREFIX" = /* ]] || PREFIX="$REPO_ROOT/$PREFIX"

if [[ -z "${DEVELOPER_DIR:-}" && -d /Applications/Xcode.app/Contents/Developer ]]; then
    export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi
if [[ "$SDK_NAME" != iphoneos ]]; then
    echo "error: this App Store dependency builder currently targets iphoneos" >&2
    exit 2
fi
if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: MADEIRA_SE_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi

fetch_source()
{
    if [[ -f "$SOURCE_DIR/meson.build" ]]; then
        return
    fi

    archive_dir="${MADEIRA_SE_DOWNLOAD_DIR:-$REPO_ROOT/.deps/downloads}"
    archive="$archive_dir/glib-$GLIB_VERSION.tar.xz"
    mkdir -p "$archive_dir" "$REPO_ROOT/.deps"
    if [[ ! -f "$archive" ]]; then
        command -v curl >/dev/null 2>&1 || {
            echo "error: curl is required to fetch GLib" >&2
            exit 1
        }
        echo "downloading GLib $GLIB_VERSION"
        curl --fail --location --retry 3 --retry-delay 2 \
            --output "$archive" "$GLIB_URL"
    fi
    printf '%s  %s\n' "$GLIB_SHA512" "$archive" | shasum -a 512 -c -
    tar -xf "$archive" -C "$REPO_ROOT/.deps"
    [[ -f "$SOURCE_DIR/meson.build" ]] || {
        echo "error: GLib archive did not create $SOURCE_DIR" >&2
        exit 1
    }
}

fetch_source

SDK_PATH="$(xcrun --sdk "$SDK_NAME" --show-sdk-path)"
IOS_CC="$(xcrun --sdk "$SDK_NAME" --find clang)"
IOS_CXX="$(xcrun --sdk "$SDK_NAME" --find clang++)"
IOS_AR="$(xcrun --sdk "$SDK_NAME" --find ar)"
IOS_RANLIB="$(xcrun --sdk "$SDK_NAME" --find ranlib)"
MAC_SDK_PATH="$(xcrun --sdk macosx --show-sdk-path)"
MAC_CC="$(xcrun --sdk macosx --find clang)"
MAC_CXX="$(xcrun --sdk macosx --find clang++)"

MESON="${MADEIRA_SE_MESON:-}"
if [[ -z "$MESON" && -x "$REPO_ROOT/build/madeira-se-qemu-ios/pyvenv/bin/meson" ]]; then
    MESON="$REPO_ROOT/build/madeira-se-qemu-ios/pyvenv/bin/meson"
fi
if [[ -z "$MESON" ]]; then
    MESON="$(command -v meson 2>/dev/null || true)"
fi
[[ -n "$MESON" && -x "$MESON" ]] || {
    echo "error: Meson is required; configure QEMU first or set MADEIRA_SE_MESON" >&2
    exit 1
}

mkdir -p "$BUILD_DIR" "$PREFIX/lib/pkgconfig"
cross_file="$BUILD_DIR/meson-cross.ini"
native_file="$BUILD_DIR/meson-native.ini"
cat >"$cross_file" <<EOF
[binaries]
c = ['$IOS_CC', '-target', '$ARCH-apple-ios', '-isysroot', '$SDK_PATH', '-miphoneos-version-min=$MIN_VERSION']
cpp = ['$IOS_CXX', '-target', '$ARCH-apple-ios', '-isysroot', '$SDK_PATH', '-miphoneos-version-min=$MIN_VERSION']
objc = ['$IOS_CC', '-target', '$ARCH-apple-ios', '-isysroot', '$SDK_PATH', '-miphoneos-version-min=$MIN_VERSION']
ar = ['$IOS_AR']
ranlib = ['$IOS_RANLIB']
pkgconfig = ['/usr/bin/false']
[built-in options]
c_args = ['-fPIC']
cpp_args = ['-fPIC']
objc_args = ['-fPIC']
[host_machine]
system = 'ios'
cpu_family = 'aarch64'
cpu = '$ARCH'
endian = 'little'
EOF
cat >"$native_file" <<EOF
[binaries]
c = ['$MAC_CC', '-isysroot', '$MAC_SDK_PATH']
cpp = ['$MAC_CXX', '-isysroot', '$MAC_SDK_PATH']
objc = ['$MAC_CC', '-isysroot', '$MAC_SDK_PATH']
EOF

echo "Configuring static GLib $GLIB_VERSION for $SDK_NAME/$ARCH"
"$MESON" setup "$BUILD_DIR" "$SOURCE_DIR" \
    --cross-file "$cross_file" \
    --native-file "$native_file" \
    --prefix "$PREFIX" \
    --wrap-mode=forcefallback \
    -Ddocumentation=false \
    -Ddtrace=disabled \
    -Dinstalled_tests=false \
    -Dintrospection=disabled \
    -Dlibelf=disabled \
    -Dlibmount=disabled \
    -Dman-pages=disabled \
    -Dselinux=disabled \
    -Dsysprof=disabled \
    -Dtests=false \
    -Dxattr=false \
    -Ddefault_library=static \
    -Dglib_debug=disabled
ninja -C "$BUILD_DIR" -j"$JOBS" install

for library in libglib-2.0.a libgmodule-2.0.a libgobject-2.0.a libgio-2.0.a; do
    [[ -f "$PREFIX/lib/$library" ]] || {
        echo "error: missing target GLib library: $PREFIX/lib/$library" >&2
        exit 1
    }
done
object="$(ar -t "$PREFIX/lib/libglib-2.0.a" | grep '\.o$' | head -1)"
probe_dir="$(mktemp -d /tmp/madeira-se-glib-XXXXXX)"
(cd "$probe_dir" && ar -x "$PREFIX/lib/libglib-2.0.a" "$object")
if ! file "$probe_dir/$object" | grep -q 'Mach-O 64-bit object arm64'; then
    echo "error: GLib archive is not an arm64 target archive" >&2
    exit 1
fi
echo "Static iOS GLib prefix: $PREFIX"
