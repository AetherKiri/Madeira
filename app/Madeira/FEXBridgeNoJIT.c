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

bool fex_initialize(void) { return false; }
void fex_shutdown(void) { }
int64_t fex_test_execute(void) { return -1; }
void fex_set_log_callback(fex_log_callback_t callback) { (void)callback; }
int64_t fex_get_jit_write_offset(void) { return 0; }
