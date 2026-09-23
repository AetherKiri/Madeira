/*
 * End-to-end Madeira-SE QEMU TCTI backend probe.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "madeira_se_qemu.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    TEST_PAGE_SIZE = 4096,
    TEST_GUEST_BASE = 0x100000,
    TEST_SYSCALL_OFFSET = 0x100,
    TEST_UNIX_OFFSET = 0x110,
};

typedef struct test_memory {
    uint64_t guest_base;
    uint8_t *data;
    size_t size;
} test_memory_t;

static void *translate_memory(void *userdata, uint64_t guest_address,
                              size_t size, uint32_t access)
{
    test_memory_t *memory = userdata;
    uint64_t offset;

    (void)access;
    if (guest_address < memory->guest_base) return NULL;
    offset = guest_address - memory->guest_base;
    if (offset > memory->size || size > memory->size - (size_t)offset)
        return NULL;
    return memory->data + offset;
}

static void initialize_context(madeira_se_x86_context_t *context,
                               madeira_se_architecture_t architecture,
                               uint64_t instruction_pointer)
{
    const uint16_t code_segment = architecture == MADEIRA_SE_ARCH_X86_64
        ? 0x33u : 0x23u;
    const uint16_t data_segment = 0x2bu;
    const uint16_t fpu_control = 0x037fu;
    const uint32_t mxcsr = 0x1f80u;

    memset(context, 0, sizeof(*context));
    context->version = MADEIRA_SE_CPU_ABI_VERSION;
    context->architecture = (uint32_t)architecture;
    context->rip = instruction_pointer;
    context->rflags = 0x202u;
    context->segment[MADEIRA_SE_X86_SEGMENT_CS] = code_segment;
    context->segment[MADEIRA_SE_X86_SEGMENT_SS] = data_segment;
    context->segment[MADEIRA_SE_X86_SEGMENT_DS] = data_segment;
    context->segment[MADEIRA_SE_X86_SEGMENT_ES] = data_segment;
    context->segment[MADEIRA_SE_X86_SEGMENT_FS] = data_segment;
    context->segment[MADEIRA_SE_X86_SEGMENT_GS] = data_segment;
    memcpy(context->fxsave, &fpu_control, sizeof(fpu_control));
    memcpy(context->fxsave + 24, &mxcsr, sizeof(mxcsr));
}

static int run_syscall_probe(madeira_se_cpu_t *cpu, test_memory_t *memory,
                             madeira_se_architecture_t architecture)
{
    madeira_se_cpu_run_request_t request = {
        .version = MADEIRA_SE_CPU_ABI_VERSION,
        .max_instructions = 100,
        .syscall_dispatcher = TEST_GUEST_BASE + TEST_SYSCALL_OFFSET,
        .unix_call_dispatcher = TEST_GUEST_BASE + TEST_UNIX_OFFSET,
    };
    madeira_se_cpu_run_result_t result = {
        .version = MADEIRA_SE_CPU_ABI_VERSION,
    };
    madeira_se_x86_context_t context;
    const uint32_t expected_eax = 0x78563412u;
    const int32_t displacement = TEST_SYSCALL_OFFSET - 10;

    memset(memory->data, 0x90, memory->size);
    memory->data[0] = 0xb8;
    memcpy(memory->data + 1, &expected_eax, sizeof(expected_eax));
    memory->data[5] = 0xe9;
    memcpy(memory->data + 6, &displacement, sizeof(displacement));
    initialize_context(&context, architecture, TEST_GUEST_BASE);

    if (madeira_se_cpu_invalidate(cpu, TEST_GUEST_BASE, memory->size)
            != MADEIRA_SE_OK
        || madeira_se_cpu_run(cpu, &request, &context, &result)
            != MADEIRA_SE_OK) {
        return -1;
    }
    if (result.reason != MADEIRA_SE_CPU_EXIT_SYSCALL
        || context.rip != request.syscall_dispatcher
        || (uint32_t)context.gpr[MADEIRA_SE_X86_RAX] != expected_eax
        || result.instructions_executed == 0
        || result.instructions_executed > request.max_instructions) {
        fprintf(stderr,
                "syscall probe mismatch: reason=%u rip=%#llx eax=%#llx count=%llu\n",
                result.reason, (unsigned long long)context.rip,
                (unsigned long long)context.gpr[MADEIRA_SE_X86_RAX],
                (unsigned long long)result.instructions_executed);
        return -1;
    }
    return 0;
}

static int run_unix_call_probe(madeira_se_cpu_t *cpu, test_memory_t *memory,
                               madeira_se_architecture_t architecture)
{
    madeira_se_cpu_run_request_t request = {
        .version = MADEIRA_SE_CPU_ABI_VERSION,
        .max_instructions = 100,
        .syscall_dispatcher = TEST_GUEST_BASE + TEST_SYSCALL_OFFSET,
        .unix_call_dispatcher = TEST_GUEST_BASE + TEST_UNIX_OFFSET,
    };
    madeira_se_cpu_run_result_t result = {
        .version = MADEIRA_SE_CPU_ABI_VERSION,
    };
    madeira_se_x86_context_t context;
    const uint32_t expected_eax = 0x21436587u;
    const int32_t displacement = TEST_UNIX_OFFSET - 10;

    memset(memory->data, 0x90, memory->size);
    memory->data[0] = 0xb8;
    memcpy(memory->data + 1, &expected_eax, sizeof(expected_eax));
    memory->data[5] = 0xe9;
    memcpy(memory->data + 6, &displacement, sizeof(displacement));
    initialize_context(&context, architecture, TEST_GUEST_BASE);

    if (madeira_se_cpu_invalidate(cpu, TEST_GUEST_BASE, memory->size)
            != MADEIRA_SE_OK
        || madeira_se_cpu_run(cpu, &request, &context, &result)
            != MADEIRA_SE_OK) {
        return -1;
    }
    if (result.reason != MADEIRA_SE_CPU_EXIT_UNIX_CALL
        || context.rip != request.unix_call_dispatcher
        || (uint32_t)context.gpr[MADEIRA_SE_X86_RAX] != expected_eax
        || result.instructions_executed == 0
        || result.instructions_executed > request.max_instructions) {
        fprintf(stderr,
                "Unix-call probe mismatch: reason=%u rip=%#llx eax=%#llx count=%llu\n",
                result.reason, (unsigned long long)context.rip,
                (unsigned long long)context.gpr[MADEIRA_SE_X86_RAX],
                (unsigned long long)result.instructions_executed);
        return -1;
    }
    return 0;
}

static int run_invalidation_probe(madeira_se_cpu_t *cpu,
                                  test_memory_t *memory,
                                  madeira_se_architecture_t architecture)
{
    madeira_se_cpu_run_request_t request = {
        .version = MADEIRA_SE_CPU_ABI_VERSION,
        .max_instructions = 100,
        .syscall_dispatcher = TEST_GUEST_BASE + TEST_SYSCALL_OFFSET,
        .unix_call_dispatcher = TEST_GUEST_BASE + TEST_UNIX_OFFSET,
    };
    madeira_se_cpu_run_result_t result = {
        .version = MADEIRA_SE_CPU_ABI_VERSION,
    };
    madeira_se_x86_context_t context;
    const int32_t displacement = TEST_SYSCALL_OFFSET - 10;
    uint32_t value;

    memset(memory->data, 0x90, memory->size);
    memory->data[0] = 0xb8;
    memory->data[5] = 0xe9;
    memcpy(memory->data + 6, &displacement, sizeof(displacement));

    value = 1;
    memcpy(memory->data + 1, &value, sizeof(value));
    initialize_context(&context, architecture, TEST_GUEST_BASE);
    if (madeira_se_cpu_invalidate(cpu, TEST_GUEST_BASE, 10) != MADEIRA_SE_OK
        || madeira_se_cpu_run(cpu, &request, &context, &result)
            != MADEIRA_SE_OK
        || result.reason != MADEIRA_SE_CPU_EXIT_SYSCALL
        || (uint32_t)context.gpr[MADEIRA_SE_X86_RAX] != value) {
        fprintf(stderr, "initial invalidation probe failed\n");
        return -1;
    }

    value = 2;
    memcpy(memory->data + 1, &value, sizeof(value));
    initialize_context(&context, architecture, TEST_GUEST_BASE);
    memset(&result, 0, sizeof(result));
    result.version = MADEIRA_SE_CPU_ABI_VERSION;
    if (madeira_se_cpu_notify_memory(cpu, MADEIRA_SE_CPU_MEMORY_DIRTY,
                                    TEST_GUEST_BASE + 1, sizeof(value), 0)
            != MADEIRA_SE_OK
        || madeira_se_cpu_run(cpu, &request, &context, &result)
            != MADEIRA_SE_OK
        || result.reason != MADEIRA_SE_CPU_EXIT_SYSCALL
        || (uint32_t)context.gpr[MADEIRA_SE_X86_RAX] != value) {
        fprintf(stderr, "dirty-range invalidation probe failed\n");
        return -1;
    }

    value = 3;
    memcpy(memory->data + 1, &value, sizeof(value));
    initialize_context(&context, architecture, TEST_GUEST_BASE);
    memset(&result, 0, sizeof(result));
    result.version = MADEIRA_SE_CPU_ABI_VERSION;
    if (madeira_se_cpu_invalidate(cpu, TEST_GUEST_BASE + 1, sizeof(value))
            != MADEIRA_SE_OK
        || madeira_se_cpu_run(cpu, &request, &context, &result)
            != MADEIRA_SE_OK
        || result.reason != MADEIRA_SE_CPU_EXIT_SYSCALL
        || (uint32_t)context.gpr[MADEIRA_SE_X86_RAX] != value) {
        fprintf(stderr, "explicit range invalidation probe failed\n");
        return -1;
    }
    return 0;
}

static uint64_t performance_budget(void)
{
    const char *value = getenv("MADEIRA_SE_PERF_INSTRUCTIONS");
    char *end = NULL;
    unsigned long long parsed;

    if (value == NULL || *value == '\0') return UINT64_C(2000000);
    errno = 0;
    parsed = strtoull(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed == 0 ||
        parsed > INT64_MAX) {
        fprintf(stderr, "invalid MADEIRA_SE_PERF_INSTRUCTIONS: %s\n", value);
        return 0;
    }
    return (uint64_t)parsed;
}

static int run_performance_probe(madeira_se_cpu_t *cpu, test_memory_t *memory,
                                 madeira_se_architecture_t architecture)
{
    uint64_t target_instructions = performance_budget();
    uint64_t executed = 0;
    madeira_se_cpu_run_request_t request = {
        .version = MADEIRA_SE_CPU_ABI_VERSION,
        .max_instructions = 1,
        .syscall_dispatcher = TEST_GUEST_BASE + TEST_PAGE_SIZE,
        .unix_call_dispatcher = TEST_GUEST_BASE + TEST_PAGE_SIZE + 0x10,
    };
    madeira_se_cpu_run_result_t result = {
        .version = MADEIRA_SE_CPU_ABI_VERSION,
    };
    madeira_se_x86_context_t context;
    struct timespec before;
    struct timespec after;
    double seconds;
    double instructions_per_second;

    if (target_instructions == 0) return -1;
    memory->data[0] = 0xff;
    memory->data[1] = 0xc0;
    memory->data[2] = 0xeb;
    memory->data[3] = 0xfc;
    initialize_context(&context, architecture, TEST_GUEST_BASE);
    if (madeira_se_cpu_invalidate(cpu, TEST_GUEST_BASE, 4) != MADEIRA_SE_OK ||
        clock_gettime(CLOCK_MONOTONIC, &before) != 0) {
        return -1;
    }
    while (executed < target_instructions) {
        uint64_t remaining = target_instructions - executed;

        request.max_instructions = remaining < UINT64_C(2000000)
            ? remaining : UINT64_C(2000000);
        if (executed != 0u)
            request.flags = MADEIRA_SE_CPU_RUN_REUSE_CONTEXT;
        memset(&result, 0, sizeof(result));
        result.version = MADEIRA_SE_CPU_ABI_VERSION;
        if (madeira_se_cpu_run(cpu, &request, &context, &result)
                != MADEIRA_SE_OK ||
            result.reason != MADEIRA_SE_CPU_EXIT_BUDGET ||
            result.instructions_executed == 0 ||
            result.instructions_executed > request.max_instructions) {
            fprintf(stderr,
                    "performance probe mismatch: reason=%u count=%llu limit=%llu\n",
                    result.reason,
                    (unsigned long long)result.instructions_executed,
                    (unsigned long long)request.max_instructions);
            return -1;
        }
        if (executed != 0u &&
            (result.reserved[0] & MADEIRA_SE_CPU_RESULT_CONTEXT_UNCHANGED) == 0u) {
            fprintf(stderr, "performance probe did not reuse resident context\n");
            return -1;
        }
        executed += result.instructions_executed;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &after) != 0) return -1;
    seconds = (double)(after.tv_sec - before.tv_sec) +
        (double)(after.tv_nsec - before.tv_nsec) / 1000000000.0;
    if (seconds <= 0.0) return -1;
    instructions_per_second = (double)executed / seconds;
    printf("MADEIRA_SE_QEMU_PERF %s instructions=%llu seconds=%.6f ips=%.0f\n",
           architecture == MADEIRA_SE_ARCH_X86_64 ? "x86_64" : "i386",
           (unsigned long long)executed, seconds,
           instructions_per_second);
    return 0;
}

static int run_budget_probe(madeira_se_cpu_t *cpu, test_memory_t *memory,
                            madeira_se_architecture_t architecture)
{
    madeira_se_cpu_run_request_t request = {
        .version = MADEIRA_SE_CPU_ABI_VERSION,
        .max_instructions = 17,
        .syscall_dispatcher = TEST_GUEST_BASE + TEST_SYSCALL_OFFSET,
        .unix_call_dispatcher = TEST_GUEST_BASE + TEST_UNIX_OFFSET,
    };
    madeira_se_cpu_run_result_t result = {
        .version = MADEIRA_SE_CPU_ABI_VERSION,
    };
    madeira_se_x86_context_t context;
    uint32_t expected_eax = 0x13579bdfu;
    const int32_t displacement = TEST_SYSCALL_OFFSET - 10;

    memory->data[0] = 0xeb;
    memory->data[1] = 0xfe;
    initialize_context(&context, architecture, TEST_GUEST_BASE);
    if (madeira_se_cpu_invalidate(cpu, TEST_GUEST_BASE, 2) != MADEIRA_SE_OK
        || madeira_se_cpu_run(cpu, &request, &context, &result)
            != MADEIRA_SE_OK) {
        return -1;
    }
    if (result.reason != MADEIRA_SE_CPU_EXIT_BUDGET
        || result.instructions_executed != request.max_instructions) {
        fprintf(stderr, "budget probe mismatch: reason=%u count=%llu\n",
                result.reason,
                (unsigned long long)result.instructions_executed);
        return -1;
    }

    /* A consecutive budget slice may stay resident in QEMU's CPU object.
     * Deliberately poison the caller's copy to prove that the backend does
     * not import it while the reuse contract is active. */
    request.flags = MADEIRA_SE_CPU_RUN_REUSE_CONTEXT;
    context.rip = TEST_GUEST_BASE + 1;
    context.gpr[MADEIRA_SE_X86_RAX] = UINT64_C(0xdeadbeef);
    memset(&result, 0, sizeof(result));
    result.version = MADEIRA_SE_CPU_ABI_VERSION;
    if (madeira_se_cpu_run(cpu, &request, &context, &result) != MADEIRA_SE_OK
        || result.reason != MADEIRA_SE_CPU_EXIT_BUDGET
        || (result.reserved[0] & MADEIRA_SE_CPU_RESULT_CONTEXT_UNCHANGED) == 0u
        || context.rip != TEST_GUEST_BASE + 1
        || context.gpr[MADEIRA_SE_X86_RAX] != UINT64_C(0xdeadbeef)) {
        fprintf(stderr,
                "budget reuse probe mismatch: reason=%u flags=%#llx rip=%#llx eax=%#llx\n",
                result.reason, (unsigned long long)result.reserved[0],
                (unsigned long long)context.rip,
                (unsigned long long)context.gpr[MADEIRA_SE_X86_RAX]);
        return -1;
    }

    /* A dirty code notification followed by a dispatcher exit must force a
     * real context export even though the caller still requests reuse. */
    memory->data[0] = 0xb8;
    memcpy(memory->data + 1, &expected_eax, sizeof(expected_eax));
    memory->data[5] = 0xe9;
    memcpy(memory->data + 6, &displacement, sizeof(displacement));
    if (madeira_se_cpu_notify_memory(cpu, MADEIRA_SE_CPU_MEMORY_DIRTY,
                                    TEST_GUEST_BASE, 10, 0) != MADEIRA_SE_OK)
        return -1;
    memset(&result, 0, sizeof(result));
    result.version = MADEIRA_SE_CPU_ABI_VERSION;
    if (madeira_se_cpu_run(cpu, &request, &context, &result) != MADEIRA_SE_OK
        || result.reason != MADEIRA_SE_CPU_EXIT_SYSCALL
        || (uint32_t)context.gpr[MADEIRA_SE_X86_RAX] != expected_eax
        || context.rip != request.syscall_dispatcher
        || (result.reserved[0] & MADEIRA_SE_CPU_RESULT_CONTEXT_UNCHANGED) != 0u) {
        fprintf(stderr,
                "budget exit probe mismatch: reason=%u flags=%#llx rip=%#llx eax=%#llx\n",
                result.reason, (unsigned long long)result.reserved[0],
                (unsigned long long)context.rip,
                (unsigned long long)context.gpr[MADEIRA_SE_X86_RAX]);
        return -1;
    }
    return 0;
}

static int run_exception_probe(madeira_se_cpu_t *cpu, test_memory_t *memory,
                               madeira_se_architecture_t architecture)
{
    madeira_se_cpu_run_request_t request = {
        .version = MADEIRA_SE_CPU_ABI_VERSION,
        .max_instructions = 16,
        .syscall_dispatcher = TEST_GUEST_BASE + TEST_SYSCALL_OFFSET,
        .unix_call_dispatcher = TEST_GUEST_BASE + TEST_UNIX_OFFSET,
    };
    madeira_se_cpu_run_result_t result = {
        .version = MADEIRA_SE_CPU_ABI_VERSION,
    };
    madeira_se_x86_context_t context;

    memory->data[0] = 0x0f;
    memory->data[1] = 0x0b;
    initialize_context(&context, architecture, TEST_GUEST_BASE);
    if (madeira_se_cpu_invalidate(cpu, TEST_GUEST_BASE, 2) != MADEIRA_SE_OK
        || madeira_se_cpu_run(cpu, &request, &context, &result)
            != MADEIRA_SE_OK) {
        return -1;
    }
    if (result.reason != MADEIRA_SE_CPU_EXIT_EXCEPTION
        || result.exception_vector != 6u) {
        fprintf(stderr, "exception probe mismatch: reason=%u vector=%u\n",
                result.reason, result.exception_vector);
        return -1;
    }
    return 0;
}

static int run_protection_probe(madeira_se_cpu_t *cpu, test_memory_t *memory,
                                madeira_se_architecture_t architecture)
{
    madeira_se_cpu_run_request_t request = {
        .version = MADEIRA_SE_CPU_ABI_VERSION,
        .max_instructions = 100,
        .syscall_dispatcher = TEST_GUEST_BASE + TEST_SYSCALL_OFFSET,
        .unix_call_dispatcher = TEST_GUEST_BASE + TEST_UNIX_OFFSET,
    };
    madeira_se_cpu_run_result_t result = {
        .version = MADEIRA_SE_CPU_ABI_VERSION,
    };
    madeira_se_x86_context_t context;
    const uint32_t target = TEST_GUEST_BASE + 0x200u;
    const int32_t displacement = TEST_SYSCALL_OFFSET - 13;

    memset(memory->data, 0x90, memory->size);
    memory->data[0] = 0xb8;
    memcpy(memory->data + 1, &target, sizeof(target));
    memory->data[5] = 0xc6;
    memory->data[6] = 0x00;
    memory->data[7] = 0x5a;
    memory->data[8] = 0xe9;
    memcpy(memory->data + 9, &displacement, sizeof(displacement));
    memory->data[0x200] = 0;

    if (madeira_se_cpu_notify_memory(
            cpu, MADEIRA_SE_CPU_MEMORY_PROTECT, target, 1,
            MADEIRA_SE_MEMORY_READ) != MADEIRA_SE_OK)
        return -1;
    initialize_context(&context, architecture, TEST_GUEST_BASE);
    if (madeira_se_cpu_invalidate(cpu, TEST_GUEST_BASE, memory->size)
            != MADEIRA_SE_OK
        || madeira_se_cpu_run(cpu, &request, &context, &result)
            != MADEIRA_SE_OK)
        return -1;
    /* A write through a read-only mapping must stop in the guest with a
     * page-protection fault.  It must not escape into a host signal and the
     * byte must remain unchanged. */
    if (result.reason != MADEIRA_SE_CPU_EXIT_EXCEPTION
        || result.exception_vector != 14u
        || result.fault_address != target
        || memory->data[0x200] != 0u) {
        fprintf(stderr,
                "read-only probe mismatch: reason=%u vector=%u fault=%#llx value=%#x\n",
                result.reason, result.exception_vector,
                (unsigned long long)result.fault_address, memory->data[0x200]);
        return -1;
    }

    if (madeira_se_cpu_notify_memory(
            cpu, MADEIRA_SE_CPU_MEMORY_PROTECT, target, 1,
            MADEIRA_SE_MEMORY_READ | MADEIRA_SE_MEMORY_WRITE)
            != MADEIRA_SE_OK)
        return -1;
    memset(&result, 0, sizeof(result));
    result.version = MADEIRA_SE_CPU_ABI_VERSION;
    initialize_context(&context, architecture, TEST_GUEST_BASE);
    if (madeira_se_cpu_invalidate(cpu, TEST_GUEST_BASE, memory->size)
            != MADEIRA_SE_OK
        || madeira_se_cpu_run(cpu, &request, &context, &result)
            != MADEIRA_SE_OK)
        return -1;
    if (result.reason != MADEIRA_SE_CPU_EXIT_SYSCALL
        || memory->data[0x200] != 0x5au) {
        fprintf(stderr, "writable probe mismatch: reason=%u value=%#x\n",
                result.reason, memory->data[0x200]);
        return -1;
    }
    /* Coalesce the deliberately fragmented protection mapping so later
     * probes exercise the normal page-backed translation path. */
    if (madeira_se_cpu_notify_memory(
            cpu, MADEIRA_SE_CPU_MEMORY_MAP, memory->guest_base, memory->size,
            MADEIRA_SE_MEMORY_READ | MADEIRA_SE_MEMORY_WRITE |
            MADEIRA_SE_MEMORY_EXECUTE) != MADEIRA_SE_OK) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    madeira_se_cpu_backend_t backend;
    madeira_se_memory_t memory_interface;
    madeira_se_cpu_t *cpu = NULL;
    madeira_se_architecture_t architecture;
    test_memory_t memory;
    madeira_se_status_t status;
    int result = 1;

    if (argc != 2) {
        fprintf(stderr, "usage: %s qemu-dylib\n", argv[0]);
        return 2;
    }
    status = madeira_se_qemu_tcti_start(argv[1], &backend);
    if (status != MADEIRA_SE_OK) {
        fprintf(stderr, "QEMU loader failed: %s\n",
                madeira_se_status_string(status));
        return 1;
    }
    architecture = (backend.capabilities & MADEIRA_SE_CPU_CAP_X86_64) != 0u
        ? MADEIRA_SE_ARCH_X86_64 : MADEIRA_SE_ARCH_X86_32;
    memory.guest_base = TEST_GUEST_BASE;
    memory.size = TEST_PAGE_SIZE;
    memory.data = aligned_alloc(TEST_PAGE_SIZE, TEST_PAGE_SIZE);
    if (memory.data == NULL) {
        fprintf(stderr, "test memory allocation failed\n");
        return 1;
    }
    memset(&memory_interface, 0, sizeof(memory_interface));
    memory_interface.version = MADEIRA_SE_CPU_ABI_VERSION;
    memory_interface.userdata = &memory;
    memory_interface.translate = translate_memory;
    status = madeira_se_cpu_create(&backend, architecture, &memory_interface,
                                   &cpu);
    if (status != MADEIRA_SE_OK) {
        fprintf(stderr, "CPU creation failed: %s\n",
                madeira_se_status_string(status));
        goto done;
    }
    status = madeira_se_cpu_notify_memory(
        cpu, MADEIRA_SE_CPU_MEMORY_MAP, memory.guest_base, memory.size,
        MADEIRA_SE_MEMORY_READ | MADEIRA_SE_MEMORY_WRITE |
        MADEIRA_SE_MEMORY_EXECUTE);
    if (status != MADEIRA_SE_OK) {
        fprintf(stderr, "memory map failed: %s\n",
                madeira_se_status_string(status));
        goto done;
    }
    if (run_syscall_probe(cpu, &memory, architecture) != 0
        || run_unix_call_probe(cpu, &memory, architecture) != 0
        || run_invalidation_probe(cpu, &memory, architecture) != 0
        || run_budget_probe(cpu, &memory, architecture) != 0
        || run_protection_probe(cpu, &memory, architecture) != 0
        || run_exception_probe(cpu, &memory, architecture) != 0
        || run_performance_probe(cpu, &memory, architecture) != 0) {
        goto done;
    }
    result = 0;
    printf("MADEIRA_SE_QEMU_BACKEND_OK %s\n", backend.name);

done:
    if (cpu != NULL) {
        (void)madeira_se_cpu_notify_memory(
            cpu, MADEIRA_SE_CPU_MEMORY_UNMAP, memory.guest_base, memory.size, 0);
        madeira_se_cpu_destroy(cpu);
    }
    free(memory.data);
    return result;
}
