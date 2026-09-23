#!/bin/bash
# Run the native Remote Metal component, not the complete iOS game emulator.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STATE="$ROOT/build/DerivedData/remote-metal"
HOST_BIN="$ROOT/research/remote-metal/host/rmetald"
ACTION="${1:-start}"

case "$ACTION" in
    start|test|status|stop) ;;
    *) echo "Usage: $0 [start|test|status|stop]" >&2; exit 2 ;;
esac

host_running() {
    [ -f "$STATE/host.pid" ] || return 1
    read -r HOST_PID < "$STATE/host.pid"
    [[ "$HOST_PID" =~ ^[0-9]+$ ]] || return 1
    [ "$(ps -p "$HOST_PID" -o comm= 2>/dev/null)" = "$HOST_BIN" ]
}

if [ "$ACTION" = status ]; then
    if host_running; then
        echo "Remote Metal is running (PID $HOST_PID), 127.0.0.1:47821"
        echo "Log: $STATE/host.log"
    else
        echo "Remote Metal is stopped"
        exit 1
    fi
    exit 0
fi

if [ "$ACTION" = stop ]; then
    if host_running; then
        kill "$HOST_PID"
        for ((i=0; i<50; i++)); do
            host_running || break
            sleep 0.1
        done
        if host_running; then
            echo "Remote Metal has not exited yet (PID $HOST_PID)" >&2
            exit 1
        fi
        rm -f "$STATE/host.pid"
        echo "Stopped Remote Metal (PID $HOST_PID)"
    else
        echo "Remote Metal is already stopped"
    fi
    exit 0
fi

if [ -z "${DEVELOPER_DIR:-}" ] && [ -d /Applications/Xcode.app/Contents/Developer ]; then
    export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi
if [ ! -f "$ROOT/research/dxmt/src/winemetal/winemetal.h" ]; then
    echo "Missing DXMT sources. Run: git submodule update --init --recursive" >&2
    exit 1
fi

umask 077
mkdir -p "$STATE"
if [ ! -s "$STATE/token" ]; then
    openssl rand -hex 24 > "$STATE/token"
fi
export RMETAL_TOKEN
RMETAL_TOKEN=$(cat "$STATE/token")

if [ "$ACTION" = test ]; then
    host_running || { echo "Start the host first: $0 start" >&2; exit 1; }
    exec "$ROOT/research/remote-metal/run-tests.sh" 127.0.0.1
fi

# Rebuild before stopping our own previous process. Never kill another checkout's
# host (or an unrelated listener), and never report readiness before it listens.
xcrun --sdk macosx clang -O1 -fobjc-arc -fdeclspec -mmacosx-version-min=15.0 \
    -framework Foundation -framework Metal -framework QuartzCore -framework AppKit \
    -o "$STATE/rmetald.new" "$ROOT/research/remote-metal/host/rmetald.m"
xcrun --sdk macosx clang -O1 -o "$ROOT/research/remote-metal/guest/rmtest" \
    "$ROOT/research/remote-metal/guest/rmtest.c"
if host_running; then
    kill "$HOST_PID"
    for ((i=0; i<50; i++)); do
        host_running || break
        sleep 0.1
    done
fi
if lsof -nP -iTCP:47821 -sTCP:LISTEN >/dev/null 2>&1; then
    echo "Port 47821 is still in use; no other process was stopped." >&2
    exit 1
fi
mv "$STATE/rmetald.new" "$HOST_BIN"
# A new session keeps the window alive when a build-tool terminal exits.
HOST_PID=$(python3 - "$HOST_BIN" "$STATE/host.log" <<'PY'
import subprocess
import sys

with open(sys.argv[2], 'wb') as log:
    process = subprocess.Popen(
        [sys.argv[1], '127.0.0.1'], stdin=subprocess.DEVNULL,
        stdout=log, stderr=subprocess.STDOUT, start_new_session=True,
    )
print(process.pid)
PY
)
echo "$HOST_PID" > "$STATE/host.pid"
for ((i=0; i<50; i++)); do
    kill -0 "$HOST_PID" 2>/dev/null || { cat "$STATE/host.log" >&2; exit 1; }
    if lsof -a -p "$HOST_PID" -iTCP:47821 -sTCP:LISTEN >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done
lsof -a -p "$HOST_PID" -iTCP:47821 -sTCP:LISTEN >/dev/null 2>&1 || {
    cat "$STATE/host.log" >&2
    exit 1
}
"$ROOT/research/remote-metal/guest/rmtest" 127.0.0.1 600 > "$STATE/render.log" 2>&1 || {
    cat "$STATE/render.log" >&2
    exit 1
}
grep -q 'TRIANGLE RENDERED ON THE HOST GPU' "$STATE/render.log"
grep -q 'presented         600/600 frames' "$STATE/render.log"
echo "Remote Metal demo is running (PID $HOST_PID), 127.0.0.1:47821"
grep 'DeviceName\|presented ' "$STATE/render.log"
echo "Logs: $STATE"
echo "Run all suites: $0 test"
echo "Stop the host:  $0 stop"
