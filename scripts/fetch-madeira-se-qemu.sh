#!/bin/bash
# Fetch the exact UTM QEMU/TCTI source pinned by Madeira-SE.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOCK_FILE="$REPO_ROOT/madeira-se/deps/qemu-tcti.lock"
DESTINATION="${MADEIRA_SE_QEMU_SOURCE:-$REPO_ROOT/.deps/qemu-utm}"
PATCH_SCRIPT="$REPO_ROOT/scripts/apply-qemu-madeira-se-patch.sh"
PATCH_FILE="$REPO_ROOT/madeira-se/patches/qemu-tcti/0001-madeira-se-embedded-tcti-adapter.patch"

if [[ $# -gt 1 ]]; then
    echo "usage: $0 [destination]" >&2
    exit 2
fi
if [[ $# -eq 1 ]]; then
    DESTINATION="$1"
fi
if [[ ! -f "$LOCK_FILE" ]]; then
    echo "error: dependency lock is missing: $LOCK_FILE" >&2
    exit 1
fi

# The lock file is repository-owned and contains only these three assignments.
# shellcheck source=/dev/null
source "$LOCK_FILE"
: "${QEMU_REPOSITORY:?missing QEMU_REPOSITORY}"
: "${QEMU_BRANCH:?missing QEMU_BRANCH}"
: "${QEMU_COMMIT:?missing QEMU_COMMIT}"

if [[ -e "$DESTINATION" && ! -d "$DESTINATION/.git" ]]; then
    echo "error: destination exists and is not a Git checkout: $DESTINATION" >&2
    exit 1
fi

if [[ ! -d "$DESTINATION/.git" ]]; then
    mkdir -p "$(dirname "$DESTINATION")"
    git clone --filter=blob:none --no-checkout "$QEMU_REPOSITORY" "$DESTINATION"
fi

actual_remote="$(git -C "$DESTINATION" remote get-url origin)"
if [[ "$actual_remote" != "$QEMU_REPOSITORY" ]]; then
    echo "error: QEMU checkout has unexpected origin: $actual_remote" >&2
    exit 1
fi
if [[ -n "$(git -C "$DESTINATION" status --porcelain)" ]]; then
    if git -C "$DESTINATION" apply --reverse --check "$PATCH_FILE" \
            >/dev/null 2>&1; then
        git -C "$DESTINATION" apply --reverse "$PATCH_FILE"
    fi
    if [[ -n "$(git -C "$DESTINATION" status --porcelain)" ]]; then
        echo "error: QEMU checkout has unexpected local changes: $DESTINATION" >&2
        exit 1
    fi
fi

git -C "$DESTINATION" fetch --depth 1 origin "$QEMU_COMMIT"
git -C "$DESTINATION" checkout --detach "$QEMU_COMMIT"

if [[ ! -f "$DESTINATION/tcg/aarch64-tcti/tcg-target.c.inc" ]]; then
    echo "error: pinned QEMU revision does not contain aarch64-tcti" >&2
    exit 1
fi
if ! grep -q "TCG_TARGET_INTERPRETER" \
    "$DESTINATION/tcg/aarch64-tcti/tcg-target.h"; then
    echo "error: pinned TCTI backend does not declare interpreter mode" >&2
    exit 1
fi

MADEIRA_SE_QEMU_SOURCE="$DESTINATION" "$PATCH_SCRIPT"

echo "QEMU TCTI source ready: $DESTINATION"
echo "revision: $(git -C "$DESTINATION" rev-parse HEAD)"
