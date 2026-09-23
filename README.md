# Madeira

Run Windows PC games on a non-jailbroken iPhone.

Madeira combines [Wine](https://www.winehq.org/) (ARM64EC),
[FEX-Emu](https://github.com/FEX-Emu/FEX) for x86-64 → ARM64 translation, and
[DXMT](https://github.com/3Shain/DXMT) for D3D11 → Metal, running as a single
Mach process on iOS with wineserver as a thread rather than a separate process.

## Status

Thumper and ULTRAKILL are playable. Marvel Cosmic Invasion has reached
gameplay, though a run has also ended in an unexplained termination and its
controls are not yet reliable. Others reach gameplay at low frame rates. This
is a research project, not a product: expect rough edges, per-title quirks and
breaking changes.

## Requirements

- A non-jailbroken iPhone. Development has been on an A15 (iPhone 13 Pro).
- JIT, which on iOS requires a debugger to attach —
  [StikDebug](https://github.com/0-Blu/StikJIT) is what this project uses.
- An Apple ID for signing. A free account works; its provisioning profiles
  expire after 7 days, so the app must be rebuilt and reinstalled weekly. The
  app's container survives reinstall, so prefixes and saves are preserved.

Because JIT requires debugger attach, this app cannot be distributed through the
App Store. It is installed by sideloading.

## Building

The build is split across several chains — the unix-side Wine libraries, the
ARM64EC PE modules, FEX, DXMT and the iOS app itself. `build/*/build.sh` covers
the native pieces; the app is built with `xcodebuild`.

```sh
git clone --recurse-submodules <this repo>
```

Note that `FEX`, `wine` and `research/dxmt` are submodules pointing at forks
containing the iOS work; upstream clones will not build here.

### Run the macOS graphics demo

The Remote Metal host can run independently on a Mac with a Metal GPU. It
opens a native window and renders a triangle through the project's transport.
This validates the graphics backend; it does not launch Windows games or the
complete Madeira iOS app.

```sh
./scripts/run-macos-demo.sh          # build, start, and render 600 frames
./scripts/run-macos-demo.sh status
./scripts/run-macos-demo.sh test     # protocol, client, rendering, and replay
./scripts/run-macos-demo.sh stop
```

The launcher binds to `127.0.0.1:47821`, keeps its token and logs under
`build/DerivedData/remote-metal/`, and uses `/Applications/Xcode.app` when
`DEVELOPER_DIR` is unset. The test suites compile their own shader fixture and
require Apple's Metal Toolchain (`xcodebuild -downloadComponent MetalToolchain`).
The current client tests expect macOS 15 or later and Apple9 GPU support.

### Madeira-SE standalone runtime (experimental)

The `madeira-se/` tree is the independent runtime boundary being developed for
the no-JIT App Store path. It does not link AetherKiri or Godot. The standalone
launcher now executes PE32 and PE32+ Windows programs through the split Wine
guest trees, QEMU TCTI and DXMT/Metal, with direct headless startup by default.
AetherKiri remains an optional embedding layer.

```sh
MADEIRA_SE_BUILD_JOBS=2 ./scripts/test-madeira-se.sh
./scripts/build-madeira-se-gui-smoke.sh
./scripts/build-madeira-se-dxmt.sh
# The regression suite covers both PE32 and PE32+ dynamic D3D11 Map/Unmap and
# D3D9 Lock/Unlock/Present paths in addition to GUI and audio startup; the
# D3D9 case also exercises the virtual display-mode compatibility shim.
./scripts/run-madeira-se-gui-smoke.sh
# Measure a title's presentation cadence and capture CPU/RSS hotspots.  The
# harness uses the DXMT-to-Metal path by default and falls back to WGL samples
# when a title uses Wine's OpenGL path.
./scripts/run-madeira-se-fps.sh --exe /path/to/title.exe
```

To launch a title directly, use `madeira-se-run` with the host, guest, QEMU,
runtime, prefix and DXMT directories documented in
[`madeira-se/README.md`](madeira-se/README.md). The executable's PE header
selects i386 versus x86-64 automatically; no Wine desktop is needed unless
`--desktop` is explicitly requested.

`madeira-se-run` accepts `--d3d9-backend auto|dxmt|wined3d`. In `auto` mode it
only enables the bundled d9mt-derived D3D9 Metal module for executables that
reference D3D9 (including `d3dx9_*.dll`); D3D11-only programs retain DXMT's
normal command stream. Use `--d3d9-backend dxmt` to force the path for a title
that loads D3D9 dynamically, or `wined3d` for the compatibility fallback.

## License

**GPL-3.0-or-later** — see [`LICENSE`](LICENSE). Derivatives that are
distributed must remain open source.

### Upstream licenses vs. this project's forks

Those are the licenses of the **upstream projects**: Wine and GnuTLS
LGPL-2.1-or-later, GMP and Nettle LGPL-3.0-or-later, FEX-Emu and DXMT MIT,
rpmalloc 0BSD. Their texts are in [`LICENSES/`](LICENSES), and upstream code
remains available under them **from upstream**.

**The forks used here are not licensed identically to their upstreams.** Each
carries its own `LICENSE-MADEIRA.md` saying exactly what applies:

| Fork | Terms |
|---|---|
| [`wine`](https://github.com/willfaust/wine) | relicensed to **GPL-3.0-or-later** under LGPL-2.1 §3 |
| [`FEX`](https://github.com/willfaust/FEX), [`dxmt`](https://github.com/willfaust/dxmt) | upstream MIT preserved; modifications **GPL-3.0-or-later** |
| [`rpmalloc`](https://github.com/willfaust/rpmalloc) | upstream 0BSD preserved; Will Faust's modifications **GPL-3.0-or-later** |

This is not retroactive: those forks were public beforehand, so anything
already obtained under a permissive license stays available under it.

[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md) has the per-component
breakdown. Note in particular that the Microsoft Visual C++ runtime DLLs are
not distributed here and must be supplied yourself — see
[`tools/fetch-vcruntime.md`](tools/fetch-vcruntime.md).

## A note on upstream contributions

The forks here contain substantial AI-assisted work. FEX-Emu's contribution
policy states that AI must not be used to generate code for contributions to
that project, so **do not submit AI-generated changes from this fork upstream**.
The MIT license permits the fork itself; the policy governs contributions back.
Check each upstream's contribution policy before proposing changes to it.
