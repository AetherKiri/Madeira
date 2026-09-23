#!/bin/bash
# Apply Madeira-SE's small embedding adapter to the pinned QEMU checkout.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
QEMU_SOURCE="${MADEIRA_SE_QEMU_SOURCE:-$REPO_ROOT/.deps/qemu-utm}"
PATCH_FILE="$REPO_ROOT/madeira-se/patches/qemu-tcti/0001-madeira-se-embedded-tcti-adapter.patch"

if [[ $# -gt 1 ]]; then
    echo "usage: $0 [qemu-source]" >&2
    exit 2
fi
if [[ $# -eq 1 ]]; then
    QEMU_SOURCE="$1"
fi
if ! git -C "$QEMU_SOURCE" rev-parse --is-inside-work-tree \
        >/dev/null 2>&1; then
    echo "error: QEMU source is not a Git checkout: $QEMU_SOURCE" >&2
    exit 1
fi
if [[ ! -f "$PATCH_FILE" ]]; then
    echo "error: Madeira-SE QEMU patch is missing: $PATCH_FILE" >&2
    exit 1
fi

if git -C "$QEMU_SOURCE" apply --check "$PATCH_FILE" >/dev/null 2>&1; then
    git -C "$QEMU_SOURCE" apply "$PATCH_FILE"
    echo "Applied Madeira-SE QEMU embedding adapter."
elif git -C "$QEMU_SOURCE" apply --reverse --check "$PATCH_FILE" \
        >/dev/null 2>&1; then
    echo "Madeira-SE QEMU embedding adapter is already applied."
else
    echo "error: QEMU checkout is neither pristine nor patched as expected" >&2
    echo "checkout: $QEMU_SOURCE" >&2
    exit 1
fi
