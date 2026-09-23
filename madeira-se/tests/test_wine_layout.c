/*
 * Madeira-SE Wine resource layout unit tests.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "madeira_se_wine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

static int make_path(char *path, size_t capacity,
                     const char *directory, const char *name)
{
    int length = snprintf(path, capacity, "%s/%s", directory, name);
    return length >= 0 && (size_t)length < capacity ? 0 : 1;
}

static int create_directory(const char *root, const char *name, char *out, size_t out_size)
{
    if (make_path(out, out_size, root, name) != 0) return 1;
    return mkdir(out, 0700) == 0 ? 0 : 1;
}

static int create_file(const char *directory, const char *name)
{
    char path[MADEIRA_SE_WINE_PATH_CAPACITY];
    FILE *file;

    if (make_path(path, sizeof(path), directory, name) != 0) return 1;
    file = fopen(path, "wb");
    if (file == NULL) return 1;
    if (fputc(0, file) == EOF) {
        (void)fclose(file);
        return 1;
    }
    return fclose(file) == 0 ? 0 : 1;
}

static void remove_file(const char *directory, const char *name)
{
    char path[MADEIRA_SE_WINE_PATH_CAPACITY];
    if (make_path(path, sizeof(path), directory, name) == 0) (void)unlink(path);
}

static int test_x86_32_layout(void)
{
    char root[] = "/tmp/madeira-se-wine32-XXXXXX";
    char host_dir[MADEIRA_SE_WINE_PATH_CAPACITY];
    char guest_dir[MADEIRA_SE_WINE_PATH_CAPACITY];
    char qemu_dir[MADEIRA_SE_WINE_PATH_CAPACITY];
    char message[256];
    madeira_se_wine_layout_t layout;
    uint32_t missing;

    CHECK(mkdtemp(root) != NULL);
    CHECK(madeira_se_wine_layout_init(root, MADEIRA_SE_ARCH_X86_32, &layout)
          == MADEIRA_SE_OK);
    CHECK(layout.profile == MADEIRA_SE_WINE_PROFILE_GUEST_I386_TCTI);
    CHECK(strcmp(layout.windows_guest_system_directory,
                 "drive_c/windows/syswow64") == 0);
    CHECK(strstr(layout.guest_pe_directory, "/i386-windows") != NULL);
    CHECK(strstr(layout.native_host_directory, "/host-arm64") != NULL);
    CHECK(strstr(layout.qemu_backend_module,
                 "/qemu/libqemu-i386-softmmu.dylib") != NULL);
    CHECK(madeira_se_wine_layout_validate(&layout, &missing) == MADEIRA_SE_E_NOT_READY);
    CHECK((missing & MADEIRA_SE_WINE_RESOURCE_GUEST_NTDLL) != 0u);
    CHECK((missing & MADEIRA_SE_WINE_RESOURCE_QEMU_BACKEND) != 0u);
    CHECK(madeira_se_wine_format_missing_resources(missing, message, sizeof(message)) > 0u);
    CHECK(strstr(message, "guest ntdll.dll") != NULL);

    CHECK(create_directory(root, "host-arm64", host_dir, sizeof(host_dir)) == 0);
    CHECK(create_directory(root, "i386-windows", guest_dir, sizeof(guest_dir)) == 0);
    CHECK(create_directory(root, "qemu", qemu_dir, sizeof(qemu_dir)) == 0);
    CHECK(create_file(host_dir, "ntdll.so") == 0);
    CHECK(create_file(host_dir, "wine") == 0);
    CHECK(create_file(host_dir, "wineserver") == 0);
    CHECK(create_file(guest_dir, "ntdll.dll") == 0);
    CHECK(create_file(guest_dir, "kernel32.dll") == 0);
    CHECK(create_file(qemu_dir, "libqemu-i386-softmmu.dylib") == 0);
    CHECK(madeira_se_wine_layout_validate(&layout, &missing) == MADEIRA_SE_OK);
    CHECK(missing == 0u);

    remove_file(guest_dir, "kernel32.dll");
    CHECK(madeira_se_wine_layout_validate(&layout, &missing) == MADEIRA_SE_E_NOT_READY);
    CHECK(missing == MADEIRA_SE_WINE_RESOURCE_GUEST_KERNEL32);

    remove_file(host_dir, "ntdll.so");
    remove_file(host_dir, "wine");
    remove_file(host_dir, "wineserver");
    remove_file(guest_dir, "ntdll.dll");
    remove_file(qemu_dir, "libqemu-i386-softmmu.dylib");
    (void)rmdir(host_dir);
    (void)rmdir(guest_dir);
    (void)rmdir(qemu_dir);
    (void)rmdir(root);
    return 0;
}

static int test_x86_64_layout(void)
{
    char root[] = "/tmp/madeira-se-wine64-XXXXXX";
    char host_dir[MADEIRA_SE_WINE_PATH_CAPACITY];
    char guest_dir[MADEIRA_SE_WINE_PATH_CAPACITY];
    char qemu_dir[MADEIRA_SE_WINE_PATH_CAPACITY];
    madeira_se_wine_layout_t layout;
    uint32_t missing;

    CHECK(mkdtemp(root) != NULL);
    CHECK(madeira_se_wine_layout_init(root, MADEIRA_SE_ARCH_X86_64, &layout)
          == MADEIRA_SE_OK);
    CHECK(layout.profile == MADEIRA_SE_WINE_PROFILE_GUEST_AMD64_TCTI);
    CHECK(strcmp(layout.windows_guest_system_directory,
                 "drive_c/windows/system32") == 0);
    CHECK(strstr(layout.guest_pe_directory, "/x86_64-windows") != NULL);
    CHECK(strstr(layout.qemu_backend_module,
                 "/qemu/libqemu-x86_64-softmmu.dylib") != NULL);

    CHECK(create_directory(root, "host-arm64", host_dir, sizeof(host_dir)) == 0);
    CHECK(create_directory(root, "x86_64-windows", guest_dir, sizeof(guest_dir)) == 0);
    CHECK(create_directory(root, "qemu", qemu_dir, sizeof(qemu_dir)) == 0);
    CHECK(create_file(host_dir, "ntdll.so") == 0);
    CHECK(create_file(host_dir, "wine") == 0);
    CHECK(create_file(host_dir, "wineserver") == 0);
    CHECK(create_file(guest_dir, "ntdll.dll") == 0);
    CHECK(create_file(guest_dir, "kernel32.dll") == 0);
    CHECK(create_file(qemu_dir, "libqemu-x86_64-softmmu.dylib") == 0);
    CHECK(madeira_se_wine_layout_validate(&layout, &missing) == MADEIRA_SE_OK);

    remove_file(host_dir, "ntdll.so");
    remove_file(host_dir, "wine");
    remove_file(host_dir, "wineserver");
    remove_file(guest_dir, "ntdll.dll");
    remove_file(guest_dir, "kernel32.dll");
    remove_file(qemu_dir, "libqemu-x86_64-softmmu.dylib");
    (void)rmdir(host_dir);
    (void)rmdir(guest_dir);
    (void)rmdir(qemu_dir);
    (void)rmdir(root);
    return 0;
}

static int test_layout_validation(void)
{
    madeira_se_wine_layout_t layout;
    char too_long[MADEIRA_SE_WINE_PATH_CAPACITY + 32u];

    memset(too_long, 'a', sizeof(too_long));
    too_long[sizeof(too_long) - 1u] = '\0';
    CHECK(madeira_se_wine_layout_init(NULL, MADEIRA_SE_ARCH_X86_32, &layout)
          == MADEIRA_SE_E_INVALID_ARGUMENT);
    CHECK(madeira_se_wine_layout_init("/tmp", MADEIRA_SE_ARCH_UNKNOWN, &layout)
          == MADEIRA_SE_E_UNSUPPORTED_ARCHITECTURE);
    CHECK(madeira_se_wine_layout_init(too_long, MADEIRA_SE_ARCH_X86_32, &layout)
          == MADEIRA_SE_E_INVALID_ARGUMENT);
    return 0;
}

int main(void)
{
    CHECK(test_x86_32_layout() == 0);
    CHECK(test_x86_64_layout() == 0);
    CHECK(test_layout_validation() == 0);
    puts("Madeira-SE Wine layout tests passed");
    return 0;
}
