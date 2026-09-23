#!/bin/bash
# Run both architecture variants of the GUI, CoreAudio and DXMT workloads.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
HOST_DIR="${MADEIRA_SE_WINE_HOST_BUILD_DIR:-$ROOT/build/madeira-se-wine-native-host}"
GUEST_DIR="${MADEIRA_SE_WINE_GUEST_BUILD_DIR:-$ROOT/build/madeira-se-wine-guest}"
QEMU_DIR="${MADEIRA_SE_QEMU_BUILD_DIR:-$ROOT/build/madeira-se-qemu}"
CORE_DIR="${MADEIRA_SE_BUILD_DIR:-$ROOT/build/madeira-se-core}"
SMOKE_DIR="${MADEIRA_SE_GUI_SMOKE_DIR:-$ROOT/build/madeira-se-gui-smoke}"
RUNTIME_LIBRARY="${MADEIRA_SE_RUNTIME_LIBRARY:-$CORE_DIR/libmadeira_se_runtime.dylib}"
PREFIX_ROOT="${MADEIRA_SE_PREFIX_ROOT:-$ROOT/build}"
DXMT_DIR="${MADEIRA_SE_DXMT_DIR:-$ROOT/build/madeira-se-dxmt/runtime}"
SMOKE_TIMEOUT="${MADEIRA_SE_SMOKE_TIMEOUT:-180}"

cleanup_server()
{
    local prefix="$1"

    if [[ -x "$HOST_DIR/server/wineserver" ]]; then
        WINEPREFIX="$prefix" WINESERVER="$HOST_DIR/server/wineserver" \
            "$HOST_DIR/server/wineserver" -k >/dev/null 2>&1 || true
        # -k is asynchronous.  Wait for this prefix's server to finish before
        # the next Metal workload, otherwise its worker can still own the
        # previous drawable when the next test starts.
        WINEPREFIX="$prefix" WINESERVER="$HOST_DIR/server/wineserver" \
            perl -e 'alarm 10; exec @ARGV' "$HOST_DIR/server/wineserver" -w \
            >/dev/null 2>&1 || true
    fi
}

# Every workload gets a fresh prefix so a timed-out guest cannot poison the
# next architecture.  Prefixes contain a staged Wine tree (roughly 100 MiB),
# so remove them after the server has acknowledged shutdown.  Keep the
# removal in Python rather than relying on a shell rm so this script remains
# safe when PREFIX_ROOT is overridden by a caller.
remove_prefix()
{
    local prefix="$1"

    python3 - "$PREFIX_ROOT" "$prefix" <<'PY'
import pathlib
import shutil
import sys

root = pathlib.Path(sys.argv[1]).resolve()
target = pathlib.Path(sys.argv[2]).resolve()
if (target.parent != root or
        not target.name.startswith("madeira-se-prefix-") or
        target == root):
    raise SystemExit("refusing to remove unexpected Madeira-SE prefix")
if target.exists() or target.is_symlink():
    shutil.rmtree(target)
PY
}

active_prefix=""
cleanup_active_prefix()
{
    if [[ -n "$active_prefix" ]]; then
        cleanup_server "$active_prefix"
        remove_prefix "$active_prefix" || true
        active_prefix=""
    fi
}
trap cleanup_active_prefix EXIT INT TERM

runner="$CORE_DIR/madeira-se-run"
if [[ ! -x "$runner" ]]; then
    echo "error: build madeira-se-run first" >&2
    exit 1
fi

run_workload()
{
    local architecture="$1"
    local name="$2"
    local expected="$3"
    local qemu executable prefix status
    local -a runner_command

    case "$architecture" in
        i386) qemu="$QEMU_DIR/libqemu-i386-softmmu.dylib" ;;
        x86_64) qemu="$QEMU_DIR/libqemu-x86_64-softmmu.dylib" ;;
    esac
    executable="$SMOKE_DIR/$name-$architecture.exe"
    # Keep each workload isolated.  wineserver shutdown and DXMT's Metal
    # worker teardown are asynchronous, so reusing one prefix can make the
    # next process race a stale server or drawable.
    # Never reuse a prefix between runs.  A timed-out guest can leave a
    # wineserver alive for a short period, and reusing its registry/socket
    # makes the following architecture look like a graphics deadlock.
    prefix="$PREFIX_ROOT/madeira-se-prefix-$architecture-$name-$$-$(date +%s)"
    active_prefix="$prefix"
    if [[ ! -f "$executable" || ! -f "$qemu" ]]; then
        echo "error: missing $architecture $name input or QEMU backend" >&2
        exit 1
    fi
    runner_command=(
        "$runner" --host-dir "$HOST_DIR" --guest-dir "$GUEST_DIR"
        --qemu "$qemu" --runtime "$RUNTIME_LIBRARY" --prefix "$prefix"
        --dxmt-dir "$DXMT_DIR"
    )
    if [[ "$name" == gui-smoke ]]; then
        runner_command+=(--window-size 1280x720)
    elif [[ "$name" == d3d9-smoke ]]; then
        # Exercise the same virtual-mode capability shim used by legacy
        # visual novels whose configured mode is absent from macOS's list.
        runner_command+=(--d3d9-virtual-mode 1280x720 --window-size 1280x720)
    fi
    set +e
    perl -e 'alarm shift; exec @ARGV' "$SMOKE_TIMEOUT" \
        "${runner_command[@]}" "$executable"
    status=$?
    set -e
    # The timeout can terminate the loader before its prefix server notices;
    # stop only this test's server so later runs start from a clean process
    # tree without touching any other Wine prefixes.
    cleanup_server "$prefix"
    remove_prefix "$prefix"
    active_prefix=""
    # wineserver -k requests asynchronous teardown.  Give DXMT's native
    # worker a moment to release Metal objects before the next workload.
    sleep 1
    if [[ "$status" -ne "$expected" ]]; then
        echo "error: $architecture $name exited with $status (expected $expected)" >&2
        exit 1
    fi
    echo "Madeira-SE $architecture $name passed (exit $expected)."
}

for architecture in i386 x86_64; do
    run_workload "$architecture" gui-smoke 43
    run_workload "$architecture" audio-smoke 47
    run_workload "$architecture" d3d11-smoke 49
    run_workload "$architecture" d3d9-smoke 49
done
