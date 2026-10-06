/*
 * Madeira-SE process-wide Wine/QEMU runtime probe.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "madeira_se_wine_cpu_host.h"
#include "madeira_se_wine_runtime.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    madeira_se_wine_cpu_query_message_t query;
    madeira_se_status_t status;

    if (argc != 2) {
        fprintf(stderr, "usage: %s qemu-dylib\n", argv[0]);
        return 2;
    }
    status = madeira_se_wine_qemu_runtime_start(argv[1]);
    if (status != MADEIRA_SE_OK ||
        madeira_se_wine_qemu_runtime_start(argv[1]) != MADEIRA_SE_OK) {
        fprintf(stderr, "runtime startup failed: %s\n",
                madeira_se_status_string(status));
        return 1;
    }

    memset(&query, 0, sizeof(query));
    query.header.version = MADEIRA_SE_WINE_CPU_ABI_VERSION;
    query.header.size = sizeof(query);
    status = (madeira_se_status_t)madeira_se_wine_cpu_dispatch_message(
        MADEIRA_SE_WINE_CPU_QUERY, &query, sizeof(query));
    if (status != MADEIRA_SE_OK ||
        (query.capabilities & MADEIRA_SE_CPU_CAP_NO_RUNTIME_CODEGEN) == 0u ||
        (query.capabilities &
         (MADEIRA_SE_CPU_CAP_X86_32 | MADEIRA_SE_CPU_CAP_X86_64)) == 0u) {
        fprintf(stderr, "runtime query failed: %s capabilities=%#x\n",
                madeira_se_status_string(status), query.capabilities);
        return 1;
    }
    puts("MADEIRA_SE_QEMU_RUNTIME_OK");
    return 0;
}
