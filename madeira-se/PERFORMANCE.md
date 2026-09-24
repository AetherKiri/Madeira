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

The TCTI soft-MMU fast path now matches the locked QEMU layout. Its four
current `CPUTLBDescFast` entries are addressed at -272, -256, -240 and -224
bytes from the negative-offset CPU state; the older gadget set only covered
the -32 through -128 cases and therefore sent these accesses through the C
slow path. Madeira-SE emits precompiled gadget variants for all four entries
and branches to the slow path when a table is still unallocated. On the same
A7 payload, the final 40-second run measured 3.93 DXMT Present FPS versus the
3.06 safe-build result (about 28%); it remains below the 30 FPS target and is
guarded by the full i386/x86-64 DXMT regression suite.

The opt-in TCTI profile also records the static `MemOpIdx` values used by
translated blocks. In the A7-3 sample, i386 loads and stores were dominated by
`0x5023` (32-bit), followed by `0x5013` (16-bit) and `0x5e03` (byte); the
profile is enabled only with `MADEIRA_SE_PERF_STATS=1` and is not part of the
shipping hot path. Madeira-SE now emits precompiled immediate-specialized
gadgets for those exact modes, plus the observed `0x5033` 64-bit store, while
retaining the generic TCTI thunk for every other mode. The specialized path
keeps the same soft-MMU miss helper and alignment decision, but avoids reading
the operation index from the bytecode stream and avoids its pointer increment
on a TLB hit. A 45-second post-change A7 run measured 3.10 Present FPS with
88.04% average process CPU; this is a small, noisy improvement over the prior
2.87 FPS sample, so it is treated as a low-risk incremental optimization, not
as evidence that the 30 FPS target has been reached.

The samples show the CPU-side TCTI interpreter as the limiting resource:
`cpu_tb_exec`, `tcg_qemu_tb_exec`, and the generated AArch64 gadget calls
dominate the busy thread. DXMT reports no drawable wait or Metal command queue
stall during the measured window. The title also performs a long asset and
script phase before it settles into its menu, so Present FPS is reported from
the native DXMT Present counter rather than inferred from GPU timing.

The byte-store half of the same `MemOpIdx` specialization is now covered by a
precompiled `0x5e03` `strb` gadget. It preserves the generic miss helper and
alignment selection while removing the operation-index fetch on a TLB hit. The
60-second A7-3 sample after this change measured 2.95 Present FPS at 90.27%
average process CPU, within the run-to-run noise of the earlier 3 FPS samples.
It is retained for coverage and a low-risk hot path, but it is not claimed as
a material FPS improvement.

An isolated Apple M4 scheduling build (`-mtune=apple-m4`) was compared using
the same A7-3 overlay and runtime settings. It measured 2.93 Present FPS at
89.89% average CPU versus 2.95 FPS at 90.27% for the default build, so the
compiler hint does not improve this TCTI-bound workload and is not used by the
shipping build. A global uninitialized-variable switch was also rejected after
it made the Wine server unstable; only the explicitly scoped TCTI temporary
frame optimization remains enabled.

Two more isolated A/B experiments were rejected. Reordering the precompiled
gadget functions to place the most frequently sampled register variants
together produced 3.11 FPS in one 60-second run and 2.85 FPS in the repeat;
the default was 2.89 FPS in the paired run, so there is no repeatable locality
gain. Passing the TCTI entry values through compiler register operands also
passed both architecture probes, but its A7 run measured 2.72 FPS versus 2.89
FPS for the default entry path. Neither experiment changes the shipping
runtime.

Capturing the TB return value directly in an output register removed the visible
return-value store/load pair in the entry assembly and passed both shared-library
probes. The matching A7 sample measured 2.61 FPS, below the 2.89 FPS default
sample, so the existing stack-backed return path is retained.

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
