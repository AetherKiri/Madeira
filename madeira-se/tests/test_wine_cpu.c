/*
 * Madeira-SE Wine CPU-provider transport tests.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "madeira_se_wine_cpu.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

static void init_message(void *message, size_t size)
{
    madeira_se_wine_cpu_message_header_t *header = message;
    memset(message, 0, size);
    header->version = MADEIRA_SE_WINE_CPU_ABI_VERSION;
    header->size = (uint32_t)size;
}

static int test_wire_layout(void)
{
    CHECK(sizeof(madeira_se_wine_cpu_message_header_t) == 8u);
    CHECK(sizeof(madeira_se_wine_cpu_query_message_t) == 24u);
    CHECK(sizeof(madeira_se_wine_cpu_process_init_message_t) == 32u);
    CHECK(sizeof(madeira_se_wine_cpu_process_term_message_t) == 24u);
    CHECK(sizeof(madeira_se_wine_cpu_thread_init_message_t) == 32u);
    CHECK(sizeof(madeira_se_wine_cpu_thread_term_message_t) == 24u);
    CHECK(sizeof(madeira_se_wine_cpu_memory_message_t) == 40u);
    CHECK(sizeof(madeira_se_wine_cpu_invalidate_message_t) == 32u);
    CHECK(sizeof(madeira_se_wine_cpu_interrupt_message_t) == 16u);
    CHECK(sizeof(madeira_se_wine_i386_context_t) == 856u);
    CHECK(sizeof(madeira_se_wine_amd64_context_t) == sizeof(madeira_se_x86_context_t));
    CHECK(madeira_se_wine_cpu_message_size(MADEIRA_SE_WINE_CPU_RUN)
          == sizeof(madeira_se_wine_cpu_run_message_t));
    CHECK(madeira_se_wine_cpu_message_size(999u) == 0u);
    return 0;
}

static int test_i386_context(void)
{
    madeira_se_wine_i386_context_t input;
    madeira_se_wine_i386_context_t output;
    madeira_se_x86_context_t canonical;
    size_t i;

    memset(&input, 0, sizeof(input));
    memset(&canonical, 0, sizeof(canonical));
    input.version = MADEIRA_SE_WINE_CPU_ABI_VERSION;
    input.valid_fields = MADEIRA_SE_WINE_CONTEXT_ALL;
    for (i = 0u; i < 8u; ++i) {
        input.gpr[i] = 0x1000u + (uint32_t)i;
        input.debug_register[i] = 0x2000u + (uint32_t)i;
    }
    for (i = 0u; i < MADEIRA_SE_X86_SEGMENT_COUNT; ++i) {
        input.segment[i] = (uint16_t)(0x30u + i);
        input.segment_base[i] = 0x30000000u + i * 0x1000u;
    }
    input.eip = 0x401234u;
    input.eflags = 0x202u;
    input.fault_address = 0x501000u;
    for (i = 0u; i < sizeof(input.fxsave); ++i)
        input.fxsave[i] = (uint8_t)(i ^ 0x5au);
    for (i = 0u; i < sizeof(input.ymm_hi); ++i)
        ((uint8_t *)input.ymm_hi)[i] = (uint8_t)(i ^ 0xa5u);

    CHECK(madeira_se_wine_i386_context_apply(&input, &canonical) == MADEIRA_SE_OK);
    CHECK(canonical.version == MADEIRA_SE_CPU_ABI_VERSION);
    CHECK(canonical.architecture == (uint32_t)MADEIRA_SE_ARCH_X86_32);
    CHECK(canonical.gpr[MADEIRA_SE_X86_RAX] == input.gpr[0]);
    CHECK(canonical.gpr[MADEIRA_SE_X86_RDI] == input.gpr[7]);
    CHECK(canonical.gpr[MADEIRA_SE_X86_R8] == 0u);
    CHECK(canonical.rip == input.eip);
    CHECK(canonical.rflags == input.eflags);
    CHECK(canonical.segment[MADEIRA_SE_X86_SEGMENT_FS]
          == input.segment[MADEIRA_SE_X86_SEGMENT_FS]);
    CHECK(canonical.segment_base[MADEIRA_SE_X86_SEGMENT_GS]
          == input.segment_base[MADEIRA_SE_X86_SEGMENT_GS]);
    CHECK(memcmp(canonical.fxsave, input.fxsave, sizeof(input.fxsave)) == 0);
    CHECK(memcmp(canonical.ymm_hi, input.ymm_hi, sizeof(input.ymm_hi)) == 0);

    CHECK(madeira_se_wine_i386_context_capture(&canonical,
                                               MADEIRA_SE_WINE_CONTEXT_ALL,
                                               &output) == MADEIRA_SE_OK);
    CHECK(output.valid_fields == MADEIRA_SE_WINE_CONTEXT_ALL);
    CHECK(memcmp(output.gpr, input.gpr, sizeof(input.gpr)) == 0);
    CHECK(output.eip == input.eip);
    CHECK(output.eflags == input.eflags);
    CHECK(memcmp(output.segment, input.segment, sizeof(input.segment)) == 0);
    CHECK(memcmp(output.segment_base, input.segment_base,
                 sizeof(input.segment_base)) == 0);
    CHECK(memcmp(output.debug_register, input.debug_register,
                 sizeof(input.debug_register)) == 0);
    CHECK(memcmp(output.fxsave, input.fxsave, sizeof(input.fxsave)) == 0);
    CHECK(memcmp(output.ymm_hi, input.ymm_hi, sizeof(input.ymm_hi)) == 0);

    canonical.gpr[MADEIRA_SE_X86_RAX] = 0xfeedfaceu;
    input.valid_fields = MADEIRA_SE_WINE_CONTEXT_CONTROL;
    input.gpr[0] = 1u;
    input.eip = 0x7777u;
    CHECK(madeira_se_wine_i386_context_apply(&input, &canonical) == MADEIRA_SE_OK);
    CHECK(canonical.gpr[MADEIRA_SE_X86_RAX] == 0xfeedfaceu);
    CHECK(canonical.rip == 0x7777u);
    return 0;
}

static int test_amd64_context(void)
{
    madeira_se_wine_amd64_context_t input;
    madeira_se_wine_amd64_context_t output;
    madeira_se_x86_context_t canonical;
    size_t i;

    memset(&input, 0, sizeof(input));
    memset(&canonical, 0, sizeof(canonical));
    input.version = MADEIRA_SE_WINE_CPU_ABI_VERSION;
    input.valid_fields = MADEIRA_SE_WINE_CONTEXT_ALL;
    for (i = 0u; i < MADEIRA_SE_X86_REGISTER_COUNT; ++i)
        input.gpr[i] = 0x100000000ull + i;
    for (i = 0u; i < MADEIRA_SE_X86_SEGMENT_COUNT; ++i) {
        input.segment[i] = (uint16_t)(0x40u + i);
        input.segment_base[i] = 0x700000000000ull + i * 0x1000u;
    }
    for (i = 0u; i < 8u; ++i) input.debug_register[i] = 0x8000u + i;
    input.rip = 0x140001000ull;
    input.rflags = 0x246u;
    input.fault_address = 0x140002000ull;
    memset(input.fxsave, 0x35, sizeof(input.fxsave));
    memset(input.ymm_hi, 0x53, sizeof(input.ymm_hi));

    CHECK(madeira_se_wine_amd64_context_apply(&input, &canonical) == MADEIRA_SE_OK);
    CHECK(canonical.architecture == (uint32_t)MADEIRA_SE_ARCH_X86_64);
    CHECK(canonical.gpr[MADEIRA_SE_X86_R15] == input.gpr[MADEIRA_SE_X86_R15]);
    CHECK(canonical.rip == input.rip);
    CHECK(madeira_se_wine_amd64_context_capture(&canonical,
                                                MADEIRA_SE_WINE_CONTEXT_ALL,
                                                &output) == MADEIRA_SE_OK);
    CHECK(memcmp(output.gpr, input.gpr, sizeof(input.gpr)) == 0);
    CHECK(output.rip == input.rip);
    CHECK(output.rflags == input.rflags);
    CHECK(memcmp(output.fxsave, input.fxsave, sizeof(input.fxsave)) == 0);
    CHECK(memcmp(output.ymm_hi, input.ymm_hi, sizeof(input.ymm_hi)) == 0);
    return 0;
}

struct fake_endpoint {
    unsigned calls[MADEIRA_SE_WINE_CPU_INTERRUPT + 1];
    uint32_t capabilities;
    uint32_t last_memory_event;
    uint64_t last_memory_address;
};

static int32_t fake_dispatch(void *userdata, uint32_t operation,
                             void *message, uint32_t message_size)
{
    struct fake_endpoint *fake = userdata;
    CHECK(message_size == madeira_se_wine_cpu_message_size(operation));
    fake->calls[operation]++;
    switch (operation) {
    case MADEIRA_SE_WINE_CPU_QUERY: {
        madeira_se_wine_cpu_query_message_t *params = message;
        params->provider_version = MADEIRA_SE_WINE_CPU_ABI_VERSION;
        params->capabilities = fake->capabilities;
        break;
    }
    case MADEIRA_SE_WINE_CPU_PROCESS_INIT:
        ((madeira_se_wine_cpu_process_init_message_t *)message)->process_handle = 0x101u;
        break;
    case MADEIRA_SE_WINE_CPU_THREAD_INIT:
        ((madeira_se_wine_cpu_thread_init_message_t *)message)->thread_handle = 0x202u;
        break;
    case MADEIRA_SE_WINE_CPU_RUN: {
        madeira_se_wine_cpu_run_message_t *params = message;
        params->context.version = MADEIRA_SE_CPU_ABI_VERSION;
        params->context.gpr[MADEIRA_SE_X86_RAX] = 0x1234u;
        params->result.version = MADEIRA_SE_CPU_ABI_VERSION;
        params->result.reason = MADEIRA_SE_CPU_EXIT_SYSCALL;
        params->result.service_number = 0x55u;
        break;
    }
    case MADEIRA_SE_WINE_CPU_MEMORY_EVENT: {
        const madeira_se_wine_cpu_memory_message_t *params = message;
        fake->last_memory_event = params->event;
        fake->last_memory_address = params->guest_address;
        break;
    }
    default:
        break;
    }
    return MADEIRA_SE_OK;
}

static int test_dispatch(void)
{
    struct fake_endpoint fake;
    madeira_se_wine_cpu_endpoint_t endpoint;
    madeira_se_wine_cpu_query_message_t query;
    madeira_se_wine_cpu_process_init_message_t process;
    madeira_se_wine_cpu_thread_init_message_t thread;
    madeira_se_wine_cpu_run_message_t run;
    madeira_se_wine_cpu_memory_message_t memory;

    memset(&fake, 0, sizeof(fake));
    memset(&endpoint, 0, sizeof(endpoint));
    fake.capabilities = MADEIRA_SE_CPU_CAP_NO_RUNTIME_CODEGEN
        | MADEIRA_SE_CPU_CAP_X86_32 | MADEIRA_SE_CPU_CAP_X86_64;
    endpoint.version = MADEIRA_SE_WINE_CPU_ABI_VERSION;
    endpoint.size = sizeof(endpoint);
    endpoint.userdata = &fake;
    endpoint.dispatch = fake_dispatch;

    init_message(&query, sizeof(query));
    CHECK(madeira_se_wine_cpu_dispatch(&endpoint, MADEIRA_SE_WINE_CPU_QUERY,
                                       &query, sizeof(query)) == MADEIRA_SE_OK);
    CHECK(query.capabilities == fake.capabilities);

    init_message(&process, sizeof(process));
    process.architecture = MADEIRA_SE_ARCH_X86_32;
    process.flags = MADEIRA_SE_WINE_CPU_PROCESS_DIRECT_ADDRESS_SPACE;
    CHECK(madeira_se_wine_cpu_dispatch(&endpoint, MADEIRA_SE_WINE_CPU_PROCESS_INIT,
                                       &process, sizeof(process)) == MADEIRA_SE_OK);
    CHECK(process.process_handle == 0x101u);

    init_message(&process, sizeof(process));
    process.architecture = MADEIRA_SE_ARCH_X86_32;
    process.flags = MADEIRA_SE_WINE_CPU_PROCESS_BIASED_ADDRESS_SPACE;
    process.guest_address_bias = UINT64_C(0x7000000000);
    CHECK(madeira_se_wine_cpu_validate_message(
              MADEIRA_SE_WINE_CPU_PROCESS_INIT, &process, sizeof(process))
          == MADEIRA_SE_OK);
    process.guest_address_bias++;
    CHECK(madeira_se_wine_cpu_validate_message(
              MADEIRA_SE_WINE_CPU_PROCESS_INIT, &process, sizeof(process))
          == MADEIRA_SE_E_INVALID_ARGUMENT);
    process.guest_address_bias = UINT64_C(0x7000000000);
    process.architecture = MADEIRA_SE_ARCH_X86_64;
    CHECK(madeira_se_wine_cpu_validate_message(
              MADEIRA_SE_WINE_CPU_PROCESS_INIT, &process, sizeof(process))
          == MADEIRA_SE_E_INVALID_ARGUMENT);

    process.flags = MADEIRA_SE_WINE_CPU_PROCESS_SPLIT_LOW_4G_ADDRESS_SPACE;
    CHECK(madeira_se_wine_cpu_validate_message(
              MADEIRA_SE_WINE_CPU_PROCESS_INIT, &process, sizeof(process))
          == MADEIRA_SE_OK);
    process.architecture = MADEIRA_SE_ARCH_X86_32;
    CHECK(madeira_se_wine_cpu_validate_message(
              MADEIRA_SE_WINE_CPU_PROCESS_INIT, &process, sizeof(process))
          == MADEIRA_SE_E_INVALID_ARGUMENT);

    init_message(&process, sizeof(process));
    process.architecture = MADEIRA_SE_ARCH_X86_32;
    process.flags = MADEIRA_SE_WINE_CPU_PROCESS_DIRECT_ADDRESS_SPACE;
    process.guest_address_bias = UINT64_C(0x100000000);
    CHECK(madeira_se_wine_cpu_validate_message(
              MADEIRA_SE_WINE_CPU_PROCESS_INIT, &process, sizeof(process))
          == MADEIRA_SE_E_INVALID_ARGUMENT);

    init_message(&process, sizeof(process));
    process.architecture = MADEIRA_SE_ARCH_X86_32;
    process.flags = MADEIRA_SE_WINE_CPU_PROCESS_DIRECT_ADDRESS_SPACE;
    CHECK(madeira_se_wine_cpu_dispatch(&endpoint,
                                       MADEIRA_SE_WINE_CPU_PROCESS_INIT,
                                       &process, sizeof(process)) == MADEIRA_SE_OK);

    init_message(&thread, sizeof(thread));
    thread.process_handle = process.process_handle;
    thread.thread_id = 7u;
    CHECK(madeira_se_wine_cpu_dispatch(&endpoint, MADEIRA_SE_WINE_CPU_THREAD_INIT,
                                       &thread, sizeof(thread)) == MADEIRA_SE_OK);
    CHECK(thread.thread_handle == 0x202u);

    init_message(&run, sizeof(run));
    run.thread_handle = thread.thread_handle;
    run.request.version = MADEIRA_SE_CPU_ABI_VERSION;
    run.request.max_instructions = 1000u;
    run.request.syscall_dispatcher = 0x70000000u;
    run.request.unix_call_dispatcher = 0x70000002u;
    run.context.version = MADEIRA_SE_CPU_ABI_VERSION;
    run.context.architecture = MADEIRA_SE_ARCH_X86_32;
    run.result.version = MADEIRA_SE_CPU_ABI_VERSION;
    CHECK(madeira_se_wine_cpu_dispatch(&endpoint, MADEIRA_SE_WINE_CPU_RUN,
                                       &run, sizeof(run)) == MADEIRA_SE_OK);
    CHECK(run.context.gpr[MADEIRA_SE_X86_RAX] == 0x1234u);
    CHECK(run.result.reason == (uint32_t)MADEIRA_SE_CPU_EXIT_SYSCALL);
    CHECK(run.result.service_number == 0x55u);

    init_message(&memory, sizeof(memory));
    memory.process_handle = process.process_handle;
    memory.event = MADEIRA_SE_CPU_MEMORY_PROTECT;
    memory.protection = MADEIRA_SE_MEMORY_READ | MADEIRA_SE_MEMORY_EXECUTE;
    memory.guest_address = 0x400000u;
    memory.size = 0x1000u;
    CHECK(madeira_se_wine_cpu_dispatch(&endpoint, MADEIRA_SE_WINE_CPU_MEMORY_EVENT,
                                       &memory, sizeof(memory)) == MADEIRA_SE_OK);
    CHECK(fake.last_memory_event == (uint32_t)MADEIRA_SE_CPU_MEMORY_PROTECT);
    CHECK(fake.last_memory_address == 0x400000u);

    memory.header.version++;
    CHECK(madeira_se_wine_cpu_dispatch(&endpoint, MADEIRA_SE_WINE_CPU_MEMORY_EVENT,
                                       &memory, sizeof(memory)) == MADEIRA_SE_E_UNSUPPORTED);
    CHECK(fake.calls[MADEIRA_SE_WINE_CPU_MEMORY_EVENT] == 1u);

    init_message(&query, sizeof(query));
    fake.capabilities = MADEIRA_SE_CPU_CAP_X86_32;
    CHECK(madeira_se_wine_cpu_dispatch(&endpoint, MADEIRA_SE_WINE_CPU_QUERY,
                                       &query, sizeof(query)) == MADEIRA_SE_E_UNSUPPORTED);
    query.header.size--;
    CHECK(madeira_se_wine_cpu_validate_message(MADEIRA_SE_WINE_CPU_QUERY,
                                               &query, sizeof(query))
          == MADEIRA_SE_E_INVALID_ARGUMENT);
    return 0;
}

int main(void)
{
    CHECK(test_wire_layout() == 0);
    CHECK(test_i386_context() == 0);
    CHECK(test_amd64_context() == 0);
    CHECK(test_dispatch() == 0);
    puts("Madeira-SE Wine CPU transport tests passed");
    return 0;
}
