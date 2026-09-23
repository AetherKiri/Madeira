/*
 * Madeira-SE CPU boundary unit tests.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "madeira_se_cpu.h"

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

struct fake_memory {
    uint8_t bytes[64];
    unsigned read_count;
    unsigned write_count;
};

struct fake_backend {
    madeira_se_memory_t memory;
    unsigned create_count;
    unsigned run_count;
    unsigned interrupt_count;
    unsigned memory_event_count;
    unsigned invalidate_count;
    unsigned destroy_count;
    int invalid_exit_reason;
};

static int memory_read(void *userdata, uint64_t address, void *destination,
                       size_t size, uint32_t access)
{
    struct fake_memory *memory = (struct fake_memory *)userdata;
    if (destination == NULL || address > sizeof(memory->bytes)
        || size > sizeof(memory->bytes) - (size_t)address
        || (access & MADEIRA_SE_MEMORY_READ) == 0u)
        return 1;
    memcpy(destination, memory->bytes + (size_t)address, size);
    memory->read_count++;
    return 0;
}

static int memory_write(void *userdata, uint64_t address, const void *source,
                        size_t size, uint32_t access)
{
    struct fake_memory *memory = (struct fake_memory *)userdata;
    if (source == NULL || address > sizeof(memory->bytes)
        || size > sizeof(memory->bytes) - (size_t)address
        || (access & MADEIRA_SE_MEMORY_WRITE) == 0u)
        return 1;
    memcpy(memory->bytes + (size_t)address, source, size);
    memory->write_count++;
    return 0;
}

static int backend_create(void *userdata, madeira_se_architecture_t architecture,
                          const madeira_se_memory_t *memory, void **out_instance)
{
    struct fake_backend *backend = (struct fake_backend *)userdata;
    if (architecture != MADEIRA_SE_ARCH_X86_32 || memory == NULL || out_instance == NULL)
        return 1;
    backend->memory = *memory;
    backend->create_count++;
    *out_instance = backend;
    return 0;
}

static int backend_run(void *userdata, void *instance,
                       const madeira_se_cpu_run_request_t *request,
                       madeira_se_x86_context_t *context,
                       madeira_se_cpu_run_result_t *result)
{
    struct fake_backend *backend = (struct fake_backend *)userdata;
    uint32_t value = 0u;

    if (instance != backend || request->max_instructions != 25u
        || context->architecture != MADEIRA_SE_ARCH_X86_32)
        return 1;
    if (backend->memory.read(backend->memory.userdata, 4u, &value, sizeof(value),
                             MADEIRA_SE_MEMORY_READ) != 0)
        return 1;
    context->gpr[MADEIRA_SE_X86_RAX] = value;
    context->rip += 2u;
    result->reason = backend->invalid_exit_reason
        ? MADEIRA_SE_CPU_EXIT_NONE : MADEIRA_SE_CPU_EXIT_SYSCALL;
    result->instructions_executed = 1u;
    result->service_number = value;
    backend->run_count++;
    return 0;
}

static int backend_interrupt(void *userdata, void *instance)
{
    struct fake_backend *backend = (struct fake_backend *)userdata;
    if (instance != backend) return 1;
    backend->interrupt_count++;
    return 0;
}

static int backend_memory_event(void *userdata, void *instance,
                                madeira_se_cpu_memory_event_t event,
                                uint64_t address, uint64_t size, uint32_t protection)
{
    struct fake_backend *backend = (struct fake_backend *)userdata;
    if (instance != backend || event != MADEIRA_SE_CPU_MEMORY_PROTECT
        || address != 0x1000u || size != 0x2000u
        || protection != (MADEIRA_SE_MEMORY_READ | MADEIRA_SE_MEMORY_EXECUTE))
        return 1;
    backend->memory_event_count++;
    return 0;
}

static int backend_invalidate(void *userdata, void *instance,
                              uint64_t address, uint64_t size)
{
    struct fake_backend *backend = (struct fake_backend *)userdata;
    if (instance != backend || address != 0x2000u || size != 0x100u) return 1;
    backend->invalidate_count++;
    return 0;
}

static void backend_destroy(void *userdata, void *instance)
{
    struct fake_backend *backend = (struct fake_backend *)userdata;
    if (instance == backend) backend->destroy_count++;
}

static madeira_se_cpu_backend_t make_backend(struct fake_backend *state)
{
    madeira_se_cpu_backend_t backend;
    memset(&backend, 0, sizeof(backend));
    backend.version = MADEIRA_SE_CPU_ABI_VERSION;
    backend.name = "fake-qemu-tcti";
    backend.capabilities = MADEIRA_SE_CPU_CAP_NO_RUNTIME_CODEGEN
        | MADEIRA_SE_CPU_CAP_X86_32 | MADEIRA_SE_CPU_CAP_X86_64;
    backend.userdata = state;
    backend.create = backend_create;
    backend.run = backend_run;
    backend.interrupt = backend_interrupt;
    backend.memory_event = backend_memory_event;
    backend.invalidate = backend_invalidate;
    backend.destroy = backend_destroy;
    return backend;
}

static int test_cpu_lifecycle(void)
{
    struct fake_backend backend_state = {0};
    struct fake_memory memory_state = {0};
    madeira_se_cpu_backend_t backend = make_backend(&backend_state);
    madeira_se_memory_t memory = {
        .version = MADEIRA_SE_CPU_ABI_VERSION,
        .userdata = &memory_state,
        .read = memory_read,
        .write = memory_write,
    };
    madeira_se_cpu_run_request_t request = {
        .version = MADEIRA_SE_CPU_ABI_VERSION,
        .max_instructions = 25u,
    };
    madeira_se_x86_context_t context = {
        .version = MADEIRA_SE_CPU_ABI_VERSION,
        .architecture = MADEIRA_SE_ARCH_X86_32,
        .rip = 0x401000u,
    };
    madeira_se_cpu_run_result_t result = {.version = MADEIRA_SE_CPU_ABI_VERSION};
    madeira_se_cpu_t *cpu = NULL;
    uint32_t service = 0x1234u;

    memcpy(memory_state.bytes + 4u, &service, sizeof(service));
    CHECK(madeira_se_cpu_create(&backend, MADEIRA_SE_ARCH_X86_32, &memory, &cpu)
          == MADEIRA_SE_OK);
    CHECK(cpu != NULL);
    CHECK(strcmp(madeira_se_cpu_backend_name(cpu), "fake-qemu-tcti") == 0);
    CHECK(madeira_se_cpu_architecture(cpu) == MADEIRA_SE_ARCH_X86_32);
    CHECK(backend_state.create_count == 1u);

    CHECK(madeira_se_cpu_run(cpu, &request, &context, &result) == MADEIRA_SE_OK);
    CHECK(result.reason == MADEIRA_SE_CPU_EXIT_SYSCALL);
    CHECK(result.service_number == service);
    CHECK(result.instructions_executed == 1u);
    CHECK(context.gpr[MADEIRA_SE_X86_RAX] == service);
    CHECK(context.rip == 0x401002u);
    CHECK(memory_state.read_count == 1u);

    CHECK(madeira_se_cpu_notify_memory(cpu, MADEIRA_SE_CPU_MEMORY_PROTECT,
                                      0x1000u, 0x2000u,
                                      MADEIRA_SE_MEMORY_READ | MADEIRA_SE_MEMORY_EXECUTE)
          == MADEIRA_SE_OK);
    CHECK(madeira_se_cpu_invalidate(cpu, 0x2000u, 0x100u) == MADEIRA_SE_OK);
    CHECK(madeira_se_cpu_interrupt(cpu) == MADEIRA_SE_OK);
    CHECK(backend_state.memory_event_count == 1u);
    CHECK(backend_state.invalidate_count == 1u);
    CHECK(backend_state.interrupt_count == 1u);

    backend_state.invalid_exit_reason = 1;
    CHECK(madeira_se_cpu_run(cpu, &request, &context, &result) == MADEIRA_SE_E_BACKEND);
    madeira_se_cpu_destroy(cpu);
    CHECK(backend_state.destroy_count == 1u);
    return 0;
}

static int test_cpu_policy_validation(void)
{
    struct fake_backend backend_state = {0};
    struct fake_memory memory_state = {0};
    madeira_se_cpu_backend_t backend = make_backend(&backend_state);
    madeira_se_memory_t memory = {
        .version = MADEIRA_SE_CPU_ABI_VERSION,
        .userdata = &memory_state,
        .read = memory_read,
        .write = memory_write,
    };
    madeira_se_cpu_t *cpu = NULL;

    backend.capabilities &= ~(uint32_t)MADEIRA_SE_CPU_CAP_NO_RUNTIME_CODEGEN;
    CHECK(madeira_se_cpu_create(&backend, MADEIRA_SE_ARCH_X86_32, &memory, &cpu)
          == MADEIRA_SE_E_UNSUPPORTED);
    CHECK(cpu == NULL);

    backend = make_backend(&backend_state);
    backend.capabilities &= ~(uint32_t)MADEIRA_SE_CPU_CAP_X86_64;
    CHECK(madeira_se_cpu_create(&backend, MADEIRA_SE_ARCH_X86_64, &memory, &cpu)
          == MADEIRA_SE_E_UNSUPPORTED_ARCHITECTURE);

    memory.read = NULL;
    CHECK(madeira_se_cpu_create(&backend, MADEIRA_SE_ARCH_X86_32, &memory, &cpu)
          == MADEIRA_SE_E_INVALID_ARGUMENT);
    return 0;
}

int main(void)
{
    CHECK(test_cpu_lifecycle() == 0);
    CHECK(test_cpu_policy_validation() == 0);
    puts("Madeira-SE CPU boundary tests passed");
    return 0;
}
