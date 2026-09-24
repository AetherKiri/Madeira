#!/bin/bash
# Apply Madeira-SE's small embedding adapter to the pinned QEMU checkout.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
QEMU_SOURCE="${MADEIRA_SE_QEMU_SOURCE:-$REPO_ROOT/.deps/qemu-utm}"
PATCH_DIR="$REPO_ROOT/madeira-se/patches/qemu-tcti"

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
if [[ ! -f "$PATCH_DIR/0001-madeira-se-embedded-tcti-adapter.patch" ]]; then
    echo "error: Madeira-SE QEMU adapter patch is missing: $PATCH_DIR" >&2
    exit 1
fi

shopt -s nullglob
patches=("$PATCH_DIR"/*.patch)
if [[ ${#patches[@]} -eq 0 ]]; then
    echo "error: no Madeira-SE QEMU patches found: $PATCH_DIR" >&2
    exit 1
fi

for patch_file in "${patches[@]}"; do
    patch_name="$(basename "$patch_file")"
    if git -C "$QEMU_SOURCE" apply --check "$patch_file" >/dev/null 2>&1; then
        git -C "$QEMU_SOURCE" apply "$patch_file"
        echo "Applied Madeira-SE QEMU patch: $patch_name"
    elif git -C "$QEMU_SOURCE" apply --reverse --check "$patch_file" \
            >/dev/null 2>&1; then
        echo "Madeira-SE QEMU patch is already applied: $patch_name"
    elif [[ "$patch_name" == "0001-madeira-se-embedded-tcti-adapter.patch" ]] \
            && [[ -f "$QEMU_SOURCE/system/madeira-se.c" ]] \
            && grep -q "Madeira-SE embedded x86/TCTI execution adapter" \
                "$QEMU_SOURCE/system/madeira-se.c" \
            && grep -q "madeira_se_qemu_tcti_backend" \
                "$QEMU_SOURCE/system/madeira-se.c"; then
        # A later patch may have changed the adapter's new files, which makes
        # git apply's reverse check too strict. The adapter marker is only
        # accepted after the forward and reverse checks have both failed, so
        # partially applied patches still stop with an error below.
        echo "Madeira-SE QEMU embedding adapter is already applied: $patch_name"
    elif [[ "$patch_name" == "0002-madeira-se-tcti-performance.patch" ]] \
            && [[ -f "$QEMU_SOURCE/system/madeira-se-tcti-perf.c" ]] \
            && grep -q "Madeira-SE TCTI throughput counters" \
                "$QEMU_SOURCE/system/madeira-se-tcti-perf.c" \
            && grep -q "madeira_se_tcti_perf_note_translation" \
                "$QEMU_SOURCE/tcg/aarch64-tcti/tcg-target.c.inc"; then
        echo "Madeira-SE TCTI performance patch is already applied: $patch_name"
    elif [[ "$patch_name" == "0003-madeira-se-tcti-tlb-fastpath.patch" ]] \
            && grep -q "off272_i64" \
                "$QEMU_SOURCE/tcg/aarch64-tcti/tcg-target.c.inc"; then
        echo "Madeira-SE TCTI TLB fastpath patch is already applied: $patch_name"
    elif [[ "$patch_name" == "0004-madeira-se-tcti-opcode-profile.patch" ]] \
            && grep -q "madeira_se_tcti_perf_note_opcode_pair" \
                "$QEMU_SOURCE/system/madeira-se-tcti-perf.c"; then
        echo "Madeira-SE TCTI opcode profile patch is already applied: $patch_name"
    elif [[ "$patch_name" == "0005-madeira-se-tcti-memop-fastpath.patch" ]] \
            && grep -q "MADEIRA_SE_TCTI_MEMOP_PROFILE_SLOTS" \
                "$QEMU_SOURCE/system/madeira-se-tcti-perf.c" \
            && grep -q "mode5023" \
                "$QEMU_SOURCE/tcg/aarch64-tcti/tcti-gadget-gen.py"; then
        echo "Madeira-SE TCTI MemOp fastpath patch is already applied: $patch_name"
    else
        echo "error: QEMU checkout is neither pristine nor patched as expected" >&2
        echo "patch: $patch_name" >&2
        echo "checkout: $QEMU_SOURCE" >&2
        exit 1
    fi
done
