/*
 * QEMU shared-library initialization smoke probe for Madeira-SE.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

typedef void (*qemu_init_fn)(int argc, char **argv);
typedef void (*qemu_unlock_fn)(void);
typedef int (*madeira_se_qemu_tcti_smoke_fn)(void);

int main(int argc, char **argv)
{
    void *library;
    qemu_init_fn qemu_init_entry;
    qemu_unlock_fn bql_unlock_entry;
    qemu_unlock_fn replay_mutex_unlock_entry;
    madeira_se_qemu_tcti_smoke_fn smoke_entry;
    char *qemu_argv[] = {
        "madeira-se-qemu",
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

    if (argc != 2) {
        fprintf(stderr, "usage: %s qemu-dylib\n", argv[0]);
        return 2;
    }
    library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }
    *(void **)(&qemu_init_entry) = dlsym(library, "qemu_init");
    if (qemu_init_entry == NULL) {
        fprintf(stderr, "qemu_init missing: %s\n", dlerror());
        return 1;
    }
    *(void **)(&bql_unlock_entry) = dlsym(library, "bql_unlock");
    *(void **)(&replay_mutex_unlock_entry) =
        dlsym(library, "replay_mutex_unlock");
    *(void **)(&smoke_entry) =
        dlsym(library, "madeira_se_qemu_tcti_smoke");
    if (bql_unlock_entry == NULL || replay_mutex_unlock_entry == NULL
        || smoke_entry == NULL) {
        fprintf(stderr, "Madeira-SE QEMU embedding API is incomplete\n");
        return 1;
    }

    qemu_init_entry(qemu_argc, qemu_argv);
    bql_unlock_entry();
    replay_mutex_unlock_entry();
    if (smoke_entry() != 0) {
        fprintf(stderr, "TCTI guest execution probe failed\n");
        return 1;
    }
    puts("MADEIRA_SE_QEMU_TCTI_OK");
    (void)fflush(stdout);

    /*
     * QEMU owns vCPU worker threads and process-global subsystems. This probe
     * exits without dlclose teardown after the guest instruction check.
     */
    _Exit(0);
}
