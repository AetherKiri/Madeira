# Madeira-SE ownership boundary

Madeira-SE is an independent Windows user-mode runtime. AetherKiri is an
optional embedding host and must not be required to build or run the core.

```text
Windows PE32 / PE32+
        |
        v
  Wine WoW64 + x86 TCTI executor
        |
        v
  Madeira-SE compatibility services
  (GDI, DirectWrite, WinMM, DirectSound, XAudio, input, media, files)
        |
        v
  Apple platform backends
  (DXMT/Metal, CoreAudio/AudioUnit, CoreText/FreeType, VideoToolbox)
        |
        +--> madeira-se-runner
        |
        +--> optional AetherKiri provider
```

The public C APIs separate four concerns:

1. **Runtime lifecycle.** The core probes the PE image, owns the launch state,
   and drives the executor.
2. **Compatibility output.** The executor submits frames and PCM samples to
   Madeira-SE. The core validates those buffers and forwards them to a native
   device sink.
3. **Host resources.** A host may provide a Metal surface, an audio device,
   an input source, a clock and logging. These are device hooks only; they do
   not replace Madeira-SE's Windows API, font, mixer or rendering semantics.
4. **CPU execution.** `include/madeira_se_cpu.h` is the boundary between Wine's
   signed Mach-O host and the complete QEMU x86 frontend. It transports
   x86 architectural state, memory-map changes, invalidations, system-call
   exits and exceptions. The core rejects backends that generate executable
   code at runtime.

`include/madeira_se_wine_cpu.h` defines the next boundary down: fixed-width,
pointer-free messages exchanged by Wine's signed Mach-O host and the standalone
CPU runtime. It covers process/thread lifetime, execution, memory-map changes,
translation invalidation and interruption. Separate i386/amd64 register images
make Wine `CONTEXT` conversion testable without importing Wine headers into the
standalone core.

The no-JIT backend is UTM QEMU's `tcg/aarch64-tcti`. QEMU still performs the
full x86 decode and lowers instructions to TCG IR. TCTI represents a translated
block as a queue of pointers to AArch64 gadgets compiled into the app. Running
the queue changes data and control flow but does not create executable pages.

Wine uses two pure-guest integration profiles:

- PE32 loads i386 Wine system DLLs and application modules into a biased guest
  address space. QEMU TCTI interprets every PE instruction. A signed Mach-O
  host performs WoW64 structure conversion and Wine Unix calls.
- PE32+ loads x86-64 Wine system DLLs and application modules as guest code.
  The same Mach-O host services syscalls while the x86-64 QEMU target executes
  all Windows-side code.

`include/madeira_se_wine.h` makes these resource decisions deterministic and
reports missing architecture-specific DLLs before Wine starts. The package has
signed ARM64 Mach-O loader/server/host binaries, i386 and x86-64 guest PE
directories, and one QEMU TCTI dylib per guest architecture. It contains no
ARM64 PE or ARM64EC execution path.

The Aether provider should therefore remain a thin adapter. It may borrow a
`MTLDevice` or an application audio session when embedded, but the DXMT
pipeline, font mapping, PCM buffering and Win32 input translation stay in
Madeira-SE. A standalone runner supplies the same hooks from an Apple host
without linking any Aether or Godot code.

## Test gates

Each layer has an independent gate:

- core unit tests cover PE probing, lifecycle transitions and buffer validation;
- CPU boundary tests cover no-JIT policy, 32/64-bit selection, memory-map
  replay and instruction exits; QEMU adapter probes execute real i386/x86-64
  instructions and cover budgets, syscall sentinels, memory protection and
  raw exception vectors;
- the standalone runner launches PE32 and PE32+ workloads without a Wine
  desktop, and the regression suite exercises GUI, audio and DXMT/Metal paths
  for both architectures;
- the copy-mode bundle is self-contained and re-signs rewritten native dylibs
  before dyld loads them;
- the Aether provider remains a later adapter layer and is not required by any
  standalone test.
