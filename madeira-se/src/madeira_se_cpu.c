/*
 * Madeira-SE no-JIT CPU backend boundary.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "madeira_se_cpu.h"

#include <stdlib.h>
#include <string.h>

struct madeira_se_cpu {
    madeira_se_cpu_backend_t backend;
    madeira_se_memory_t memory;
    madeira_se_architecture_t architecture;
    void *instance;
};

static int version_is_supported(uint32_t version)
{
    return version == 0u || version == MADEIRA_SE_CPU_ABI_VERSION;
}

static uint32_t architecture_capability(madeira_se_architecture_t architecture)
{
    switch (architecture) {
    case MADEIRA_SE_ARCH_X86_32: return MADEIRA_SE_CPU_CAP_X86_32;
    case MADEIRA_SE_ARCH_X86_64: return MADEIRA_SE_CPU_CAP_X86_64;
    default: return 0u;
    }
}

static int exit_reason_is_valid(uint32_t reason)
{
    return reason >= (uint32_t)MADEIRA_SE_CPU_EXIT_BUDGET
        && reason <= (uint32_t)MADEIRA_SE_CPU_EXIT_INTERRUPTED;
}

madeira_se_status_t madeira_se_cpu_create(const madeira_se_cpu_backend_t *backend,
                                          madeira_se_architecture_t architecture,
                                          const madeira_se_memory_t *memory,
                                          madeira_se_cpu_t **out_cpu)
{
    madeira_se_cpu_t *cpu;
    uint32_t arch_capability;

    if (out_cpu == NULL) return MADEIRA_SE_E_INVALID_ARGUMENT;
    *out_cpu = NULL;
    if (backend == NULL || memory == NULL || backend->name == NULL
        || backend->create == NULL || backend->run == NULL || backend->destroy == NULL)
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    if (!version_is_supported(backend->version) || !version_is_supported(memory->version))
        return MADEIRA_SE_E_UNSUPPORTED;
    if (memory->translate == NULL && (memory->read == NULL || memory->write == NULL))
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    if ((backend->capabilities & MADEIRA_SE_CPU_CAP_NO_RUNTIME_CODEGEN) == 0u)
        return MADEIRA_SE_E_UNSUPPORTED;

    arch_capability = architecture_capability(architecture);
    if (arch_capability == 0u || (backend->capabilities & arch_capability) == 0u)
        return MADEIRA_SE_E_UNSUPPORTED_ARCHITECTURE;

    cpu = (madeira_se_cpu_t *)calloc(1u, sizeof(*cpu));
    if (cpu == NULL) return MADEIRA_SE_E_OUT_OF_MEMORY;
    cpu->backend = *backend;
    cpu->memory = *memory;
    cpu->architecture = architecture;
    if (cpu->backend.create(cpu->backend.userdata, architecture, &cpu->memory,
                            &cpu->instance) != 0
        || cpu->instance == NULL) {
        if (cpu->instance != NULL)
            cpu->backend.destroy(cpu->backend.userdata, cpu->instance);
        free(cpu);
        return MADEIRA_SE_E_BACKEND;
    }
    *out_cpu = cpu;
    return MADEIRA_SE_OK;
}

void madeira_se_cpu_destroy(madeira_se_cpu_t *cpu)
{
    if (cpu == NULL) return;
    cpu->backend.destroy(cpu->backend.userdata, cpu->instance);
    free(cpu);
}

madeira_se_status_t madeira_se_cpu_run(madeira_se_cpu_t *cpu,
                                       const madeira_se_cpu_run_request_t *request,
                                       madeira_se_x86_context_t *context,
                                       madeira_se_cpu_run_result_t *result)
{
    uint32_t result_version;

    if (cpu == NULL || request == NULL || context == NULL || result == NULL)
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    if (!version_is_supported(request->version)
        || !version_is_supported(context->version)
        || !version_is_supported(result->version))
        return MADEIRA_SE_E_UNSUPPORTED;
    if (context->architecture != cpu->architecture)
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    if (request->max_instructions == 0u && request->deadline_ns == 0u)
        return MADEIRA_SE_E_INVALID_ARGUMENT;

    result_version = result->version == 0u ? MADEIRA_SE_CPU_ABI_VERSION : result->version;
    memset(result, 0, sizeof(*result));
    result->version = result_version;
    if (cpu->backend.run(cpu->backend.userdata, cpu->instance, request, context, result) != 0)
        return MADEIRA_SE_E_BACKEND;
    if (!exit_reason_is_valid(result->reason)) return MADEIRA_SE_E_BACKEND;
    return MADEIRA_SE_OK;
}

madeira_se_status_t madeira_se_cpu_interrupt(madeira_se_cpu_t *cpu)
{
    if (cpu == NULL) return MADEIRA_SE_E_INVALID_ARGUMENT;
    if (cpu->backend.interrupt == NULL) return MADEIRA_SE_E_UNSUPPORTED;
    return cpu->backend.interrupt(cpu->backend.userdata, cpu->instance) == 0
        ? MADEIRA_SE_OK : MADEIRA_SE_E_BACKEND;
}

madeira_se_status_t madeira_se_cpu_notify_memory(madeira_se_cpu_t *cpu,
                                                 madeira_se_cpu_memory_event_t event,
                                                 uint64_t guest_address,
                                                 uint64_t size,
                                                 uint32_t protection)
{
    if (cpu == NULL || (size == 0u && event != MADEIRA_SE_CPU_MEMORY_UNMAP))
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    if (event < MADEIRA_SE_CPU_MEMORY_MAP || event > MADEIRA_SE_CPU_MEMORY_DIRTY)
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    if (cpu->backend.memory_event == NULL) return MADEIRA_SE_E_UNSUPPORTED;
    return cpu->backend.memory_event(cpu->backend.userdata, cpu->instance, event,
                                     guest_address, size, protection) == 0
        ? MADEIRA_SE_OK : MADEIRA_SE_E_BACKEND;
}

madeira_se_status_t madeira_se_cpu_invalidate(madeira_se_cpu_t *cpu,
                                              uint64_t guest_address,
                                              uint64_t size)
{
    if (cpu == NULL || size == 0u) return MADEIRA_SE_E_INVALID_ARGUMENT;
    if (cpu->backend.invalidate == NULL) return MADEIRA_SE_E_UNSUPPORTED;
    return cpu->backend.invalidate(cpu->backend.userdata, cpu->instance,
                                   guest_address, size) == 0
        ? MADEIRA_SE_OK : MADEIRA_SE_E_BACKEND;
}

const char *madeira_se_cpu_backend_name(const madeira_se_cpu_t *cpu)
{
    return cpu != NULL ? cpu->backend.name : NULL;
}

madeira_se_architecture_t madeira_se_cpu_architecture(const madeira_se_cpu_t *cpu)
{
    return cpu != NULL ? cpu->architecture : MADEIRA_SE_ARCH_UNKNOWN;
}
