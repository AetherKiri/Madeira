/*
 * Madeira-SE host implementation of the Wine CPU-provider transport.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MADEIRA_SE_WINE_CPU_HOST_H
#define MADEIRA_SE_WINE_CPU_HOST_H

#include "madeira_se_wine_cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct madeira_se_wine_cpu_host madeira_se_wine_cpu_host_t;

/*
 * The host copies the backend table. The backend's userdata and name storage
 * must remain valid until the host is destroyed.
 */
madeira_se_status_t madeira_se_wine_cpu_host_create(
    const madeira_se_cpu_backend_t *backend,
    madeira_se_wine_cpu_host_t **out_host);
void madeira_se_wine_cpu_host_destroy(madeira_se_wine_cpu_host_t *host);

/*
 * The returned endpoint is owned by host and remains valid until host is
 * destroyed. Pass it to madeira_se_wine_cpu_dispatch().
 */
const madeira_se_wine_cpu_endpoint_t *madeira_se_wine_cpu_host_endpoint(
    madeira_se_wine_cpu_host_t *host);

/*
 * A Wine Unix library cannot carry a native host pointer through the PE ABI.
 * The standalone launcher therefore activates one host for the Wine process,
 * and madeiracpu.so resolves the exported dispatch symbol below with dlsym().
 */
madeira_se_status_t madeira_se_wine_cpu_host_activate(
    madeira_se_wine_cpu_host_t *host);
void madeira_se_wine_cpu_host_deactivate(madeira_se_wine_cpu_host_t *host);

#if defined(__GNUC__)
# define MADEIRA_SE_WINE_CPU_EXPORT __attribute__((visibility("default")))
#else
# define MADEIRA_SE_WINE_CPU_EXPORT
#endif

MADEIRA_SE_WINE_CPU_EXPORT int32_t madeira_se_wine_cpu_dispatch_message(
    uint32_t operation, void *message, uint32_t message_size);

#ifdef __cplusplus
}
#endif

#endif /* MADEIRA_SE_WINE_CPU_HOST_H */
