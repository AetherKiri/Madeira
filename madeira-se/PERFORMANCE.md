# Madeira-SE performance notes

The current performance gate uses the A7-3 PE32 title with the DXMT D3D9 to
Metal path at 1280x720. The runner uses the compatibility-safe defaults:
resident TCTI context reuse is disabled, the CPU slice budget is 8,000,000
guest instructions, and the presentation cap is 30 FPS.

On the Apple Silicon development host, the baseline run before the TCTI
temporary-frame change measured about 2.44 Present FPS. After marking the
per-TB temporary array uninitialized, while retaining Clang's stack protector,
the comparable 40-second runs measured 3.05 to 3.42 Present FPS. The result is
still below the 30 FPS target, but it removes a repeated 1 KiB stack clear from
the hottest TCTI entry path without weakening the rest of QEMU's hardening.

The same entry path now caches the host TLS slot used by helper return
addresses in QEMU's CPU state. A warm i386 backend probe improved from a
median of roughly 6.75 ms to 6.62 ms for two million guest instructions
(about 1.8%). The A7 title remains dominated by guest work and showed no
material FPS change, but the cache removes one macOS TLS accessor from every
translation-block entry while the CPU thread remains fixed.

The TCTI build also avoids the Apple JIT write-protection check and the
split-WX address conversion on every TB return. Both paths are irrelevant to
TCTI's non-executable RW bytecode buffer. The A7 runs measured 3.18 to 3.26
Present FPS after these changes, so they are retained as low-risk hot-path
cleanup rather than claimed as the 30 FPS fix. Release builds disable QEMU's
trace backend (`trace_backends=nop`) because Madeira-SE does not expose QEMU
trace logging to the app.

The samples show the CPU-side TCTI interpreter as the limiting resource:
`cpu_tb_exec`, `tcg_qemu_tb_exec`, and the generated AArch64 gadget calls
dominate the busy thread. DXMT reports no drawable wait or Metal command queue
stall during the measured window. The title also performs a long asset and
script phase before it settles into its menu, so Present FPS is reported from
the native DXMT Present counter rather than inferred from GPU timing.

With `MADEIRA_SE_PERF_STATS=1`, one active A7 title window counted about
1.59 billion guest instructions and 174.8 million TCTI TB entries, while only
245 thousand TBs needed translation. The translated TBs covered about 1.38
million static instructions, so the cost is repeated interpretation of a
small translated working set rather than translation or Metal submission.

An executable-page-only translation invalidation experiment was measured and
discarded. Some titles use dirty notifications while changing code protection;
filtering those notifications reduced the title's draw count and lowered the
measured Present rate. The shipping path therefore keeps the conservative
invalidation behavior for compatibility.

The next performance work should target TCTI dispatch and translation-cache
reuse with per-title regression coverage. Reaching 30 FPS for this workload
requires roughly an order of magnitude more CPU throughput; changing the
Metal presentation path alone cannot provide that gain. All proposed changes
must keep the no-runtime-code-generation policy required by the App Store.
