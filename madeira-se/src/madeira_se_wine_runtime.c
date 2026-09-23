/*
 * Madeira-SE standalone Wine/QEMU runtime bootstrap.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "madeira_se_wine_runtime.h"

#include "madeira_se_qemu.h"
#include "madeira_se_wine_cpu_host.h"

#include <pthread.h>
#include <string.h>

static pthread_mutex_t runtime_mutex = PTHREAD_MUTEX_INITIALIZER;
static madeira_se_wine_cpu_host_t *runtime_host;

madeira_se_status_t madeira_se_wine_qemu_runtime_start(
    const char *qemu_library_path)
{
    madeira_se_cpu_backend_t backend;
    madeira_se_wine_cpu_host_t *host = NULL;
    madeira_se_status_t status;

    if (qemu_library_path == NULL || qemu_library_path[0] == '\0')
        return MADEIRA_SE_E_INVALID_ARGUMENT;

    (void)pthread_mutex_lock(&runtime_mutex);
    if (runtime_host != NULL) {
        const char *active_path = madeira_se_qemu_tcti_library_path();

        status = active_path != NULL &&
                 strcmp(active_path, qemu_library_path) == 0
               ? MADEIRA_SE_OK : MADEIRA_SE_E_INVALID_STATE;
        (void)pthread_mutex_unlock(&runtime_mutex);
        return status;
    }

    status = madeira_se_qemu_tcti_start(qemu_library_path, &backend);
    if (status == MADEIRA_SE_OK)
        status = madeira_se_wine_cpu_host_create(&backend, &host);
    if (status == MADEIRA_SE_OK)
        status = madeira_se_wine_cpu_host_activate(host);
    if (status != MADEIRA_SE_OK) {
        madeira_se_wine_cpu_host_destroy(host);
        (void)pthread_mutex_unlock(&runtime_mutex);
        return status;
    }
    runtime_host = host;
    (void)pthread_mutex_unlock(&runtime_mutex);
    return MADEIRA_SE_OK;
}
