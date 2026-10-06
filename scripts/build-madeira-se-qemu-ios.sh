#!/usr/bin/env bash
# Reproducibly build and stage the App Store QEMU TCTI libraries.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SDK_NAME="${MADEIRA_SE_QEMU_SDK:-iphoneos}"
ARCH="${MADEIRA_SE_QEMU_ARCH:-arm64}"
JOBS="${MADEIRA_SE_BUILD_JOBS:-2}"
GLIB_BUILD_DIR="${MADEIRA_SE_GLIB_BUILD_DIR:-$REPO_ROOT/build/madeira-se-glib-ios}"
GLIB_PREFIX="${MADEIRA_SE_GLIB_PREFIX:-$REPO_ROOT/build/madeira-se-glib-ios-prefix}"
QEMU_BUILD_DIR="${MADEIRA_SE_QEMU_BUILD_DIR:-$REPO_ROOT/build/madeira-se-qemu-ios}"
QEMU_PKG_CONFIG_PATH="${MADEIRA_SE_QEMU_PKG_CONFIG_PATH:-$GLIB_PREFIX/lib/pkgconfig}"

if [[ "$SDK_NAME" != iphoneos ]]; then
    echo "error: this packaging wrapper currently targets iphoneos" >&2
    exit 2
fi
if [[ -z "${DEVELOPER_DIR:-}" && -d /Applications/Xcode.app/Contents/Developer ]]; then
    export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi

MADEIRA_SE_SDK="$SDK_NAME" \
MADEIRA_SE_ARCH="$ARCH" \
MADEIRA_SE_BUILD_JOBS="$JOBS" \
MADEIRA_SE_GLIB_BUILD_DIR="$GLIB_BUILD_DIR" \
MADEIRA_SE_GLIB_PREFIX="$GLIB_PREFIX" \
    "$SCRIPT_DIR/build-madeira-se-glib.sh"

MADEIRA_SE_QEMU_SDK="$SDK_NAME" \
MADEIRA_SE_QEMU_ARCH="$ARCH" \
MADEIRA_SE_BUILD_JOBS="$JOBS" \
MADEIRA_SE_QEMU_BUILD_DIR="$QEMU_BUILD_DIR" \
MADEIRA_SE_QEMU_PKG_CONFIG_PATH="$QEMU_PKG_CONFIG_PATH" \
    "$SCRIPT_DIR/configure-qemu-madeira-se.sh" "$QEMU_BUILD_DIR"

MADEIRA_SE_QEMU_SDK="$SDK_NAME" \
MADEIRA_SE_BUILD_JOBS="$JOBS" \
MADEIRA_SE_QEMU_BUILD_DIR="$QEMU_BUILD_DIR" \
    "$SCRIPT_DIR/build-qemu-madeira-se-smoke.sh" "$QEMU_BUILD_DIR"

MADEIRA_SE_QEMU_BUILD_DIR="$QEMU_BUILD_DIR" \
    "$SCRIPT_DIR/stage-madeira-se-qemu.sh" "$QEMU_BUILD_DIR" \
        "${MADEIRA_SE_QEMU_DEST:-$REPO_ROOT/app/Madeira/qemu}"

echo "Madeira-SE iOS QEMU TCTI staged in ${MADEIRA_SE_QEMU_DEST:-$REPO_ROOT/app/Madeira/qemu}"
