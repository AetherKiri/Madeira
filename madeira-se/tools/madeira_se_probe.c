/*
 * Madeira-SE PE architecture probe.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "madeira_se.h"
#include "madeira_se_wine.h"

#include <stdio.h>
#include <string.h>

static const char *architecture_name(madeira_se_architecture_t architecture)
{
    switch (architecture) {
    case MADEIRA_SE_ARCH_X86_32: return "x86-32";
    case MADEIRA_SE_ARCH_X86_64: return "x86-64";
    default: return "unknown";
    }
}

int main(int argc, char **argv)
{
    madeira_se_architecture_t architecture;
    madeira_se_status_t status;
    const char *runtime_root = NULL;
    const char *image_path;

    if (argc == 2) {
        image_path = argv[1];
    } else if (argc == 4 && strcmp(argv[1], "--runtime-root") == 0) {
        runtime_root = argv[2];
        image_path = argv[3];
    } else {
        fprintf(stderr, "usage: %s [--runtime-root <directory>] <windows-pe>\n", argv[0]);
        return 2;
    }

    status = madeira_se_probe_pe_file(image_path, &architecture);
    if (status != MADEIRA_SE_OK) {
        fprintf(stderr, "%s: %s\n", image_path, madeira_se_status_string(status));
        return 1;
    }
    printf("%s: %s\n", image_path, architecture_name(architecture));

    if (runtime_root != NULL) {
        madeira_se_wine_layout_t layout;
        uint32_t missing = 0u;
        char missing_text[512];

        status = madeira_se_wine_layout_init(runtime_root, architecture, &layout);
        if (status != MADEIRA_SE_OK) {
            fprintf(stderr, "runtime layout: %s\n", madeira_se_status_string(status));
            return 1;
        }
        printf("Wine profile: %s\n", madeira_se_wine_profile_string(layout.profile));
        printf("native host directory: %s\n", layout.native_host_directory);
        printf("guest PE directory: %s\n", layout.guest_pe_directory);
        printf("QEMU backend: %s\n", layout.qemu_backend_module);
        status = madeira_se_wine_layout_validate(&layout, &missing);
        if (status != MADEIRA_SE_OK) {
            (void)madeira_se_wine_format_missing_resources(missing, missing_text,
                                                           sizeof(missing_text));
            printf("Wine resources: not ready (%s)\n", missing_text);
            return 3;
        }
        puts("Wine resources: ready");
    }
    return 0;
}
