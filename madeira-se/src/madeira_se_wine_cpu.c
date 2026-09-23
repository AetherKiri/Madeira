/*
 * Madeira-SE Wine CPU-provider transport.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "madeira_se_wine_cpu.h"

#include <limits.h>
#include <string.h>

_Static_assert(sizeof(madeira_se_wine_cpu_message_header_t) == 8u,
               "Wine CPU message header layout changed");
_Static_assert(sizeof(madeira_se_wine_cpu_query_message_t) == 24u,
               "Wine CPU query message layout changed");
_Static_assert(sizeof(madeira_se_wine_cpu_process_init_message_t) == 32u,
               "Wine CPU process-init message layout changed");
_Static_assert(sizeof(madeira_se_wine_cpu_thread_init_message_t) == 32u,
               "Wine CPU thread-init message layout changed");
_Static_assert(sizeof(madeira_se_wine_i386_context_t) == 856u,
               "Wine i386 context layout changed");
_Static_assert(sizeof(madeira_se_wine_amd64_context_t) == 1184u,
               "Wine amd64 context layout changed");
_Static_assert(sizeof(madeira_se_x86_context_t) == 1184u,
               "canonical x86 context layout changed");

static int version_is_supported(uint32_t version)
{
    return version == 0u || version == MADEIRA_SE_CPU_ABI_VERSION;
}

static int wine_version_is_supported(uint32_t version)
{
    return version == MADEIRA_SE_WINE_CPU_ABI_VERSION;
}

static int architecture_is_supported(uint32_t architecture)
{
    return architecture == (uint32_t)MADEIRA_SE_ARCH_X86_32
        || architecture == (uint32_t)MADEIRA_SE_ARCH_X86_64;
}

static int valid_context_fields(uint32_t fields)
{
    return fields != 0u
        && (fields & ~(uint32_t)MADEIRA_SE_WINE_CONTEXT_ALL) == 0u;
}

static int valid_exit_reason(uint32_t reason)
{
    return reason >= (uint32_t)MADEIRA_SE_CPU_EXIT_BUDGET
        && reason <= (uint32_t)MADEIRA_SE_CPU_EXIT_INTERRUPTED;
}

size_t madeira_se_wine_cpu_message_size(uint32_t operation)
{
    switch (operation) {
    case MADEIRA_SE_WINE_CPU_QUERY:
        return sizeof(madeira_se_wine_cpu_query_message_t);
    case MADEIRA_SE_WINE_CPU_PROCESS_INIT:
        return sizeof(madeira_se_wine_cpu_process_init_message_t);
    case MADEIRA_SE_WINE_CPU_PROCESS_TERM:
        return sizeof(madeira_se_wine_cpu_process_term_message_t);
    case MADEIRA_SE_WINE_CPU_THREAD_INIT:
        return sizeof(madeira_se_wine_cpu_thread_init_message_t);
    case MADEIRA_SE_WINE_CPU_THREAD_TERM:
        return sizeof(madeira_se_wine_cpu_thread_term_message_t);
    case MADEIRA_SE_WINE_CPU_RUN:
        return sizeof(madeira_se_wine_cpu_run_message_t);
    case MADEIRA_SE_WINE_CPU_MEMORY_EVENT:
        return sizeof(madeira_se_wine_cpu_memory_message_t);
    case MADEIRA_SE_WINE_CPU_INVALIDATE:
        return sizeof(madeira_se_wine_cpu_invalidate_message_t);
    case MADEIRA_SE_WINE_CPU_INTERRUPT:
        return sizeof(madeira_se_wine_cpu_interrupt_message_t);
    default:
        return 0u;
    }
}

madeira_se_status_t madeira_se_wine_cpu_validate_message(uint32_t operation,
                                                         const void *message,
                                                         size_t message_size)
{
    const madeira_se_wine_cpu_message_header_t *header =
        (const madeira_se_wine_cpu_message_header_t *)message;
    size_t expected_size = madeira_se_wine_cpu_message_size(operation);

    if (message == NULL || expected_size == 0u || expected_size > UINT32_MAX)
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    if (message_size != expected_size
        || header->size != (uint32_t)expected_size)
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    if (!wine_version_is_supported(header->version))
        return MADEIRA_SE_E_UNSUPPORTED;

    switch (operation) {
    case MADEIRA_SE_WINE_CPU_QUERY:
        break;
    case MADEIRA_SE_WINE_CPU_PROCESS_INIT: {
        const madeira_se_wine_cpu_process_init_message_t *params = message;
        const uint32_t address_flags =
            MADEIRA_SE_WINE_CPU_PROCESS_DIRECT_ADDRESS_SPACE
            | MADEIRA_SE_WINE_CPU_PROCESS_BIASED_ADDRESS_SPACE
            | MADEIRA_SE_WINE_CPU_PROCESS_SPLIT_LOW_4G_ADDRESS_SPACE;
        const uint32_t selected_address_flags = params->flags & address_flags;
        if (!architecture_is_supported(params->architecture))
            return MADEIRA_SE_E_UNSUPPORTED_ARCHITECTURE;
        if ((params->flags & ~address_flags) != 0u
            || selected_address_flags == 0u
            || (selected_address_flags & (selected_address_flags - 1u)) != 0u)
            return MADEIRA_SE_E_INVALID_ARGUMENT;
        if ((params->flags
             & MADEIRA_SE_WINE_CPU_PROCESS_DIRECT_ADDRESS_SPACE) != 0u) {
            if (params->guest_address_bias != 0u)
                return MADEIRA_SE_E_INVALID_ARGUMENT;
        } else if (params->guest_address_bias == 0u
                   || (params->guest_address_bias & UINT64_C(0xffffffff)) != 0u
                   || params->guest_address_bias
                      > (uint64_t)UINTPTR_MAX - UINT64_C(0xffffffff)) {
            return MADEIRA_SE_E_INVALID_ARGUMENT;
        } else if ((params->flags
                    & MADEIRA_SE_WINE_CPU_PROCESS_BIASED_ADDRESS_SPACE) != 0u
                   && params->architecture != (uint32_t)MADEIRA_SE_ARCH_X86_32) {
            return MADEIRA_SE_E_INVALID_ARGUMENT;
        } else if ((params->flags
                    & MADEIRA_SE_WINE_CPU_PROCESS_SPLIT_LOW_4G_ADDRESS_SPACE) != 0u
                   && params->architecture != (uint32_t)MADEIRA_SE_ARCH_X86_64) {
            return MADEIRA_SE_E_INVALID_ARGUMENT;
        }
        break;
    }
    case MADEIRA_SE_WINE_CPU_PROCESS_TERM: {
        const madeira_se_wine_cpu_process_term_message_t *params = message;
        if (params->process_handle == 0u) return MADEIRA_SE_E_INVALID_ARGUMENT;
        break;
    }
    case MADEIRA_SE_WINE_CPU_THREAD_INIT: {
        const madeira_se_wine_cpu_thread_init_message_t *params = message;
        if (params->process_handle == 0u || params->thread_id == 0u)
            return MADEIRA_SE_E_INVALID_ARGUMENT;
        break;
    }
    case MADEIRA_SE_WINE_CPU_THREAD_TERM: {
        const madeira_se_wine_cpu_thread_term_message_t *params = message;
        if (params->thread_handle == 0u) return MADEIRA_SE_E_INVALID_ARGUMENT;
        break;
    }
    case MADEIRA_SE_WINE_CPU_RUN: {
        const madeira_se_wine_cpu_run_message_t *params = message;
        if (params->thread_handle == 0u
            || !version_is_supported(params->request.version)
            || !version_is_supported(params->context.version)
            || !version_is_supported(params->result.version))
            return MADEIRA_SE_E_INVALID_ARGUMENT;
        if (!architecture_is_supported(params->context.architecture))
            return MADEIRA_SE_E_UNSUPPORTED_ARCHITECTURE;
        if (params->request.max_instructions == 0u
            && params->request.deadline_ns == 0u)
            return MADEIRA_SE_E_INVALID_ARGUMENT;
        if (params->request.syscall_dispatcher == 0u
            || params->request.unix_call_dispatcher == 0u
            || params->request.syscall_dispatcher
               == params->request.unix_call_dispatcher)
            return MADEIRA_SE_E_INVALID_ARGUMENT;
        break;
    }
    case MADEIRA_SE_WINE_CPU_MEMORY_EVENT: {
        const madeira_se_wine_cpu_memory_message_t *params = message;
        if (params->process_handle == 0u
            || (params->size == 0u
                && params->event != (uint32_t)MADEIRA_SE_CPU_MEMORY_UNMAP)
            || params->event < (uint32_t)MADEIRA_SE_CPU_MEMORY_MAP
            || params->event > (uint32_t)MADEIRA_SE_CPU_MEMORY_DIRTY)
            return MADEIRA_SE_E_INVALID_ARGUMENT;
        if ((params->protection & ~(uint32_t)(MADEIRA_SE_MEMORY_READ
                                             | MADEIRA_SE_MEMORY_WRITE
                                             | MADEIRA_SE_MEMORY_EXECUTE)) != 0u)
            return MADEIRA_SE_E_INVALID_ARGUMENT;
        break;
    }
    case MADEIRA_SE_WINE_CPU_INVALIDATE: {
        const madeira_se_wine_cpu_invalidate_message_t *params = message;
        if (params->process_handle == 0u || params->size == 0u)
            return MADEIRA_SE_E_INVALID_ARGUMENT;
        break;
    }
    case MADEIRA_SE_WINE_CPU_INTERRUPT: {
        const madeira_se_wine_cpu_interrupt_message_t *params = message;
        if (params->thread_handle == 0u) return MADEIRA_SE_E_INVALID_ARGUMENT;
        break;
    }
    default:
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    }
    return MADEIRA_SE_OK;
}

static madeira_se_status_t validate_response(uint32_t operation, const void *message)
{
    switch (operation) {
    case MADEIRA_SE_WINE_CPU_QUERY: {
        const madeira_se_wine_cpu_query_message_t *params = message;
        uint32_t architectures = MADEIRA_SE_CPU_CAP_X86_32 | MADEIRA_SE_CPU_CAP_X86_64;
        if (params->provider_version != MADEIRA_SE_WINE_CPU_ABI_VERSION)
            return MADEIRA_SE_E_UNSUPPORTED;
        if ((params->capabilities & MADEIRA_SE_CPU_CAP_NO_RUNTIME_CODEGEN) == 0u
            || (params->capabilities & architectures) == 0u)
            return MADEIRA_SE_E_UNSUPPORTED;
        break;
    }
    case MADEIRA_SE_WINE_CPU_PROCESS_INIT:
        if (((const madeira_se_wine_cpu_process_init_message_t *)message)->process_handle == 0u)
            return MADEIRA_SE_E_BACKEND;
        break;
    case MADEIRA_SE_WINE_CPU_THREAD_INIT:
        if (((const madeira_se_wine_cpu_thread_init_message_t *)message)->thread_handle == 0u)
            return MADEIRA_SE_E_BACKEND;
        break;
    case MADEIRA_SE_WINE_CPU_RUN: {
        const madeira_se_wine_cpu_run_message_t *params = message;
        if (params->context.version != MADEIRA_SE_CPU_ABI_VERSION
            || params->result.version != MADEIRA_SE_CPU_ABI_VERSION
            || !valid_exit_reason(params->result.reason))
            return MADEIRA_SE_E_BACKEND;
        break;
    }
    default:
        break;
    }
    return MADEIRA_SE_OK;
}

madeira_se_status_t madeira_se_wine_cpu_dispatch(
    const madeira_se_wine_cpu_endpoint_t *endpoint,
    uint32_t operation,
    void *message,
    size_t message_size)
{
    madeira_se_status_t status;
    int32_t backend_status;

    if (endpoint == NULL || endpoint->version != MADEIRA_SE_WINE_CPU_ABI_VERSION
        || endpoint->size < sizeof(*endpoint) || endpoint->dispatch == NULL)
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    status = madeira_se_wine_cpu_validate_message(operation, message, message_size);
    if (status != MADEIRA_SE_OK) return status;
    backend_status = endpoint->dispatch(endpoint->userdata, operation, message,
                                        (uint32_t)message_size);
    if (backend_status != (int32_t)MADEIRA_SE_OK)
        return backend_status <= (int32_t)MADEIRA_SE_E_INVALID_ARGUMENT
            && backend_status >= (int32_t)MADEIRA_SE_E_NOT_READY
            ? (madeira_se_status_t)backend_status : MADEIRA_SE_E_BACKEND;
    return validate_response(operation, message);
}

static madeira_se_status_t prepare_target(madeira_se_x86_context_t *target,
                                          madeira_se_architecture_t architecture)
{
    if (target == NULL) return MADEIRA_SE_E_INVALID_ARGUMENT;
    if (target->version == 0u) {
        memset(target, 0, sizeof(*target));
        target->version = MADEIRA_SE_CPU_ABI_VERSION;
        target->architecture = (uint32_t)architecture;
    }
    if (!version_is_supported(target->version)
        || target->architecture != (uint32_t)architecture)
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    return MADEIRA_SE_OK;
}

madeira_se_status_t madeira_se_wine_i386_context_apply(
    const madeira_se_wine_i386_context_t *source,
    madeira_se_x86_context_t *target)
{
    size_t i;
    madeira_se_status_t status;

    if (source == NULL || !wine_version_is_supported(source->version)
        || !valid_context_fields(source->valid_fields))
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    status = prepare_target(target, MADEIRA_SE_ARCH_X86_32);
    if (status != MADEIRA_SE_OK) return status;

    if ((source->valid_fields & MADEIRA_SE_WINE_CONTEXT_INTEGER) != 0u)
        for (i = 0u; i < 8u; ++i) target->gpr[i] = source->gpr[i];
    if ((source->valid_fields & MADEIRA_SE_WINE_CONTEXT_CONTROL) != 0u) {
        target->rip = source->eip;
        target->rflags = source->eflags;
        target->fault_address = source->fault_address;
    }
    if ((source->valid_fields & MADEIRA_SE_WINE_CONTEXT_SEGMENTS) != 0u)
        memcpy(target->segment, source->segment, sizeof(source->segment));
    if ((source->valid_fields & MADEIRA_SE_WINE_CONTEXT_SEGMENT_BASES) != 0u)
        memcpy(target->segment_base, source->segment_base, sizeof(source->segment_base));
    if ((source->valid_fields & MADEIRA_SE_WINE_CONTEXT_DEBUG) != 0u)
        for (i = 0u; i < 8u; ++i) target->debug_register[i] = source->debug_register[i];
    if ((source->valid_fields & MADEIRA_SE_WINE_CONTEXT_FLOATING_POINT) != 0u)
        memcpy(target->fxsave, source->fxsave, sizeof(source->fxsave));
    if ((source->valid_fields & MADEIRA_SE_WINE_CONTEXT_XSTATE) != 0u)
        memcpy(target->ymm_hi, source->ymm_hi, sizeof(source->ymm_hi));
    return MADEIRA_SE_OK;
}

madeira_se_status_t madeira_se_wine_i386_context_capture(
    const madeira_se_x86_context_t *source,
    uint32_t valid_fields,
    madeira_se_wine_i386_context_t *target)
{
    size_t i;

    if (source == NULL || target == NULL || !version_is_supported(source->version)
        || source->architecture != (uint32_t)MADEIRA_SE_ARCH_X86_32
        || !valid_context_fields(valid_fields))
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    memset(target, 0, sizeof(*target));
    target->version = MADEIRA_SE_WINE_CPU_ABI_VERSION;
    target->valid_fields = valid_fields;
    if ((valid_fields & MADEIRA_SE_WINE_CONTEXT_INTEGER) != 0u)
        for (i = 0u; i < 8u; ++i) target->gpr[i] = (uint32_t)source->gpr[i];
    if ((valid_fields & MADEIRA_SE_WINE_CONTEXT_CONTROL) != 0u) {
        target->eip = (uint32_t)source->rip;
        target->eflags = (uint32_t)source->rflags;
        target->fault_address = (uint32_t)source->fault_address;
    }
    if ((valid_fields & MADEIRA_SE_WINE_CONTEXT_SEGMENTS) != 0u)
        memcpy(target->segment, source->segment, sizeof(target->segment));
    if ((valid_fields & MADEIRA_SE_WINE_CONTEXT_SEGMENT_BASES) != 0u)
        memcpy(target->segment_base, source->segment_base, sizeof(target->segment_base));
    if ((valid_fields & MADEIRA_SE_WINE_CONTEXT_DEBUG) != 0u)
        for (i = 0u; i < 8u; ++i)
            target->debug_register[i] = (uint32_t)source->debug_register[i];
    if ((valid_fields & MADEIRA_SE_WINE_CONTEXT_FLOATING_POINT) != 0u)
        memcpy(target->fxsave, source->fxsave, sizeof(target->fxsave));
    if ((valid_fields & MADEIRA_SE_WINE_CONTEXT_XSTATE) != 0u)
        memcpy(target->ymm_hi, source->ymm_hi, sizeof(target->ymm_hi));
    return MADEIRA_SE_OK;
}

madeira_se_status_t madeira_se_wine_amd64_context_apply(
    const madeira_se_wine_amd64_context_t *source,
    madeira_se_x86_context_t *target)
{
    madeira_se_status_t status;

    if (source == NULL || !wine_version_is_supported(source->version)
        || !valid_context_fields(source->valid_fields))
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    status = prepare_target(target, MADEIRA_SE_ARCH_X86_64);
    if (status != MADEIRA_SE_OK) return status;
    if ((source->valid_fields & MADEIRA_SE_WINE_CONTEXT_INTEGER) != 0u)
        memcpy(target->gpr, source->gpr, sizeof(source->gpr));
    if ((source->valid_fields & MADEIRA_SE_WINE_CONTEXT_CONTROL) != 0u) {
        target->rip = source->rip;
        target->rflags = source->rflags;
        target->fault_address = source->fault_address;
    }
    if ((source->valid_fields & MADEIRA_SE_WINE_CONTEXT_SEGMENTS) != 0u)
        memcpy(target->segment, source->segment, sizeof(source->segment));
    if ((source->valid_fields & MADEIRA_SE_WINE_CONTEXT_SEGMENT_BASES) != 0u)
        memcpy(target->segment_base, source->segment_base, sizeof(source->segment_base));
    if ((source->valid_fields & MADEIRA_SE_WINE_CONTEXT_DEBUG) != 0u)
        memcpy(target->debug_register, source->debug_register,
               sizeof(source->debug_register));
    if ((source->valid_fields & MADEIRA_SE_WINE_CONTEXT_FLOATING_POINT) != 0u)
        memcpy(target->fxsave, source->fxsave, sizeof(source->fxsave));
    if ((source->valid_fields & MADEIRA_SE_WINE_CONTEXT_XSTATE) != 0u)
        memcpy(target->ymm_hi, source->ymm_hi, sizeof(source->ymm_hi));
    return MADEIRA_SE_OK;
}

madeira_se_status_t madeira_se_wine_amd64_context_capture(
    const madeira_se_x86_context_t *source,
    uint32_t valid_fields,
    madeira_se_wine_amd64_context_t *target)
{
    if (source == NULL || target == NULL || !version_is_supported(source->version)
        || source->architecture != (uint32_t)MADEIRA_SE_ARCH_X86_64
        || !valid_context_fields(valid_fields))
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    memset(target, 0, sizeof(*target));
    target->version = MADEIRA_SE_WINE_CPU_ABI_VERSION;
    target->valid_fields = valid_fields;
    if ((valid_fields & MADEIRA_SE_WINE_CONTEXT_INTEGER) != 0u)
        memcpy(target->gpr, source->gpr, sizeof(target->gpr));
    if ((valid_fields & MADEIRA_SE_WINE_CONTEXT_CONTROL) != 0u) {
        target->rip = source->rip;
        target->rflags = source->rflags;
        target->fault_address = source->fault_address;
    }
    if ((valid_fields & MADEIRA_SE_WINE_CONTEXT_SEGMENTS) != 0u)
        memcpy(target->segment, source->segment, sizeof(target->segment));
    if ((valid_fields & MADEIRA_SE_WINE_CONTEXT_SEGMENT_BASES) != 0u)
        memcpy(target->segment_base, source->segment_base, sizeof(target->segment_base));
    if ((valid_fields & MADEIRA_SE_WINE_CONTEXT_DEBUG) != 0u)
        memcpy(target->debug_register, source->debug_register,
               sizeof(target->debug_register));
    if ((valid_fields & MADEIRA_SE_WINE_CONTEXT_FLOATING_POINT) != 0u)
        memcpy(target->fxsave, source->fxsave, sizeof(target->fxsave));
    if ((valid_fields & MADEIRA_SE_WINE_CONTEXT_XSTATE) != 0u)
        memcpy(target->ymm_hi, source->ymm_hi, sizeof(target->ymm_hi));
    return MADEIRA_SE_OK;
}
