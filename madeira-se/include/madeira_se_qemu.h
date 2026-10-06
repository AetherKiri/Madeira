/*
 * Madeira-SE embedded QEMU TCTI loader.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MADEIRA_SE_QEMU_H
#define MADEIRA_SE_QEMU_H

#include "madeira_se_cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Starts one architecture-specific QEMU shared library in the current
 * process and returns a copy of its no-JIT CPU backend. QEMU owns worker
 * threads and process-global state, so the selected library stays loaded for
 * the lifetime of the process. Repeated calls with the same path are safe.
 */
madeira_se_status_t madeira_se_qemu_tcti_start(
    const char *library_path,
    madeira_se_cpu_backend_t *out_backend);

const char *madeira_se_qemu_tcti_library_path(void);

#ifdef __cplusplus
}
#endif

#endif /* MADEIRA_SE_QEMU_H */
