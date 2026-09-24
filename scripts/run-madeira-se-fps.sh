#!/bin/bash
# Measure a title's presentation cadence through the standalone runtime.
# Wine's wglSwapBuffers samples are preferred; DXMT's native Present counter
# is used for the D3D9/Metal path when the title does not go through WGL.

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

runtime_root="${MADEIRA_SE_RUNTIME_ROOT:-$ROOT/build/madeira-se-runtime}"
executable="${MADEIRA_SE_FPS_EXE:-$ROOT/../A7-3/cs2.exe}"
duration="${MADEIRA_SE_FPS_DURATION:-15}"
warmup="${MADEIRA_SE_FPS_WARMUP:-3}"
budget="${MADEIRA_SE_CPU_RUN_BUDGET:-8000000}"
reuse_slices="${MADEIRA_SE_CPU_REUSE_SLICES:-0}"
csmt="${MADEIRA_SE_FPS_CSMT:-0}"
vsync="${MADEIRA_SE_FPS_VSYNC:-1}"
fps_cap="${MADEIRA_SE_FPS_CAP:-30}"
virtual_mode="${MADEIRA_SE_D3D9_VIRTUAL_MODE:-1280x720}"
window_size="${MADEIRA_SE_WINDOW_SIZE:-}"
d3d9_backend="${MADEIRA_SE_D3D9_BACKEND:-dxmt}"
output_root="${MADEIRA_SE_FPS_OUTPUT_DIR:-$ROOT/build/fps}"

usage()
{
    cat <<'EOF'
Usage: scripts/run-madeira-se-fps.sh [options]
  --exe PATH                 Windows executable (default: ../A7-3/cs2.exe)
  --runtime-root DIR         staged runtime (default: build/madeira-se-runtime)
  --duration SECONDS         total run time (default: 15)
  --warmup SECONDS           samples to exclude (default: 3)
  --budget INSTRUCTIONS      TCTI run budget (default: 8000000)
  --csmt 0|1                 Wine command stream mode (default: 0)
  --vsync 0|1                swap interval (default: 1; 0 measures raw throughput)
  --fps-cap FPS              presentation cap, 0 disables (default: 30)
  --window-size WxH          host client area (default: D3D9 virtual mode)
  --d3d9-virtual-mode WxH    virtual adapter mode (default: 1280x720)
  --d3d9-backend NAME        D3D9 path: auto, dxmt or wined3d (default: dxmt)
  --output-dir DIR           result root (default: build/fps)
  -h, --help                 show this help
EOF
}

while [[ "$#" -gt 0 ]]; do
    case "$1" in
        --exe|--runtime-root|--duration|--warmup|--budget|--csmt|--vsync|--fps-cap|--window-size|--d3d9-virtual-mode|--d3d9-backend|--output-dir)
            [[ "$#" -ge 2 ]] || { usage >&2; exit 2; }
            case "$1" in
                --exe) executable="$2" ;;
                --runtime-root) runtime_root="$2" ;;
                --duration) duration="$2" ;;
                --warmup) warmup="$2" ;;
                --budget) budget="$2" ;;
                --csmt) csmt="$2" ;;
                --vsync) vsync="$2" ;;
                --fps-cap) fps_cap="$2" ;;
                --window-size) window_size="$2" ;;
                --d3d9-virtual-mode) virtual_mode="$2" ;;
                --d3d9-backend) d3d9_backend="$2" ;;
                --output-dir) output_root="$2" ;;
            esac
            shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -z "$window_size" ]]; then window_size="$virtual_mode"; fi

case "$duration" in ''|*[!0-9]*) echo "error: duration must be an integer" >&2; exit 2 ;; esac
case "$warmup" in ''|*[!0-9]*) echo "error: warmup must be an integer" >&2; exit 2 ;; esac
if [[ "$duration" -le 0 || "$warmup" -ge "$duration" ]]; then
    echo "error: duration must be positive and greater than warmup" >&2
    exit 2
fi
if [[ "$csmt" != 0 && "$csmt" != 1 ]]; then
    echo "error: --csmt must be 0 or 1" >&2
    exit 2
fi
if [[ "$vsync" != 0 && "$vsync" != 1 ]]; then
    echo "error: --vsync must be 0 or 1" >&2
    exit 2
fi
case "$fps_cap" in
    ''|*[!0-9]*) echo "error: --fps-cap must be an integer from 0 to 240" >&2; exit 2 ;;
esac
if [[ "$fps_cap" -gt 240 ]]; then
    echo "error: --fps-cap must be an integer from 0 to 240" >&2
    exit 2
fi
case "$d3d9_backend" in
    auto|dxmt|wined3d) ;;
    *)
        echo "error: --d3d9-backend must be auto, dxmt or wined3d" >&2
        exit 2
        ;;
esac

for required in \
    "$runtime_root/madeira-se-run" "$runtime_root/host-arm64" \
    "$runtime_root/guest" "$runtime_root/madeira-se-runtime.dylib" \
    "$runtime_root/dxmt" "$executable"; do
    if [[ ! -e "$required" ]]; then
        echo "error: missing $required" >&2
        exit 1
    fi
done

# The launcher changes to --workdir before exec'ing Wine.  Resolve the guest
# image before that chdir so a relative --exe remains valid inside the child.
executable="$(cd "$(dirname "$executable")" && pwd)/$(basename "$executable")"

if file "$executable" | grep -q 'PE32+'; then architecture="x86_64"; else architecture="i386"; fi
qemu="$runtime_root/qemu/libqemu-$architecture-softmmu.dylib"
if [[ ! -f "$qemu" ]]; then echo "error: missing QEMU backend $qemu" >&2; exit 1; fi

timestamp="$(date +%Y%m%d-%H%M%S)"
result_dir="$output_root/$timestamp-$$"
mkdir -p "$result_dir"
run_log="$result_dir/run.log"
fps_log="$result_dir/fps.log"
present_log="$result_dir/present.log"
metrics_csv="$result_dir/metrics.csv"
sample_log="$result_dir/sample.txt"
summary_log="$result_dir/summary.txt"
prefix="$result_dir/prefix"

wine_debug="${WINEDEBUG:-}"
if [[ -n "$wine_debug" ]]; then wine_debug="$wine_debug,+fps"; else wine_debug="+fps"; fi
echo "Madeira-SE FPS test"
echo "  executable: $executable"
echo "  architecture: $architecture"
echo "  runtime: $runtime_root"
echo "  duration: ${duration}s (warmup ${warmup}s)"
echo "  budget: $budget"
echo "  context reuse slices: $reuse_slices"
echo "  csmt: $csmt"
echo "  vsync: $vsync"
echo "  fps cap: $fps_cap"
echo "  d3d9 backend: $d3d9_backend"
echo "  result: $result_dir"

set +e
env \
    MADEIRA_SE_CPU_RUN_BUDGET="$budget" MADEIRA_SE_CPU_REUSE_SLICES="$reuse_slices" \
    MADEIRA_SE_CPU_STATS="${MADEIRA_SE_CPU_STATS:-1}" WINE_D3D_CONFIG="csmt=$csmt" \
    MADEIRA_SE_FPS_VSYNC="$vsync" MADEIRA_SE_FPS_CAP="$fps_cap" \
    WINEDEBUG="$wine_debug" MADEIRA_SE_WINDOW_SIZE="$window_size" \
    "$runtime_root/madeira-se-run" \
    --host-dir "$runtime_root/host-arm64" --guest-dir "$runtime_root/guest" \
    --qemu "$qemu" --runtime "$runtime_root/madeira-se-runtime.dylib" \
    --prefix "$prefix" --dxmt-dir "$runtime_root/dxmt" \
    --workdir "$(cd "$(dirname "$executable")" && pwd)" \
    --d3d9-backend "$d3d9_backend" \
    --d3d9-virtual-mode "$virtual_mode" --window-size "$window_size" \
    --fps-cap "$fps_cap" "$executable" >"$run_log" 2>&1 &
runner_pid=$!

printf 'wall_s,cpu_percent,rss_kb\n' >"$metrics_csv"
sample_metrics()
{
    local now cpu rss
    now="$(python3 -c 'import time; print(f"{time.monotonic():.3f}")')"
    cpu="0.0"
    rss="0"
    if ps_line="$(ps -p "$runner_pid" -o %cpu= -o rss= 2>/dev/null)" && [[ -n "$ps_line" ]]; then
        read -r cpu rss <<<"$ps_line"
    fi
    printf '%s,%s,%s\n' "$now" "${cpu:-0.0}" "${rss:-0}" >>"$metrics_csv"
}

deadline=$((SECONDS + duration))
while kill -0 "$runner_pid" 2>/dev/null && [[ "$SECONDS" -lt "$deadline" ]]; do
    sample_metrics
    sleep 1
done
sample_metrics

if kill -0 "$runner_pid" 2>/dev/null && command -v sample >/dev/null 2>&1; then
    sample "$runner_pid" 1 1 -file "$sample_log" >/dev/null 2>&1 || :
fi
if kill -0 "$runner_pid" 2>/dev/null; then kill -TERM "$runner_pid" 2>/dev/null || :; sleep 1; fi
if kill -0 "$runner_pid" 2>/dev/null; then kill -KILL "$runner_pid" 2>/dev/null || :; fi
wait "$runner_pid"
run_exit=$?

server="$runtime_root/host-arm64/server/wineserver"
if [[ -x "$server" ]]; then
    WINEPREFIX="$prefix" WINESERVER="$server" "$server" -k >/dev/null 2>&1 || :
fi

sed -nE 's/.*@ approx ([0-9.]+)fps, total ([0-9.]+)fps.*/\1 \2/p' "$run_log" >"$fps_log"
sed -nE 's/.*Present #([0-9]+) t=([0-9]+\.[0-9]+).*/\1 \2/p' "$run_log" >"$present_log"
if [[ -s "$fps_log" ]]; then
    awk -v skip="$warmup" '
        { line++; if (line <= skip) next; near=$1+0; total=$2+0; count++; sum+=near;
          if (count == 1 || near < min) min=near;
          if (count == 1 || near > max) max=near; last_total=total }
        END {
          if (count == 0) { print "FPS_STATUS=NO_SWAPBUFFERS"; print "FPS_SAMPLES=0"; exit 2 }
          average=sum/count;
          printf "FPS_SOURCE=WGL_SWAPBUFFERS\nFPS_STATUS=%s\n", (average >= 30 ? "PASS_30FPS" : "BELOW_30FPS");
          printf "FPS_SAMPLES=%d\nFPS_AVG=%.2f\nFPS_MIN=%.2f\nFPS_MAX=%.2f\nFPS_TOTAL=%.2f\n", count, average, min, max, last_total;
          printf "FPS_STABLE_30=%s\n", (min >= 30 ? "PASS" : "FAIL")
        }
    ' "$fps_log" >"$summary_log"
    summary_exit=$?
else
    awk -v warmup="$warmup" '
        BEGIN { count=0 }
        NR == 1 { boot_t=$2+0; have_previous=0 }
        {
          n=$1+0; t=$2+0;
          if (!have_start && (t-boot_t) >= warmup) {
            if (have_previous) { first_n=previous_n; first_t=previous_t; count=1 }
            else { first_n=n; first_t=t; count=0 }
            have_start=1;
          }
          previous_n=n; previous_t=t; have_previous=1;
          if (have_start) { count++; last_n=n; last_t=t }
        }
        END {
          if (!have_start || count < 2 || last_t <= first_t) {
            print "FPS_SOURCE=DXMT_PRESENT"; print "FPS_STATUS=INSUFFICIENT_PRESENT"; print "FPS_SAMPLES=" count; exit 2
          }
          average=(last_n-first_n)/(last_t-first_t);
          sustained=(count >= 3 && (last_t-first_t) >= 1.0);
          status=(sustained && average >= 30 ? "PASS_30FPS" : (sustained ? "BELOW_30FPS" : "INSUFFICIENT_PRESENT"));
          printf "FPS_SOURCE=DXMT_PRESENT\nFPS_STATUS=%s\n", status;
          printf "FPS_SAMPLES=%d\nFPS_AVG=%.2f\nFPS_MIN=NA\nFPS_MAX=NA\nFPS_TOTAL=%d\n", count, average, last_n;
          printf "FPS_STABLE_30=%s\n", (sustained && average >= 30 ? "PASS" : "UNAVAILABLE");
          if (!sustained) exit 2
        }
    ' "$present_log" >"$summary_log"
    summary_exit=$?
fi

printf 'FPS_ARCHITECTURE=%s\nFPS_BUDGET=%s\nFPS_CSMT=%s\nFPS_VSYNC=%s\nFPS_D3D9_BACKEND=%s\n' \
    "$architecture" "$budget" "$csmt" "$vsync" "$d3d9_backend" >>"$summary_log"
printf 'FPS_CAP=%s\nMETRICS_CSV=%s\n' "$fps_cap" "$metrics_csv" >>"$summary_log"
printf 'FPS_CONTEXT_REUSE_SLICES=%s\n' "$reuse_slices" >>"$summary_log"
awk -v warmup="$warmup" '
    BEGIN { lines=0 }
    NR == 1 { boot_t=$2+0; have_previous=0 }
    {
      n=$1+0; t=$2+0;
      if (!have_start && (t-boot_t) >= warmup) {
        if (have_previous) { first_n=previous_n; first_t=previous_t; lines=1 }
        else { first_n=n; first_t=t; lines=0 }
        have_start=1;
      }
      previous_n=n; previous_t=t; have_previous=1;
      if (have_start) { last_n=n; last_t=t; lines++ }
    }
    END {
      if (!have_start || lines < 2) { print "PRESENT_STATUS=INSUFFICIENT"; print "PRESENT_SAMPLES=" lines; exit }
      dt=last_t-first_t;
      if (dt > 0) printf "PRESENT_STATUS=MEASURED\nPRESENT_SAMPLES=%d\nPRESENT_FIRST=%d\nPRESENT_LAST=%d\nPRESENT_WINDOW_SECONDS=%.3f\nPRESENT_AVG_FPS=%.2f\n", lines, first_n, last_n, dt, (last_n-first_n)/dt;
      else printf "PRESENT_STATUS=INSUFFICIENT\nPRESENT_SAMPLES=%d\n", lines;
    }
' "$present_log" >>"$summary_log"

awk -F, '
    NR > 1 { count++; cpu += $2+0; if (($2+0) > cpu_max) cpu_max=$2+0;
             if (($3+0) > rss_max) rss_max=$3+0; rss_last=$3+0 }
    END {
      if (!count) { print "METRICS_STATUS=EMPTY"; exit }
      printf "METRICS_STATUS=MEASURED\nMETRICS_SAMPLES=%d\nCPU_AVG_PERCENT=%.2f\nCPU_MAX_PERCENT=%.2f\nRSS_MAX_KB=%d\nRSS_LAST_KB=%d\n", count, cpu/count, cpu_max, rss_max, rss_last;
      if (cpu/count >= 80) print "FPS_BOTTLENECK_HINT=CPU_BOUND_TCTI_OR_GUEST";
      else print "FPS_BOTTLENECK_HINT=NOT_CPU_SATURATED_CHECK_PRESENT_OR_GPU";
    }
' "$metrics_csv" >>"$summary_log"

cat "$summary_log"
echo "RUN_EXIT=$run_exit" | tee -a "$summary_log" >/dev/null
echo "FPS_LOG=$fps_log"
echo "SAMPLE_LOG=$sample_log"
echo "SUMMARY=$summary_log"
if [[ "$summary_exit" -ne 0 ]]; then
    echo "No usable WGL or DXMT Present samples were captured; inspect $run_log." >&2
    exit 2
fi
exit 0
