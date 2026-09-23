#!/bin/sh
# Rebuild AND run every suite. Stale test binaries speak an old protocol and
# get refused by a newer daemon, which reads as a product failure ("newLibrary
# -> err", "auth failed") when it is only a rebuild that did not happen.
set -e
cd "$(dirname "$0")"
HOST="${1:-127.0.0.1}"
: "${RMETAL_TOKEN:?set RMETAL_TOKEN}"
if [ -z "${DEVELOPER_DIR:-}" ] && [ -d /Applications/Xcode.app/Contents/Developer ]; then
    export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi
OUT=$(mktemp -d "${TMPDIR:-/tmp}/madeira-metal-tests.XXXXXX")
trap 'rm -rf "$OUT"' EXIT

# Compile fixtures locally: the original suites depended on metallibs in the
# author's /tmp directory and silently skipped the shipping shader path.
xcrun --sdk macosx metal -std=metal3.1 -mmacosx-version-min=15.0 \
    -c schema/test.metal -o "$OUT/test.air"
xcrun --sdk macosx metallib "$OUT/test.air" -o "$OUT/test.metallib"

# Capture each command before summarising it. A failing test piped into tail
# previously returned tail's successful status, so set -e did not stop the run.
run_suite() {
    suite=$1
    shift
    if "$@" > "$OUT/$suite.log" 2>&1; then
        printf '  %s: ' "$suite"
        tail -1 "$OUT/$suite.log"
    else
        cat "$OUT/$suite.log" >&2
        return 1
    fi
}

echo "  protocol: v$(sed -n 's/^#define RM_VERSION \([0-9]*\)u.*/\1/p' protocol.h)"
clang -O1 -w -I. -o "$OUT/wire_test" schema/wire_test.c
run_suite wire "$OUT/wire_test"
clang -O1 -w -fdeclspec -I ../dxmt/src/winemetal -o "$OUT/pack_test" schema/pack_test.c
run_suite pack "$OUT/pack_test"
clang -O1 -w -o "$OUT/rmclient_test" guest/rmclient_test.c
run_suite client env DXMT_REMOTE_METAL="$HOST" "$OUT/rmclient_test" "$OUT/test.metallib"
clang -O1 -w -I. -o "$OUT/rmtest" guest/rmtest.c
run_suite rmtest "$OUT/rmtest" "$HOST" 600
grep -q 'TRIANGLE RENDERED ON THE HOST GPU' "$OUT/rmtest.log"
grep -q 'presented         600/600 frames' "$OUT/rmtest.log"
grep 'presented ' "$OUT/rmtest.log"
clang -O1 -w -I. -o "$OUT/rmreplay" guest/rmreplay.c
run_suite replay "$OUT/rmreplay" "$HOST" "$OUT/test.metallib"
