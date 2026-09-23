#!/bin/bash
# Configure the split Wine trees used by the standalone Madeira-SE runtime.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
WINE_SOURCE="${MADEIRA_SE_WINE_SOURCE:-$REPO_ROOT/wine}"
TOOLCHAIN="${MADEIRA_SE_LLVM_MINGW:-$REPO_ROOT/toolchains/llvm-mingw-20260421-ucrt-macos-universal}"

# Keep the old single-directory variable as a guest-tree alias. A split
# build is required because the signed ARM64 Mach-O host and the x86 PE
# payloads have different compiler/linker rules.
HOST_BUILD_DIR="${MADEIRA_SE_WINE_HOST_BUILD_DIR:-$REPO_ROOT/build/madeira-se-wine-native-host}"
GUEST_BUILD_DIR="${MADEIRA_SE_WINE_GUEST_BUILD_DIR:-${MADEIRA_SE_WINE_BUILD_DIR:-$REPO_ROOT/build/madeira-se-wine-guest}}"

if [[ $# -gt 2 ]]; then
    echo "usage: $0 [guest-build-directory] [host-build-directory]" >&2
    exit 2
fi
if [[ $# -ge 1 ]]; then
    GUEST_BUILD_DIR="$1"
fi
if [[ $# -ge 2 ]]; then
    HOST_BUILD_DIR="$2"
fi

if [[ ! -x "$WINE_SOURCE/configure" ]]; then
    echo "error: Wine configure script not found at $WINE_SOURCE/configure" >&2
    exit 1
fi
if [[ ! -d "$TOOLCHAIN/bin" ]]; then
    echo "error: llvm-mingw toolchain not found at $TOOLCHAIN" >&2
    exit 1
fi

# macOS still ships Bison 2.3. Prefer a Homebrew Bison 3.x when present.
for bison_directory in /opt/homebrew/opt/bison/bin /usr/local/opt/bison/bin; do
    if [[ -x "$bison_directory/bison" ]]; then
        PATH="$bison_directory:$PATH"
        break
    fi
done
export PATH="$TOOLCHAIN/bin:$PATH"

if ! bison_version="$(bison --version 2>/dev/null | head -n 1)" \
    || [[ ! "$bison_version" =~ \ ([3-9]|[1-9][0-9]+)\. ]]; then
    echo "error: Wine requires Bison 3.x or newer (found: ${bison_version:-none})" >&2
    exit 1
fi

for compiler in i686-w64-mingw32-clang x86_64-w64-mingw32-clang; do
    if ! command -v "$compiler" >/dev/null 2>&1; then
        echo "error: missing cross compiler: $compiler" >&2
        exit 1
    fi
done

# Catch an incomplete toolchain before Wine spends time probing hundreds of
# features. Each compiler must be able to emit a Windows COFF object.
PROBE_DIR="$(mktemp -d /tmp/madeira-se-mingw-probe.XXXXXX)"
trap 'rm -rf "$PROBE_DIR"' EXIT
cat >"$PROBE_DIR/probe.c" <<'EOF'
int madeira_se_cross_compiler_probe(void) { return 0; }
EOF
for compiler in i686-w64-mingw32-clang x86_64-w64-mingw32-clang; do
    "$compiler" -Werror -c "$PROBE_DIR/probe.c" -o "$PROBE_DIR/$compiler.o"
done

HOST_BUILD_DIR="$(mkdir -p "$HOST_BUILD_DIR" && cd "$HOST_BUILD_DIR" && pwd)"
GUEST_BUILD_DIR="$(mkdir -p "$GUEST_BUILD_DIR" && cd "$GUEST_BUILD_DIR" && pwd)"
if [[ "$HOST_BUILD_DIR" == "$GUEST_BUILD_DIR" ]]; then
    echo "error: host and guest build directories must be different" >&2
    exit 2
fi

echo "Configuring Madeira-SE native Wine host in $HOST_BUILD_DIR"
cd "$HOST_BUILD_DIR"
"$WINE_SOURCE/configure" \
    --enable-archs=none \
    --without-mingw \
    --disable-tests \
    --without-x \
    --without-wayland \
    --without-opengl \
    --without-vulkan

echo "Configuring Madeira-SE x86 guest Wine tree in $GUEST_BUILD_DIR"
cd "$GUEST_BUILD_DIR"
"$WINE_SOURCE/configure" \
    --enable-archs=i386,x86_64 \
    --with-mingw=llvm-mingw \
    --disable-tests \
    --disable-madeiracpu \
    --disable-xtajit64 \
    --disable-wow64 \
    --disable-wow64cpu \
    --disable-wow64win \
    --enable-conhost=i386,x86_64 \
    --enable-explorer=i386,x86_64 \
    --enable-plugplay=i386,x86_64 \
    --enable-rpcss=i386,x86_64 \
    --enable-services=i386,x86_64 \
    --enable-spoolsv=i386,x86_64 \
    --enable-termsv=i386,x86_64 \
    --enable-wineboot=i386,x86_64 \
    --enable-winedevice=i386,x86_64 \
    --enable-winemenubuilder=i386,x86_64 \
    --enable-wuauserv=i386,x86_64 \
    --without-x \
    --without-wayland \
    --without-opengl \
    --without-vulkan

if ! grep -Eq '^PE_ARCHS[[:space:]]*=[[:space:]]*none[[:space:]]*$' "$HOST_BUILD_DIR/Makefile"; then
    echo "error: native host tree unexpectedly has PE architectures" >&2
    exit 1
fi
if ! grep -Eq '^PE_ARCHS[[:space:]]*=([[:space:]]+)(i386[[:space:]]+x86_64|x86_64[[:space:]]+i386)[[:space:]]*$' \
    "$GUEST_BUILD_DIR/Makefile"; then
    echo "error: guest tree must contain exactly i386 and x86_64 PE architectures" >&2
    exit 1
fi

disabled_subdirs="$(grep '^DISABLED_SUBDIRS[[:space:]]*=' "$GUEST_BUILD_DIR/Makefile" || true)"
for obsolete in 'dlls/madeiracpu' 'dlls/xtajit64' 'dlls/wow64' 'dlls/wow64cpu' 'dlls/wow64win'; do
    if [[ "$disabled_subdirs" != *"$obsolete"* ]]; then
        echo "error: obsolete executable provider $obsolete is enabled in the guest tree" >&2
        exit 1
    fi
done

echo "Wine split configuration is ready."
echo "Native host: $HOST_BUILD_DIR"
echo "Guest PE tree: $GUEST_BUILD_DIR"
echo "Build with: scripts/build-wine-madeira-se-bootstrap.sh '$GUEST_BUILD_DIR' '$HOST_BUILD_DIR'"
