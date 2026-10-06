#!/usr/bin/env bash
# Launch a Windows PE32/PE32+ program through the standalone macOS path.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

NO_BUILD=0
DURATION="${MADEIRA_SE_MACOS_TEST_DURATION:-20}"
BACKEND="${MADEIRA_SE_MACOS_TEST_BACKEND:-dxmt}"
WINDOW_SIZE="${MADEIRA_SE_MACOS_TEST_WINDOW_SIZE:-1280x720}"
REQUIRE_D3D9=0
PREFIX=""
EXE=""

usage() {
    cat >&2 <<'EOF'
usage: test-madeira-se-macos.sh [options] executable [arguments...]

Options:
  --no-build                 use already-built macOS artifacts
  --duration SECONDS         bounded run duration (default: 20)
  --backend dxmt|wined3d    D3D9 backend (default: dxmt)
  --window-size WxH          initial Wine window size (default: 1280x720)
  --require-d3d9             require a D3D9 device/backbuffer log line
  --prefix DIR               reuse this Wine prefix instead of a temporary one
  -h, --help                 show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build) NO_BUILD=1; shift ;;
        --duration)
            [[ $# -ge 2 ]] || { echo "error: --duration needs a value" >&2; usage; exit 2; }
            DURATION="$2"; shift 2 ;;
        --backend)
            [[ $# -ge 2 ]] || { echo "error: --backend needs a value" >&2; usage; exit 2; }
            BACKEND="$2"; shift 2 ;;
        --window-size)
            [[ $# -ge 2 ]] || { echo "error: --window-size needs a value" >&2; usage; exit 2; }
            WINDOW_SIZE="$2"; shift 2 ;;
        --require-d3d9) REQUIRE_D3D9=1; shift ;;
        --prefix)
            [[ $# -ge 2 ]] || { echo "error: --prefix needs a value" >&2; usage; exit 2; }
            PREFIX="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        --) shift; break ;;
        -*) echo "error: unknown option: $1" >&2; usage; exit 2 ;;
        *) EXE="$1"; shift; break ;;
    esac
done

if [[ -z "$EXE" || ! -f "$EXE" ]]; then
    echo "error: executable is required" >&2
    usage
    exit 2
fi
if [[ "$(uname -s)" != Darwin || "$(uname -m)" != arm64 ]]; then
    echo "error: this test requires an Apple Silicon macOS host" >&2
    exit 1
fi
if ! [[ "$DURATION" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: --duration must be a positive integer" >&2
    exit 2
fi
case "$BACKEND" in
    dxmt|wined3d) ;;
    *) echo "error: --backend must be dxmt or wined3d" >&2; exit 2 ;;
esac

if [[ "$NO_BUILD" == 0 ]]; then
    "$SCRIPT_DIR/build-madeira-se-macos.sh"
fi

CORE_BUILD_DIR="${MADEIRA_SE_CORE_BUILD_DIR:-$REPO_ROOT/build/madeira-se-core}"
QEMU_BUILD_DIR="${MADEIRA_SE_QEMU_BUILD_DIR:-$REPO_ROOT/build/madeira-se-qemu}"
WINE_HOST_BUILD_DIR="${MADEIRA_SE_WINE_HOST_BUILD_DIR:-$REPO_ROOT/build/madeira-se-wine-native-host}"
WINE_GUEST_BUILD_DIR="${MADEIRA_SE_WINE_GUEST_BUILD_DIR:-$REPO_ROOT/build/madeira-se-wine-guest}"
DXMT_RUNTIME_DIR="${MADEIRA_SE_DXMT_RUNTIME_DIR:-$REPO_ROOT/build/madeira-se-dxmt/runtime}"
RUNTIME_LIBRARY="${MADEIRA_SE_RUNTIME_LIBRARY:-$CORE_BUILD_DIR/libmadeira_se_runtime.0.1.0.dylib}"
RUNNER="$CORE_BUILD_DIR/madeira-se-run"

if file -b "$EXE" | grep -q 'PE32+'; then
    QEMU_LIBRARY="$QEMU_BUILD_DIR/libqemu-x86_64-softmmu.dylib"
    GUEST_ARCH=x86_64
elif file -b "$EXE" | grep -q 'PE32 executable'; then
    QEMU_LIBRARY="$QEMU_BUILD_DIR/libqemu-i386-softmmu.dylib"
    GUEST_ARCH=i386
else
    echo "error: not a supported PE32/PE32+ executable: $(file -b "$EXE")" >&2
    exit 1
fi

for artifact in "$RUNNER" "$RUNTIME_LIBRARY" "$QEMU_LIBRARY" \
    "$WINE_HOST_BUILD_DIR/loader/wine" "$WINE_HOST_BUILD_DIR/server/wineserver" \
    "$WINE_GUEST_BUILD_DIR/dlls/ntdll/$GUEST_ARCH-windows/ntdll.dll"; do
    if [[ ! -f "$artifact" ]]; then
        echo "error: missing runtime artifact: $artifact" >&2
        echo "run scripts/build-madeira-se-macos.sh first" >&2
        exit 1
    fi
done

EXE="$(cd "$(dirname "$EXE")" && pwd)/$(basename "$EXE")"
WORKDIR="${MADEIRA_SE_MACOS_TEST_WORKDIR:-$(dirname "$EXE")}"
LOG_DIR="${MADEIRA_SE_MACOS_TEST_LOG_DIR:-$REPO_ROOT/build/madeira-se-macos}"
mkdir -p "$LOG_DIR"
LOG_FILE="${MADEIRA_SE_MACOS_TEST_LOG:-$LOG_DIR/$(basename "$EXE").$GUEST_ARCH.log}"

TEMP_PREFIX=0
if [[ -z "$PREFIX" ]]; then
    PREFIX="$(mktemp -d "$LOG_DIR/prefix.$GUEST_ARCH.XXXXXX")"
    TEMP_PREFIX=1
else
    mkdir -p "$PREFIX"
fi
cleanup() {
    if [[ "$TEMP_PREFIX" == 1 && "${MADEIRA_SE_MACOS_KEEP_PREFIX:-0}" != 1 ]]; then
        rm -rf "$PREFIX"
    fi
}
trap cleanup EXIT

echo "Launching $EXE"
echo "guest=$GUEST_ARCH backend=$BACKEND duration=${DURATION}s"
echo "log=$LOG_FILE"

set +e
WINEDEBUG="${WINEDEBUG:-+err,+dxmt}" \
MADEIRA_SE_CPU_RUN_BUDGET="${MADEIRA_SE_CPU_RUN_BUDGET:-8000000}" \
MADEIRA_SE_CPU_BATCH_SLICES="${MADEIRA_SE_CPU_BATCH_SLICES:-1}" \
perl -e 'alarm shift; exec @ARGV' "$DURATION" \
    "$RUNNER" \
    --host-dir "$WINE_HOST_BUILD_DIR" \
    --guest-dir "$WINE_GUEST_BUILD_DIR" \
    --qemu "$QEMU_LIBRARY" \
    --runtime "$RUNTIME_LIBRARY" \
    --prefix "$PREFIX" \
    --dxmt-dir "$DXMT_RUNTIME_DIR" \
    --workdir "$WORKDIR" \
    --window-size "$WINDOW_SIZE" \
    --d3d9-backend "$BACKEND" \
    "$EXE" "$@" >"$LOG_FILE" 2>&1
STATUS=$?
set -e

if [[ "$STATUS" != 0 && "$STATUS" != 142 ]]; then
    echo "error: Madeira-SE exited with status $STATUS" >&2
    tail -n 160 "$LOG_FILE" >&2 || true
    exit "$STATUS"
fi
if grep -q 'Failed to create metal view' "$LOG_FILE"; then
    echo "error: DXMT failed to create the macOS Metal view" >&2
    tail -n 160 "$LOG_FILE" >&2
    exit 1
fi
if [[ "$REQUIRE_D3D9" == 1 ]] && ! grep -Eq 'D3D9Device: backbuffer|D3D9Device: created successfully|Direct3DCreate9 called' "$LOG_FILE"; then
    echo "error: the run timed out before a D3D9 device/backbuffer was created" >&2
    tail -n 160 "$LOG_FILE" >&2
    exit 1
fi

echo "Madeira-SE macOS launch passed (status=$STATUS; timeout 142 is expected)."
if grep -Eq 'D3D9Device: backbuffer|D3D9Device: created successfully|Direct3DCreate9 called' "$LOG_FILE"; then
    grep -E 'D3D9Device: backbuffer|D3D9Device: created successfully|Direct3DCreate9 called' "$LOG_FILE" | tail -n 5
else
    echo "No D3D9 device line was observed; the title may use another graphics API."
fi
echo "full log: $LOG_FILE"
