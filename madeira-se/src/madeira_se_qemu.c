/*
 * Madeira-SE embedded QEMU TCTI loader.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "madeira_se_qemu.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef void (*qemu_init_fn)(int argc, char **argv);
typedef void (*qemu_unlock_fn)(void);
typedef const madeira_se_cpu_backend_t *(*qemu_backend_fn)(void);

static pthread_mutex_t qemu_loader_mutex = PTHREAD_MUTEX_INITIALIZER;
static void *qemu_library;
static char *qemu_library_path;
static madeira_se_cpu_backend_t qemu_backend;

static int backend_is_valid(const madeira_se_cpu_backend_t *backend)
{
    return backend != NULL
        && backend->version == MADEIRA_SE_CPU_ABI_VERSION
        && backend->name != NULL
        && (backend->capabilities & MADEIRA_SE_CPU_CAP_NO_RUNTIME_CODEGEN) != 0u
        && (backend->capabilities
            & (MADEIRA_SE_CPU_CAP_X86_32 | MADEIRA_SE_CPU_CAP_X86_64)) != 0u
        && backend->create != NULL
        && backend->run != NULL
        && backend->interrupt != NULL
        && backend->memory_event != NULL
        && backend->invalidate != NULL
        && backend->destroy != NULL;
}

madeira_se_status_t madeira_se_qemu_tcti_start(
    const char *library_path,
    madeira_se_cpu_backend_t *out_backend)
{
    qemu_init_fn qemu_init_entry;
    qemu_unlock_fn bql_unlock_entry;
    qemu_unlock_fn replay_mutex_unlock_entry;
    qemu_backend_fn backend_entry;
    const madeira_se_cpu_backend_t *backend;
    void *library;
    char *qemu_argv[] = {
        NULL,
        "-machine", "none",
        "-cpu", "max,apic-id=0",
        "-accel", "tcg,thread=single",
        "-icount", "shift=0,sleep=off",
        "-display", "none",
        "-nodefaults",
        "-no-user-config",
        "-S",
        NULL,
    };
    const int qemu_argc = (int)(sizeof(qemu_argv) / sizeof(qemu_argv[0])) - 1;
    madeira_se_status_t status = MADEIRA_SE_OK;

    if (library_path == NULL || library_path[0] == '\0' || out_backend == NULL)
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    memset(out_backend, 0, sizeof(*out_backend));

    (void)pthread_mutex_lock(&qemu_loader_mutex);
    if (qemu_library != NULL) {
        if (strcmp(qemu_library_path, library_path) != 0) {
            status = MADEIRA_SE_E_INVALID_STATE;
        } else {
            *out_backend = qemu_backend;
        }
        (void)pthread_mutex_unlock(&qemu_loader_mutex);
        return status;
    }

    library = dlopen(library_path, RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) {
        (void)pthread_mutex_unlock(&qemu_loader_mutex);
        return MADEIRA_SE_E_NOT_FOUND;
    }
    *(void **)(&qemu_init_entry) = dlsym(library, "qemu_init");
    *(void **)(&bql_unlock_entry) = dlsym(library, "bql_unlock");
    *(void **)(&replay_mutex_unlock_entry) =
        dlsym(library, "replay_mutex_unlock");
    *(void **)(&backend_entry) =
        dlsym(library, "madeira_se_qemu_tcti_backend");
    if (qemu_init_entry == NULL || bql_unlock_entry == NULL
        || replay_mutex_unlock_entry == NULL || backend_entry == NULL) {
        (void)dlclose(library);
        (void)pthread_mutex_unlock(&qemu_loader_mutex);
        return MADEIRA_SE_E_UNSUPPORTED;
    }

    qemu_argv[0] = (char *)library_path;
    qemu_init_entry(qemu_argc, qemu_argv);
    bql_unlock_entry();
    replay_mutex_unlock_entry();
    backend = backend_entry();
    if (!backend_is_valid(backend)) {
        /* QEMU owns live worker threads after qemu_init(), so do not dlclose. */
        (void)pthread_mutex_unlock(&qemu_loader_mutex);
        return MADEIRA_SE_E_BACKEND;
    }

    qemu_library_path = strdup(library_path);
    if (qemu_library_path == NULL) {
        (void)pthread_mutex_unlock(&qemu_loader_mutex);
        return MADEIRA_SE_E_OUT_OF_MEMORY;
    }
    qemu_library = library;
    qemu_backend = *backend;
    *out_backend = qemu_backend;
    (void)pthread_mutex_unlock(&qemu_loader_mutex);
    return MADEIRA_SE_OK;
}

const char *madeira_se_qemu_tcti_library_path(void)
{
    const char *path;

    (void)pthread_mutex_lock(&qemu_loader_mutex);
    path = qemu_library_path;
    (void)pthread_mutex_unlock(&qemu_loader_mutex);
    return path;
}
