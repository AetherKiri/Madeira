/*
 * Madeira-SE Wine runtime resource layout.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "madeira_se_wine.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int copy_string(char *destination, size_t capacity, const char *source)
{
    int length;

    if (destination == NULL || capacity == 0u || source == NULL) return 0;
    length = snprintf(destination, capacity, "%s", source);
    return length >= 0 && (size_t)length < capacity;
}

static int join_path(char *destination, size_t capacity,
                     const char *root, const char *component)
{
    int length;
    const char *separator;
    size_t root_length;

    if (destination == NULL || capacity == 0u || root == NULL || component == NULL)
        return 0;
    root_length = strlen(root);
    separator = root_length > 0u && root[root_length - 1u] == '/' ? "" : "/";
    length = snprintf(destination, capacity, "%s%s%s", root, separator, component);
    return length >= 0 && (size_t)length < capacity;
}

static int regular_file_exists(const char *directory, const char *filename)
{
    struct stat info;
    char path[MADEIRA_SE_WINE_PATH_CAPACITY];

    if (!join_path(path, sizeof(path), directory, filename)) return 0;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static int regular_path_exists(const char *path)
{
    struct stat info;
    return path != NULL && stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

madeira_se_status_t madeira_se_wine_layout_init(const char *runtime_root_utf8,
                                                madeira_se_architecture_t architecture,
                                                madeira_se_wine_layout_t *out_layout)
{
    const char *qemu_name;

    if (runtime_root_utf8 == NULL || runtime_root_utf8[0] == '\0' || out_layout == NULL)
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    memset(out_layout, 0, sizeof(*out_layout));
    out_layout->version = MADEIRA_SE_WINE_LAYOUT_VERSION;
    out_layout->architecture = architecture;
    if (!copy_string(out_layout->runtime_root, sizeof(out_layout->runtime_root),
                     runtime_root_utf8)
        || !join_path(out_layout->native_host_directory,
                      sizeof(out_layout->native_host_directory),
                      runtime_root_utf8, "host-arm64"))
        return MADEIRA_SE_E_INVALID_ARGUMENT;

    switch (architecture) {
    case MADEIRA_SE_ARCH_X86_32:
        out_layout->profile = MADEIRA_SE_WINE_PROFILE_GUEST_I386_TCTI;
        out_layout->required_resources =
            MADEIRA_SE_WINE_RESOURCE_HOST_NTDLL
            | MADEIRA_SE_WINE_RESOURCE_HOST_LOADER
            | MADEIRA_SE_WINE_RESOURCE_HOST_SERVER
            | MADEIRA_SE_WINE_RESOURCE_GUEST_NTDLL
            | MADEIRA_SE_WINE_RESOURCE_GUEST_KERNEL32
            | MADEIRA_SE_WINE_RESOURCE_QEMU_BACKEND;
        if (!join_path(out_layout->guest_pe_directory,
                       sizeof(out_layout->guest_pe_directory),
                       runtime_root_utf8, "i386-windows")
            || !copy_string(out_layout->windows_guest_system_directory,
                            sizeof(out_layout->windows_guest_system_directory),
                            "drive_c/windows/syswow64"))
            return MADEIRA_SE_E_INVALID_ARGUMENT;
        qemu_name = "qemu/libqemu-i386-softmmu.dylib";
        break;

    case MADEIRA_SE_ARCH_X86_64:
        out_layout->profile = MADEIRA_SE_WINE_PROFILE_GUEST_AMD64_TCTI;
        out_layout->required_resources =
            MADEIRA_SE_WINE_RESOURCE_HOST_NTDLL
            | MADEIRA_SE_WINE_RESOURCE_HOST_LOADER
            | MADEIRA_SE_WINE_RESOURCE_HOST_SERVER
            | MADEIRA_SE_WINE_RESOURCE_GUEST_NTDLL
            | MADEIRA_SE_WINE_RESOURCE_GUEST_KERNEL32
            | MADEIRA_SE_WINE_RESOURCE_QEMU_BACKEND;
        if (!join_path(out_layout->guest_pe_directory,
                       sizeof(out_layout->guest_pe_directory),
                       runtime_root_utf8, "x86_64-windows")
            || !copy_string(out_layout->windows_guest_system_directory,
                            sizeof(out_layout->windows_guest_system_directory),
                            "drive_c/windows/system32"))
            return MADEIRA_SE_E_INVALID_ARGUMENT;
        qemu_name = "qemu/libqemu-x86_64-softmmu.dylib";
        break;

    default:
        return MADEIRA_SE_E_UNSUPPORTED_ARCHITECTURE;
    }

    if (!join_path(out_layout->qemu_backend_module,
                   sizeof(out_layout->qemu_backend_module),
                   runtime_root_utf8, qemu_name))
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    return MADEIRA_SE_OK;
}

madeira_se_status_t madeira_se_wine_layout_validate(const madeira_se_wine_layout_t *layout,
                                                    uint32_t *out_missing_resources)
{
    uint32_t missing = 0u;

    if (layout == NULL || out_missing_resources == NULL)
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    *out_missing_resources = 0u;
    if (layout->version != MADEIRA_SE_WINE_LAYOUT_VERSION)
        return MADEIRA_SE_E_UNSUPPORTED;
    if ((layout->required_resources & MADEIRA_SE_WINE_RESOURCE_HOST_NTDLL) != 0u
        && !regular_file_exists(layout->native_host_directory, "ntdll.so"))
        missing |= MADEIRA_SE_WINE_RESOURCE_HOST_NTDLL;
    if ((layout->required_resources & MADEIRA_SE_WINE_RESOURCE_HOST_LOADER) != 0u
        && !regular_file_exists(layout->native_host_directory, "wine"))
        missing |= MADEIRA_SE_WINE_RESOURCE_HOST_LOADER;
    if ((layout->required_resources & MADEIRA_SE_WINE_RESOURCE_HOST_SERVER) != 0u
        && !regular_file_exists(layout->native_host_directory, "wineserver"))
        missing |= MADEIRA_SE_WINE_RESOURCE_HOST_SERVER;
    if ((layout->required_resources & MADEIRA_SE_WINE_RESOURCE_GUEST_NTDLL) != 0u
        && !regular_file_exists(layout->guest_pe_directory, "ntdll.dll"))
        missing |= MADEIRA_SE_WINE_RESOURCE_GUEST_NTDLL;
    if ((layout->required_resources & MADEIRA_SE_WINE_RESOURCE_GUEST_KERNEL32) != 0u
        && !regular_file_exists(layout->guest_pe_directory, "kernel32.dll"))
        missing |= MADEIRA_SE_WINE_RESOURCE_GUEST_KERNEL32;
    if ((layout->required_resources & MADEIRA_SE_WINE_RESOURCE_QEMU_BACKEND) != 0u
        && !regular_path_exists(layout->qemu_backend_module))
        missing |= MADEIRA_SE_WINE_RESOURCE_QEMU_BACKEND;

    *out_missing_resources = missing;
    return missing == 0u ? MADEIRA_SE_OK : MADEIRA_SE_E_NOT_READY;
}

const char *madeira_se_wine_profile_string(madeira_se_wine_profile_t profile)
{
    switch (profile) {
    case MADEIRA_SE_WINE_PROFILE_GUEST_I386_TCTI: return "guest-i386-tcti";
    case MADEIRA_SE_WINE_PROFILE_GUEST_AMD64_TCTI: return "guest-amd64-tcti";
    default: return "unknown";
    }
}

size_t madeira_se_wine_format_missing_resources(uint32_t missing_resources,
                                                char *buffer,
                                                size_t buffer_size)
{
    static const struct {
        uint32_t bit;
        const char *name;
    } resources[] = {
        {MADEIRA_SE_WINE_RESOURCE_HOST_NTDLL, "arm64 Mach-O ntdll.so"},
        {MADEIRA_SE_WINE_RESOURCE_HOST_LOADER, "arm64 Mach-O wine loader"},
        {MADEIRA_SE_WINE_RESOURCE_HOST_SERVER, "arm64 Mach-O wineserver"},
        {MADEIRA_SE_WINE_RESOURCE_GUEST_NTDLL, "guest ntdll.dll"},
        {MADEIRA_SE_WINE_RESOURCE_GUEST_KERNEL32, "guest kernel32.dll"},
        {MADEIRA_SE_WINE_RESOURCE_QEMU_BACKEND, "QEMU TCTI backend"},
    };
    size_t used = 0u;
    size_t i;

    if (buffer != NULL && buffer_size > 0u) buffer[0] = '\0';
    for (i = 0u; i < sizeof(resources) / sizeof(resources[0]); ++i) {
        int written;
        if ((missing_resources & resources[i].bit) == 0u) continue;
        written = snprintf(buffer != NULL && used < buffer_size ? buffer + used : NULL,
                           buffer != NULL && used < buffer_size ? buffer_size - used : 0u,
                           "%s%s", used == 0u ? "" : ", ", resources[i].name);
        if (written < 0) break;
        used += (size_t)written;
    }
    return used;
}
