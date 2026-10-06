/*
 * Madeira-SE standalone Wine/QEMU runtime bootstrap.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MADEIRA_SE_WINE_RUNTIME_H
#define MADEIRA_SE_WINE_RUNTIME_H

#include "madeira_se.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__)
# define MADEIRA_SE_RUNTIME_EXPORT __attribute__((visibility("default")))
#else
# define MADEIRA_SE_RUNTIME_EXPORT
#endif

/*
 * Starts the process-wide Wine CPU provider with one embeddable QEMU target.
 * QEMU owns worker threads after startup, so the runtime intentionally remains
 * active for the lifetime of the Wine process. Repeating the same path is
 * idempotent; selecting another target in the same process is rejected.
 */
MADEIRA_SE_RUNTIME_EXPORT madeira_se_status_t
madeira_se_wine_qemu_runtime_start(const char *qemu_library_path);

#ifdef __cplusplus
}
#endif

#endif /* MADEIRA_SE_WINE_RUNTIME_H */
