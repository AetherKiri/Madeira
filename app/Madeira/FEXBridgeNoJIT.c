/*
 * Compatibility symbols for the Madeira-SE App Store target.
 *
 * The old UI still contains diagnostic controls for the historical FEX
 * backend. Madeira-SE never calls those controls on its default path, but
 * keeping the C ABI as a small no-JIT stub lets one source tree build without
 * linking FEXCore or allocating executable memory. A legacy FEX development
 * build can replace this file with FEXBridge.mm explicitly.
 */

#include "FEXBridge.h"

#include <stdatomic.h>
#include <stddef.h>
#include <libkern/OSCacheControl.h>

/* ntdll's instruction-cache flush path is shared by Wine and QEMU. The
 * former FEX bridge supplied this compiler-rt symbol as a side effect; keep
 * the ABI while using Apple's non-JIT cache primitive directly. */
void __clear_cache(void *start, void *end)
{
    char *first = start, *last = end;
    if (!first || !last || last <= first) return;
    sys_icache_invalidate(first, (size_t)(last - first));
}

static _Atomic int madeira_eco_mode;

void madeira_set_eco(int on)
{
    atomic_store_explicit(&madeira_eco_mode, !!on, memory_order_relaxed);
}

int madeira_get_eco(void)
{
    return atomic_load_explicit(&madeira_eco_mode, memory_order_relaxed);
}

bool fex_initialize(void) { return false; }
void fex_shutdown(void) { }
int64_t fex_test_execute(void) { return -1; }
void fex_set_log_callback(fex_log_callback_t callback) { (void)callback; }
int64_t fex_get_jit_write_offset(void) { return 0; }
