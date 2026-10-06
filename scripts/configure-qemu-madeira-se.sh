#!/bin/bash
# Configure the pinned UTM QEMU fork as embeddable no-JIT x86/TCTI libraries.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOCK_FILE="$REPO_ROOT/madeira-se/deps/qemu-tcti.lock"
QEMU_SOURCE="${MADEIRA_SE_QEMU_SOURCE:-$REPO_ROOT/.deps/qemu-utm}"
PATCH_SCRIPT="$REPO_ROOT/scripts/apply-qemu-madeira-se-patch.sh"
BUILD_DIR="${MADEIRA_SE_QEMU_BUILD_DIR:-$REPO_ROOT/build/madeira-se-qemu}"
CONFIGURE_LOG="${MADEIRA_SE_QEMU_CONFIGURE_LOG:-$REPO_ROOT/build/madeira-se-qemu-configure.log}"
# LTO reduces the final TCTI library size, but its Darwin linker peak memory
# is very high. Keep normal builds usable on development machines and let
# release builders opt in explicitly with MADEIRA_SE_QEMU_LTO=true.
QEMU_LTO="${MADEIRA_SE_QEMU_LTO:-false}"
QEMU_SDK="${MADEIRA_SE_QEMU_SDK:-}"
QEMU_ARCH="${MADEIRA_SE_QEMU_ARCH:-arm64}"
QEMU_HOST_CPU="${MADEIRA_SE_QEMU_CPU:-aarch64}"
QEMU_MIN_VERSION="${MADEIRA_SE_QEMU_MIN_VERSION:-17.0}"
QEMU_PKG_CONFIG_PATH="${MADEIRA_SE_QEMU_PKG_CONFIG_PATH:-}"

# The iOS linker must never consume Homebrew's macOS GLib. The dedicated
# dependency script writes a target-only pkg-config prefix; use it
# automatically when it is already present, while leaving host builds
# untouched.
if [[ -z "$QEMU_PKG_CONFIG_PATH" && -n "$QEMU_SDK" ]]; then
    default_glib_pkgconfig="$REPO_ROOT/build/madeira-se-glib-ios-prefix/lib/pkgconfig"
    if [[ -d "$default_glib_pkgconfig" ]]; then
        QEMU_PKG_CONFIG_PATH="$default_glib_pkgconfig"
    fi
fi

if [[ -z "${DEVELOPER_DIR:-}" && -d /Applications/Xcode.app/Contents/Developer ]]; then
    export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi

# Cross builds must resolve GLib and its transitive dependencies for the
# target SDK.  A host pkg-config entry would otherwise select Homebrew's
# macOS dylib and fail at the final iOS link.  Keep the target directory first
# and, when explicitly supplied, prevent host search-path fallback entirely.
if [[ -n "$QEMU_PKG_CONFIG_PATH" ]]; then
    export PKG_CONFIG_PATH="$QEMU_PKG_CONFIG_PATH${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    export PKG_CONFIG_LIBDIR="${MADEIRA_SE_QEMU_PKG_CONFIG_LIBDIR:-$QEMU_PKG_CONFIG_PATH}"
    echo "Target pkg-config path: $QEMU_PKG_CONFIG_PATH"
fi

if [[ $# -gt 1 ]]; then
    echo "usage: $0 [build-directory]" >&2
    exit 2
fi
if [[ $# -eq 1 ]]; then
    BUILD_DIR="$1"
fi
case "$QEMU_LTO" in
    true|false) ;;
    *)
        echo "error: MADEIRA_SE_QEMU_LTO must be true or false" >&2
        exit 2
        ;;
esac
if [[ ! -f "$LOCK_FILE" ]]; then
    echo "error: dependency lock is missing: $LOCK_FILE" >&2
    exit 1
fi

# shellcheck source=/dev/null
source "$LOCK_FILE"
: "${QEMU_COMMIT:?missing QEMU_COMMIT}"

if [[ ! -x "$QEMU_SOURCE/configure" ]]; then
    echo "error: QEMU source is missing: $QEMU_SOURCE" >&2
    echo "run scripts/fetch-madeira-se-qemu.sh first" >&2
    exit 1
fi
actual_commit="$(git -C "$QEMU_SOURCE" rev-parse HEAD)"
if [[ "$actual_commit" != "$QEMU_COMMIT" ]]; then
    echo "error: QEMU checkout is at $actual_commit, expected $QEMU_COMMIT" >&2
    exit 1
fi
if [[ ! -f "$QEMU_SOURCE/tcg/aarch64-tcti/tcg-target.c.inc" ]]; then
    echo "error: the pinned QEMU checkout has no AArch64 TCTI backend" >&2
    exit 1
fi
MADEIRA_SE_QEMU_SOURCE="$QEMU_SOURCE" "$PATCH_SCRIPT"

python_is_usable()
{
    "$1" -c 'import sys; raise SystemExit(sys.version_info < (3, 10))' \
        >/dev/null 2>&1
}

if [[ -n "${MADEIRA_SE_PYTHON:-}" ]]; then
    PYTHON="$MADEIRA_SE_PYTHON"
    if ! python_is_usable "$PYTHON"; then
        echo "error: MADEIRA_SE_PYTHON must point to Python 3.10 or newer" >&2
        exit 1
    fi
else
    PYTHON=""
    for candidate in \
        /opt/homebrew/bin/python3.13 \
        /opt/homebrew/bin/python3 \
        /usr/local/bin/python3.13 \
        /usr/local/bin/python3 \
        python3.13 python3.12 python3.11 python3.10 python3; do
        if [[ "$candidate" == */* ]]; then
            candidate_path="$candidate"
        else
            candidate_path="$(command -v "$candidate" 2>/dev/null || true)"
        fi
        if [[ -n "$candidate_path" ]] && python_is_usable "$candidate_path"; then
            PYTHON="$candidate_path"
            break
        fi
    done
    if [[ -z "$PYTHON" ]]; then
        echo "error: QEMU TCTI's gadget generator requires Python 3.10 or newer" >&2
        exit 1
    fi
fi
export PYTHON

mkdir -p "$BUILD_DIR"
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"

echo "Configuring QEMU x86 TCTI probe in $BUILD_DIR"
echo "QEMU revision: $actual_commit"
echo "Python: $PYTHON ($("$PYTHON" -c 'import sys; print(sys.version.split()[0])'))"
echo "LTO: $QEMU_LTO"

qemu_lto_args=()
if [[ "$QEMU_LTO" == true ]]; then
    qemu_lto_args+=("-Db_lto=true")
fi

qemu_tool_args=()
qemu_extra_cflags=""
qemu_extra_ldflags=""
if [[ -n "$QEMU_SDK" ]]; then
    sdk_path="$(xcrun --sdk "$QEMU_SDK" --show-sdk-path)"
    qemu_cc="$(xcrun --sdk "$QEMU_SDK" --find clang)"
    qemu_cxx="$(xcrun --sdk "$QEMU_SDK" --find clang++)"
    qemu_ar="$(xcrun --sdk "$QEMU_SDK" --find ar)"
    qemu_ranlib="$(xcrun --sdk "$QEMU_SDK" --find ranlib)"
    qemu_strip="$(xcrun --sdk "$QEMU_SDK" --find strip)"
    qemu_nm="$(xcrun --sdk "$QEMU_SDK" --find nm)"
    host_cc="$(xcrun --sdk macosx --find clang)"
    qemu_target="${QEMU_ARCH}-apple-ios"
    qemu_extra_cflags="-target $qemu_target -isysroot $sdk_path -miphoneos-version-min=$QEMU_MIN_VERSION"
    qemu_extra_ldflags="$qemu_extra_cflags"
    # An empty --cross-prefix still switches QEMU's configure to Meson's
    # cross-file mode; the explicit tool paths above keep the host generator
    # and target library compilers separate.
    qemu_tool_args+=(
        "--cross-prefix="
        "--cc=$qemu_cc"
        "--cxx=$qemu_cxx"
        "--host-cc=$host_cc"
        "--cpu=$QEMU_HOST_CPU"
    )
    export AR="$qemu_ar" RANLIB="$qemu_ranlib" STRIP="$qemu_strip" NM="$qemu_nm"
    echo "Apple target SDK: $QEMU_SDK ($sdk_path)"
    echo "Apple target architecture: $qemu_target"
fi

if ! (
    cd "$BUILD_DIR"
    "$QEMU_SOURCE/configure" \
        "${qemu_tool_args[@]+${qemu_tool_args[@]}}" \
        --target-list=x86_64-softmmu,i386-softmmu \
        --without-default-features \
        --enable-system \
        --enable-tcg \
        --enable-tcg-threaded-interpreter \
        --enable-shared-lib \
        --disable-debug-info \
        --disable-qom-cast-debug \
        -Doptimization=3 \
        -Dmadeira_se_performance=true \
        -Dtrace_backends=nop \
        "${qemu_lto_args[@]+${qemu_lto_args[@]}}" \
        --extra-cflags="-I$REPO_ROOT/madeira-se/include $qemu_extra_cflags" \
        --extra-ldflags="$qemu_extra_ldflags" \
        --without-default-devices \
        --disable-docs \
        --disable-tools \
        --disable-guest-agent \
        --disable-slirp \
        --disable-cocoa \
        --disable-hvf
) >"$CONFIGURE_LOG" 2>&1; then
    echo "error: QEMU TCTI configuration failed; tail of $CONFIGURE_LOG:" >&2
    tail -n 200 "$CONFIGURE_LOG" >&2
    exit 1
fi

if ! grep -q '^#define CONFIG_TCG_THREADED_INTERPRETER' \
    "$BUILD_DIR/config-host.h"; then
    echo "error: configured QEMU build did not enable TCTI" >&2
    exit 1
fi
if ! grep -q '^#define CONFIG_SHARED_LIBRARY_BUILD' \
    "$BUILD_DIR/config-host.h"; then
    echo "error: configured QEMU build did not enable shared libraries" >&2
    exit 1
fi
for target in x86_64-softmmu i386-softmmu; do
    if [[ ! -f "$BUILD_DIR/$target-config-target.h" ]]; then
        echo "error: configured QEMU build is missing target $target" >&2
        exit 1
    fi
done

echo "QEMU TCTI shared-library configuration verified."
echo "Madeira-SE embeds this CPU/TCG core in the Wine process; it does not boot a VM."
