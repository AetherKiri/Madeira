/*
 * Madeira-SE biased WoW64 address conversion unit tests.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "wine/madeira_se.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

int main(void)
{
    const uint32_t guest_address = 0x12345000u;
    const uintptr_t biased_address =
        (uintptr_t)MADEIRA_SE_WOW64_GUEST_BIAS + guest_address;

    CHECK(madeira_se_wow64_guest_to_host(0) == NULL);
    CHECK((uintptr_t)madeira_se_wow64_guest_to_host(1) == 1u);
    CHECK((uintptr_t)madeira_se_wow64_guest_to_host(0xffffu) == 0xffffu);
    CHECK((uintptr_t)madeira_se_wow64_guest_to_host(0x10000u) ==
          (uintptr_t)MADEIRA_SE_WOW64_GUEST_BIAS + 0x10000u);
    CHECK((uintptr_t)madeira_se_wow64_guest_to_host(guest_address) == biased_address);
    CHECK(madeira_se_wow64_host_to_guest((const void *)biased_address) == guest_address);
    CHECK(madeira_se_wow64_host_to_guest((const void *)(uintptr_t)1u) == 1u);
    CHECK(madeira_se_wow64_widen_integer(0x00080000u) == (intptr_t)0x00080000);
    CHECK(madeira_se_wow64_widen_integer(0x7fffffffu) == (intptr_t)0x7fffffff);
    CHECK(madeira_se_wow64_widen_integer(0xffffffffu) == (intptr_t)-1);
    CHECK(madeira_se_wow64_widen_integer(0x80000000u) == (intptr_t)INT32_MIN);

    puts("Madeira-SE WoW64 address tests passed");
    return 0;
}
