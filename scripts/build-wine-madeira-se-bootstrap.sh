#!/bin/bash
# Build and verify the split Wine bootstrap used by Madeira-SE.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
GUEST_BUILD_DIR="${MADEIRA_SE_WINE_GUEST_BUILD_DIR:-${MADEIRA_SE_WINE_BUILD_DIR:-$REPO_ROOT/build/madeira-se-wine-guest}}"
HOST_BUILD_DIR="${MADEIRA_SE_WINE_HOST_BUILD_DIR:-$REPO_ROOT/build/madeira-se-wine-native-host}"
TOOLCHAIN="${MADEIRA_SE_LLVM_MINGW:-$REPO_ROOT/toolchains/llvm-mingw-20260421-ucrt-macos-universal}"
BUILD_JOBS="${MADEIRA_SE_BUILD_JOBS:-2}"

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
if [[ ! -f "$HOST_BUILD_DIR/Makefile" || ! -f "$GUEST_BUILD_DIR/Makefile" ]]; then
    echo "error: both Wine trees must be configured first" >&2
    echo "run scripts/configure-wine-madeira-se.sh '$GUEST_BUILD_DIR' '$HOST_BUILD_DIR'" >&2
    exit 1
fi
if [[ ! "$BUILD_JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: MADEIRA_SE_BUILD_JOBS must be a positive integer" >&2
    exit 2
fi

for bison_directory in /opt/homebrew/opt/bison/bin /usr/local/opt/bison/bin; do
    if [[ -x "$bison_directory/bison" ]]; then
        PATH="$bison_directory:$PATH"
        break
    fi
done
export PATH="$TOOLCHAIN/bin:$PATH"

READOBJ="$TOOLCHAIN/bin/llvm-readobj"
if [[ ! -x "$READOBJ" ]]; then
    echo "error: llvm-readobj is required from $TOOLCHAIN" >&2
    exit 1
fi

# The native tree contains the Unix side of every Wine DLL.  Building only
# loader/wineserver/ntdll is enough for the bootstrap process, but it leaves
# ordinary guest imports such as ws2_32 without a host Unix library.  Build
# the complete native tree so an arbitrary visual novel can select its normal
# Wine providers at runtime.
host_targets=()
guest_targets=()
for architecture in i386 x86_64; do
    guest_targets+=(
        "dlls/apisetschema/${architecture}-windows/apisetschema.dll"
        "dlls/ntdll/${architecture}-windows/ntdll.dll"
        "dlls/kernelbase/${architecture}-windows/kernelbase.dll"
        "dlls/kernel32/${architecture}-windows/kernel32.dll"
        "dlls/win32u/${architecture}-windows/win32u.dll"
        "programs/wineboot/${architecture}-windows/wineboot.exe"
    )
done

HOST_LOG="${MADEIRA_SE_WINE_HOST_BUILD_LOG:-$REPO_ROOT/build/madeira-se-wine-host-bootstrap.log}"
GUEST_LOG="${MADEIRA_SE_WINE_GUEST_BUILD_LOG:-$REPO_ROOT/build/madeira-se-wine-guest-bootstrap.log}"

echo "Building native ARM64 Mach-O Wine host with -j$BUILD_JOBS"
if ! make -C "$HOST_BUILD_DIR" -j"$BUILD_JOBS" >"$HOST_LOG" 2>&1; then
    echo "error: native host build failed; tail of $HOST_LOG:" >&2
    tail -n 200 "$HOST_LOG" >&2
    exit 1
fi

echo "Building i386/x86-64 guest bootstrap with -j$BUILD_JOBS"
if ! make -C "$GUEST_BUILD_DIR" -j"$BUILD_JOBS" "${guest_targets[@]}" >"$GUEST_LOG" 2>&1; then
    echo "error: guest bootstrap build failed; tail of $GUEST_LOG:" >&2
    tail -n 200 "$GUEST_LOG" >&2
    exit 1
fi

# The native wineserver and loader still consume Wine's data files even though
# all executable PE modules come from the guest tree.  The arch-none host
# configure does not emit these files, so stage the canonical guest copies
# beside the host binaries.
rm -rf "$HOST_BUILD_DIR/nls"
mkdir -p "$HOST_BUILD_DIR/nls"
cp -R "$GUEST_BUILD_DIR/nls/." "$HOST_BUILD_DIR/nls/"
cp "$GUEST_BUILD_DIR/loader/wine.inf" "$HOST_BUILD_DIR/loader/wine.inf"

check_machine()
{
    local relative_path="$1"
    local expected_machine="$2"
    local artifact="$GUEST_BUILD_DIR/$relative_path"
    local headers

    if [[ ! -f "$artifact" ]]; then
        echo "error: missing build artifact: $artifact" >&2
        return 1
    fi
    headers="$("$READOBJ" --file-headers "$artifact")"
    if ! grep -q "Machine: $expected_machine" <<<"$headers"; then
        echo "error: $relative_path is not $expected_machine" >&2
        return 1
    fi
    echo "verified $relative_path ($expected_machine)"
}

if ! file "$HOST_BUILD_DIR/loader/wine" | grep -Eq 'Mach-O.*arm64'; then
    echo "error: native loader/wine is not an ARM64 Mach-O executable" >&2
    exit 1
fi
if ! file "$HOST_BUILD_DIR/server/wineserver" | grep -Eq 'Mach-O.*arm64'; then
    echo "error: native wineserver is not an ARM64 Mach-O executable" >&2
    exit 1
fi
if ! file "$HOST_BUILD_DIR/dlls/ntdll/ntdll.so" | grep -Eq 'Mach-O.*arm64'; then
    echo "error: native ntdll.so is not an ARM64 Mach-O library" >&2
    exit 1
fi

for architecture in i386 x86_64; do
    if [[ "$architecture" == i386 ]]; then
        machine=IMAGE_FILE_MACHINE_I386
    else
        machine=IMAGE_FILE_MACHINE_AMD64
    fi
    check_machine "dlls/apisetschema/${architecture}-windows/apisetschema.dll" "$machine"
    check_machine "dlls/ntdll/${architecture}-windows/ntdll.dll" "$machine"
    check_machine "dlls/kernelbase/${architecture}-windows/kernelbase.dll" "$machine"
    check_machine "dlls/kernel32/${architecture}-windows/kernel32.dll" "$machine"
    check_machine "programs/wineboot/${architecture}-windows/wineboot.exe" "$machine"
done

if grep -Eq '^PE_ARCHS[[:space:]]*=.*(aarch64|arm64ec)' "$GUEST_BUILD_DIR/Makefile"; then
    echo "error: guest Makefile still exposes ARM64 PE architectures" >&2
    exit 1
fi
for artifact in "$GUEST_BUILD_DIR"/dlls/madeiracpu/* "$GUEST_BUILD_DIR"/dlls/xtajit64/*; do
    if [[ -e "$artifact" ]]; then
        echo "error: obsolete executable provider artifact exists: $artifact" >&2
        exit 1
    fi
done

echo "Wine bootstrap verification passed."
echo "Native host: $HOST_BUILD_DIR"
echo "Guest PE tree: $GUEST_BUILD_DIR"
