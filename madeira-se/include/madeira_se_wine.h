/*
 * Madeira-SE Wine runtime resource layout.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MADEIRA_SE_WINE_H
#define MADEIRA_SE_WINE_H

#include "madeira_se.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MADEIRA_SE_WINE_LAYOUT_VERSION 2u
#define MADEIRA_SE_WINE_PATH_CAPACITY 1024u

typedef enum madeira_se_wine_profile {
    MADEIRA_SE_WINE_PROFILE_UNKNOWN = 0,
    MADEIRA_SE_WINE_PROFILE_GUEST_I386_TCTI = 1,
    MADEIRA_SE_WINE_PROFILE_GUEST_AMD64_TCTI = 2,
} madeira_se_wine_profile_t;

enum {
    MADEIRA_SE_WINE_RESOURCE_HOST_NTDLL = 1u << 0,
    MADEIRA_SE_WINE_RESOURCE_HOST_LOADER = 1u << 1,
    MADEIRA_SE_WINE_RESOURCE_HOST_SERVER = 1u << 2,
    MADEIRA_SE_WINE_RESOURCE_GUEST_NTDLL = 1u << 3,
    MADEIRA_SE_WINE_RESOURCE_GUEST_KERNEL32 = 1u << 4,
    MADEIRA_SE_WINE_RESOURCE_QEMU_BACKEND = 1u << 5,
};

/*
 * The App Store profile executes no ARM64 PE image. Wine's native half is a
 * build-time Mach-O host, while every Windows PE module remains i386 or amd64
 * guest code interpreted by QEMU TCTI.
 */
typedef struct madeira_se_wine_layout {
    uint32_t version;
    madeira_se_architecture_t architecture;
    madeira_se_wine_profile_t profile;
    uint32_t required_resources;
    char runtime_root[MADEIRA_SE_WINE_PATH_CAPACITY];
    char native_host_directory[MADEIRA_SE_WINE_PATH_CAPACITY];
    char guest_pe_directory[MADEIRA_SE_WINE_PATH_CAPACITY];
    char windows_guest_system_directory[64];
    char qemu_backend_module[MADEIRA_SE_WINE_PATH_CAPACITY];
} madeira_se_wine_layout_t;

madeira_se_status_t madeira_se_wine_layout_init(const char *runtime_root_utf8,
                                                madeira_se_architecture_t architecture,
                                                madeira_se_wine_layout_t *out_layout);
madeira_se_status_t madeira_se_wine_layout_validate(const madeira_se_wine_layout_t *layout,
                                                    uint32_t *out_missing_resources);

const char *madeira_se_wine_profile_string(madeira_se_wine_profile_t profile);
size_t madeira_se_wine_format_missing_resources(uint32_t missing_resources,
                                                char *buffer,
                                                size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* MADEIRA_SE_WINE_H */
