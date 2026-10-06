#!/usr/bin/env bash
# Stage the already-built QEMU TCTI shared libraries into the app resource
# tree. Release/iOS builds should provide a target-SDK build directory; this
# script does not silently copy a macOS library into an iOS archive.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_BUILD="${MADEIRA_SE_QEMU_BUILD_DIR:-$REPO_ROOT/build/madeira-se-qemu}"
DEST_DIR="${MADEIRA_SE_QEMU_DEST:-$REPO_ROOT/app/Madeira/qemu}"
ALLOW_MACOS="${MADEIRA_SE_ALLOW_MACOS_QEMU:-0}"

if [[ $# -gt 2 ]]; then
    echo "usage: $0 [qemu-build-directory] [destination-directory]" >&2
    exit 2
fi
if [[ $# -ge 1 ]]; then SOURCE_BUILD="$1"; fi
if [[ $# -ge 2 ]]; then DEST_DIR="$2"; fi

mkdir -p "$DEST_DIR"
for architecture in i386 x86_64; do
    source="$SOURCE_BUILD/libqemu-$architecture-softmmu.dylib"
    destination="$DEST_DIR/libqemu-$architecture-softmmu.dylib"
    [[ -f "$source" ]] || { echo "error: missing $source" >&2; exit 1; }
    description="$(file "$source")"
    if [[ "$description" == *"Mach-O 64-bit dynamically linked shared library arm64"* ]]; then
        if [[ "$ALLOW_MACOS" != "1" ]]; then
            # LC_BUILD_VERSION distinguishes the host SDK from an iOS SDK;
            # don't make a host smoke artifact look App Store compatible.
            if otool -l "$source" | grep -qE '^[[:space:]]*platform 1$'; then
                echo "error: $source is a macOS library; set MADEIRA_SE_ALLOW_MACOS_QEMU=1 only for host smoke" >&2
                exit 1
            fi
        fi
    else
        echo "error: unexpected QEMU artifact: $description" >&2
        exit 1
    fi
    cp "$source" "$destination"
    echo "staged $architecture QEMU TCTI: $destination"
done
