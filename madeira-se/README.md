# Madeira-SE standalone runtime

This directory contains the standalone runtime boundary for Madeira-SE. It has
no dependency on AetherKiri or Godot. The public C API owns the runtime
lifecycle and exposes host-device hooks for frames and PCM audio. Wine's
Mach-O host is connected to embeddable i386 and x86-64 QEMU libraries using
UTM's AArch64 TCTI executor.

The current slice provides:

- safe PE32/PE32+ architecture detection for i386 and x86-64 images;
- an independent runtime lifecycle (`open`, `tick`, `pause`, `resume`, `close`);
- a no-runtime-code-generation CPU ABI for the QEMU TCTI adapter;
- a fixed-width Wine-host CPU transport with i386/amd64
  context conversion;
- process-wide Wine/QEMU bootstrap in `libmadeira_se_runtime.dylib`;
- real i386 and x86-64 TCTI execution, exception, memory-protection and
  instruction-budget probes;
- pure-guest Wine resource plans for i386 and x86-64;
- explicit graphics, audio and input boundaries;
- a small command-line probe tool; and
- unit tests for image probing, lifecycle transitions, CPU policy, memory
  notifications, Wine CPU messages and 32/64-bit Wine layouts.

The executor remains injectable, but the default standalone path loads the
pinned QEMU target dynamically. The development launcher now runs complete
PE32 and PE32+ smoke programs through the split Wine tree, including the DXMT
Metal bridge and native audio path. AetherKiri integration remains optional.

## Build and test

```sh
./scripts/test-madeira-se.sh

# Probe an existing Windows executable.
./scripts/test-madeira-se.sh /path/to/game.exe

# Build the standalone launcher and the GUI/FPS regression payloads.
cmake --build build/madeira-se-core --parallel 2 --target madeira-se-run
./scripts/build-madeira-se-gui-smoke.sh

# Build the DXMT PE modules and ARM64 Metal bridge.
./scripts/build-madeira-se-dxmt.sh

# Run GUI, audio, D3D11 Map/Unmap, and D3D9 Lock/Unlock/Present smoke tests
# for both guest architectures. The D3D9 case also checks the 1280x720
# virtual-mode compatibility shim used by legacy visual novels.
./scripts/run-madeira-se-gui-smoke.sh

# Measure the standalone OpenGL frame loop without a game-specific menu.
./scripts/run-madeira-se-fps.sh \
  --exe build/madeira-se-gui-smoke/fps-smoke-i386.exe \
  --duration 15 --warmup 2

# Exercise the d9mt-derived D3D9 -> Metal path explicitly.  DXMT's native
# Present counter is reported in run.log and is used when the title does not
# go through Wine's OpenGL swap counter.
./scripts/run-madeira-se-fps.sh \
  --exe build/madeira-se-gui-smoke/fps-smoke-i386.exe \
  --d3d9-backend dxmt --duration 15 --warmup 2

# Remove the display refresh ceiling when comparing translator throughput.
./scripts/run-madeira-se-fps.sh \
  --exe build/madeira-se-gui-smoke/fps-smoke-i386.exe \
  --duration 8 --warmup 2 --vsync 0

# Probe the PE and report missing Wine runtime resources.
/tmp/madeira-se-build/madeira-se-probe \
  --runtime-root app/Madeira /path/to/game.exe

# Configure a separate Wine tree that emits both guest PE architectures.
./scripts/configure-wine-madeira-se.sh

# Build and verify the bootstrap PE DLLs (defaults to the safe -j2 setting).
./scripts/build-wine-madeira-se-bootstrap.sh

# Fetch the pinned UTM QEMU fork containing aarch64-tcti.
./scripts/fetch-madeira-se-qemu.sh

# Configure and compile a no-JIT i386/x86-64 TCTI smoke build.
./scripts/configure-qemu-madeira-se.sh
./scripts/build-qemu-madeira-se-smoke.sh

# Optional: enable QEMU LTO for a release build (high peak linker memory).
MADEIRA_SE_QEMU_LTO=true ./scripts/configure-qemu-madeira-se.sh build/madeira-se-qemu-lto
```

Set `MADEIRA_SE_BUILD_DIR` when the build output should live somewhere other
than `/tmp/madeira-se-build`.

The API is C-compatible so the same core can be linked by a standalone
Madeira-SE runner or by an optional AetherKiri provider. Aether only supplies
host resources when embedded; it is not part of the runtime's compatibility
implementation.

`madeira-se-run` is the process launcher used by the development and packaging
profiles. It probes the PE header, selects the i386 or x86-64 TCTI library,
sets the Wine guest-build and prefix variables, and execs the signed ARM64
Wine loader. It launches the executable directly; a Wine desktop shell is not
started unless `--desktop` is supplied. Resource paths may be relative or
absolute and are resolved before the loader starts.

For a local build, launch a Windows executable directly with:

```sh
./build/madeira-se-core/madeira-se-run \
  --host-dir build/madeira-se-wine-native-host \
  --guest-dir build/madeira-se-wine-guest \
  --qemu build/madeira-se-qemu/libqemu-x86_64-softmmu.dylib \
  --runtime build/madeira-se-core/libmadeira_se_runtime.dylib \
  --prefix "$PWD/build/prefix-my-game" \
  --dxmt-dir build/madeira-se-dxmt/runtime \
  /path/to/game.exe arg1 arg2
```

The launcher accepts both PE32 and PE32+ images and selects the matching
QEMU/TCTI library automatically. `--workdir DIR` sets the Windows process
working directory. By default the launcher uses that directory as the game's
`%APPDATA%` root (or the executable's directory when `--workdir` is omitted),
and updates the prefix's shell-folder registry before Wine starts. Use
`--appdata-dir DIR` when a title needs a separate writable data directory.
CatSystem2 titles commonly resolve `setup.xml` below
`%APPDATA%\baseson\<title>`; pass the game bundle as `--workdir` so Madeira-SE
creates that writable root beside the executable and the existing file is
read in place. The launcher does not fabricate a title's installation or
copy-protection key files.
`--desktop` is an explicit compatibility fallback for a title that needs
Wine's shell; ordinary visual novels use the headless direct path.
Some DirectX 9 visual novels reject Retina-scaled macOS display modes before
creating a device. For Wine's wined3d path, opt in to the capability shim with
`--d3d9-virtual-mode 1920x1080`. It adds a 1920×1080 mode and reports that mode
as the current adapter mode while leaving the actual wined3d renderer in
place. The shim is disabled by default and is only a compatibility probe; it
does not add GPU features that the renderer cannot execute.

The launcher selects the D3D9 backend with `--d3d9-backend auto|dxmt|wined3d`
or `MADEIRA_SE_D3D9_BACKEND`. `auto` first checks whether the executable
references `d3d9`/`d3dx9`; only those titles receive the bundled DXMT D3D9→Metal
layer. This keeps D3D11-only programs on Wine's normal command stream while
still handling engines that reach D3D9 through `d3dx9_*.dll`. `dxmt` is the
d9mt-derived Metal path; it uses the same ARM64 `winemetal.so` bridge as
DXMT's D3D10/11 modules. `wined3d` is a reliable compatibility fallback and
can be selected with the same prefix after a DXMT run because the launcher
restores the Wine module explicitly. Explicit `dxmt` is useful for a title that
loads D3D9 dynamically and has no import-table marker.

Standalone launches for a D3D9 title enable the D3D9 window-reset
compatibility path and use Wine's single-threaded command stream by default.
This keeps the first Cocoa drawable alive when a legacy engine creates its
main window and immediately calls `IDirect3DDevice9::Reset`. The launcher does
not force that global Wine setting for D3D11/OpenGL-only programs, since the
x86-64 DXMT D3D11 path uses the normal command stream. Explicit
`MADEIRA_SE_D3D9_SOFT_RESET`, `MADEIRA_SE_D3D9_CSMT=0|1`, and
`WINE_D3D_CONFIG` values still override those defaults for diagnostics. The
DXMT backend owns its own Reset/presentation implementation, but the WoW64
guest still benefits from the serialized command stream during first-frame
startup.
PE32 OpenGL thunks translate object pointers through Madeira-SE's biased guest
arena while widening `GLintptr` and `GLsizeiptr` as integer values. This is
required by wined3d buffer uploads and fence synchronization on Apple Silicon.

Direct3D 9 titles can use the d9mt-derived DXMT Metal renderer or Wine's
wined3d/OpenGL fallback. The bundled DXMT overlay handles Direct3D 9, 10 and
11 calls when the corresponding backend is selected. The D3D9 layer is still
under active compatibility testing; use `--d3d9-backend wined3d` when a title
exercises an unsupported D3D9 feature.
`scripts/stage-madeira-se-runtime.sh` assembles a development link bundle or
a self-contained copy bundle for an app target.

DXMT shader timing statistics are opt-in with `DXMT_SHADER_STATS=1`. The
default keeps the standalone process free of a detached diagnostic worker so a
game can exit cleanly after its last frame.

To measure a title's actual presentation cadence on macOS, use the FPS harness:

```sh
./scripts/run-madeira-se-fps.sh \
  --runtime-root build/madeira-se-runtime \
  --exe /path/to/title.exe \
  --duration 15 --warmup 3
```

The harness enables Wine's `+fps` channel, which counts successful
`wglSwapBuffers` calls, and defaults to the DXMT D3D9-to-Metal path with a
30 FPS presentation cap. Use `--fps-cap 0` for an uncapped run or choose a
different integer from 1 to 240. DXMT's native Present counter is written to
`present.log`; it remains available for D3D9/Metal titles that do not pass
through Wine's OpenGL `wglSwapBuffers` counter. Each run stores `run.log`,
`fps.log`, `present.log`, `metrics.csv` (monotonic time, process CPU and RSS),
a macOS `sample.txt` stack capture when available, and `summary.txt` in
`build/fps/<timestamp>-<pid>/`. A missing OpenGL sample is measured from the
DXMT Present counter instead of being treated as a failed run. `summary.txt`
also records average/max CPU, peak RSS, and a bottleneck hint. A title that is
still decoding assets can therefore show a low Present rate while consuming a
full CPU core; that is a CPU/TCTI workload signal rather than a Metal Present
stall.
Use `--vsync 0` with the deterministic FPS payload to measure
translator/rendering throughput without the display refresh ceiling; leave the
default `--vsync 1` when measuring presentation cadence.

The standalone WoW64 path disables resident TCTI context reuse by default.
This is deliberate: a title can return to Wine between instruction slices and
change the guest context, while a resident QEMU CPU state would then continue
from stale registers. For controlled profiling only, set
`MADEIRA_SE_CPU_REUSE_SLICES=N`; the runtime bounds reuse to N consecutive
budget slices and falls back to a full import/export cycle. Keep the default
(`0`) for compatibility testing until a title has passed a long run.

The regression payloads use exit 43 for GUI, 47 for audio and 49 for a
D3D11 render-target submission. Those are success sentinels for the smoke
programs, not application exit-code conventions.

The QEMU revision is locked in `deps/qemu-tcti.lock`. The fetch step applies
the ordered patches in `patches/qemu-tcti/`: the first embeds the x86/TCTI
adapter, and the second adds no-JIT throughput counters plus TCTI hot-path
optimizations. Madeira-SE uses the full QEMU x86 translator and TCG
implementation. TCTI emits queues of pointers to precompiled AArch64 gadgets,
so it does not allocate JIT code pages or generate new executable instructions
at runtime. Release builds select QEMU's `nop` trace backend because the
standalone app does not expose QEMU trace logging.

The QEMU build emits `libqemu-i386-softmmu.dylib` and
`libqemu-x86_64-softmmu.dylib`; Madeira-SE embeds their CPU/TCG pieces and uses
`-machine none`, so it does not boot a virtual machine. The smoke script loads
each library in a separate process and executes guest instructions through
TCTI behind the public CPU ABI and the process-wide Wine runtime.

The App Store runtime uses a signed ARM64 Mach-O Wine host. It does not execute
ARM64 PE or ARM64EC system DLLs: i386 and x86-64 Wine DLLs stay on the guest
side and run through QEMU TCTI. The host owns syscall conversion, Wine Unix
calls and native platform drivers. PE32 uses a biased low-4-GiB backing arena
because XNU permanently reserves that range in native ARM64 processes; guest
addresses remain unchanged from the application's point of view.
