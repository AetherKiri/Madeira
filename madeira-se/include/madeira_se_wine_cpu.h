/*
 * Madeira-SE Wine CPU-provider transport.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MADEIRA_SE_WINE_CPU_H
#define MADEIRA_SE_WINE_CPU_H

#include "madeira_se_cpu.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MADEIRA_SE_WINE_CPU_ABI_VERSION 2u

/*
 * Every PE-to-Unix message starts with this header. The structures below use
 * fixed-width integers and contain no native pointers, so the ARM64 Wine DLL
 * and its Unix library cannot disagree about pointer-sized fields.
 */
typedef struct madeira_se_wine_cpu_message_header {
    uint32_t version;
    uint32_t size;
} madeira_se_wine_cpu_message_header_t;

typedef enum madeira_se_wine_cpu_operation {
    MADEIRA_SE_WINE_CPU_QUERY = 0,
    MADEIRA_SE_WINE_CPU_PROCESS_INIT = 1,
    MADEIRA_SE_WINE_CPU_PROCESS_TERM = 2,
    MADEIRA_SE_WINE_CPU_THREAD_INIT = 3,
    MADEIRA_SE_WINE_CPU_THREAD_TERM = 4,
    MADEIRA_SE_WINE_CPU_RUN = 5,
    MADEIRA_SE_WINE_CPU_MEMORY_EVENT = 6,
    MADEIRA_SE_WINE_CPU_INVALIDATE = 7,
    MADEIRA_SE_WINE_CPU_INTERRUPT = 8,
} madeira_se_wine_cpu_operation_t;

enum {
    MADEIRA_SE_WINE_CPU_PROCESS_DIRECT_ADDRESS_SPACE = 1u << 0,
    /*
     * Guest addresses are translated through guest_address_bias before the
     * backend touches host memory.  Apple Silicon uses this for PE32 because
     * XNU permanently excludes the host process's low 4 GiB.
     */
    MADEIRA_SE_WINE_CPU_PROCESS_BIASED_ADDRESS_SPACE = 1u << 1,
    /*
     * AMD64 Wine uses fixed data in the low 4 GiB even though XNU reserves
     * that whole range for native arm64 processes.  Translate low guest
     * addresses through guest_address_bias while leaving addresses at or
     * above 4 GiB direct.  PE32 continues to use the fully biased mode.
     */
    MADEIRA_SE_WINE_CPU_PROCESS_SPLIT_LOW_4G_ADDRESS_SPACE = 1u << 2,
};

typedef struct madeira_se_wine_cpu_query_message {
    madeira_se_wine_cpu_message_header_t header;
    uint32_t provider_version;
    uint32_t capabilities;
    uint32_t reserved[2];
} madeira_se_wine_cpu_query_message_t;

typedef struct madeira_se_wine_cpu_process_init_message {
    madeira_se_wine_cpu_message_header_t header;
    uint32_t architecture;
    uint32_t flags;
    uint64_t guest_address_bias;
    uint64_t process_handle;
} madeira_se_wine_cpu_process_init_message_t;

typedef struct madeira_se_wine_cpu_process_term_message {
    madeira_se_wine_cpu_message_header_t header;
    uint64_t process_handle;
    uint32_t exit_code;
    uint32_t reserved;
} madeira_se_wine_cpu_process_term_message_t;

typedef struct madeira_se_wine_cpu_thread_init_message {
    madeira_se_wine_cpu_message_header_t header;
    uint64_t process_handle;
    uint64_t thread_id;
    uint64_t thread_handle;
} madeira_se_wine_cpu_thread_init_message_t;

typedef struct madeira_se_wine_cpu_thread_term_message {
    madeira_se_wine_cpu_message_header_t header;
    uint64_t thread_handle;
    uint32_t exit_code;
    uint32_t reserved;
} madeira_se_wine_cpu_thread_term_message_t;

typedef struct madeira_se_wine_cpu_run_message {
    madeira_se_wine_cpu_message_header_t header;
    uint64_t thread_handle;
    madeira_se_cpu_run_request_t request;
    madeira_se_x86_context_t context;
    madeira_se_cpu_run_result_t result;
} madeira_se_wine_cpu_run_message_t;

typedef struct madeira_se_wine_cpu_memory_message {
    madeira_se_wine_cpu_message_header_t header;
    uint64_t process_handle;
    uint32_t event;
    uint32_t protection;
    uint64_t guest_address;
    uint64_t size;
} madeira_se_wine_cpu_memory_message_t;

typedef struct madeira_se_wine_cpu_invalidate_message {
    madeira_se_wine_cpu_message_header_t header;
    uint64_t process_handle;
    uint64_t guest_address;
    uint64_t size;
} madeira_se_wine_cpu_invalidate_message_t;

typedef struct madeira_se_wine_cpu_interrupt_message {
    madeira_se_wine_cpu_message_header_t header;
    uint64_t thread_handle;
} madeira_se_wine_cpu_interrupt_message_t;

enum {
    MADEIRA_SE_WINE_CONTEXT_CONTROL = 1u << 0,
    MADEIRA_SE_WINE_CONTEXT_INTEGER = 1u << 1,
    MADEIRA_SE_WINE_CONTEXT_SEGMENTS = 1u << 2,
    MADEIRA_SE_WINE_CONTEXT_FLOATING_POINT = 1u << 3,
    MADEIRA_SE_WINE_CONTEXT_DEBUG = 1u << 4,
    MADEIRA_SE_WINE_CONTEXT_XSTATE = 1u << 5,
    MADEIRA_SE_WINE_CONTEXT_SEGMENT_BASES = 1u << 6,
    MADEIRA_SE_WINE_CONTEXT_ALL = (1u << 7) - 1u,
};

/* Portable register images populated from Wine's I386_CONTEXT/AMD64_CONTEXT. */
typedef struct madeira_se_wine_i386_context {
    uint32_t version;
    uint32_t valid_fields;
    uint32_t gpr[8];
    uint32_t eip;
    uint32_t eflags;
    uint16_t segment[MADEIRA_SE_X86_SEGMENT_COUNT];
    uint16_t reserved16[2];
    uint64_t segment_base[MADEIRA_SE_X86_SEGMENT_COUNT];
    uint32_t debug_register[8];
    uint32_t fault_address;
    uint32_t reserved32;
    uint8_t fxsave[512];
    uint8_t ymm_hi[8][16];
    uint64_t reserved[8];
} madeira_se_wine_i386_context_t;

typedef struct madeira_se_wine_amd64_context {
    uint32_t version;
    uint32_t valid_fields;
    uint64_t gpr[MADEIRA_SE_X86_REGISTER_COUNT];
    uint64_t rip;
    uint64_t rflags;
    uint16_t segment[MADEIRA_SE_X86_SEGMENT_COUNT];
    uint16_t reserved16[2];
    uint64_t segment_base[MADEIRA_SE_X86_SEGMENT_COUNT];
    uint64_t debug_register[8];
    uint64_t fault_address;
    uint8_t fxsave[512];
    uint8_t ymm_hi[16][16];
    uint64_t reserved[16];
} madeira_se_wine_amd64_context_t;

typedef int32_t (*madeira_se_wine_cpu_dispatch_fn)(void *userdata,
                                                    uint32_t operation,
                                                    void *message,
                                                    uint32_t message_size);

/* Native-only endpoint. It is not copied across the Wine Unix-call boundary. */
typedef struct madeira_se_wine_cpu_endpoint {
    uint32_t version;
    uint32_t size;
    void *userdata;
    madeira_se_wine_cpu_dispatch_fn dispatch;
    void *reserved[4];
} madeira_se_wine_cpu_endpoint_t;

size_t madeira_se_wine_cpu_message_size(uint32_t operation);
madeira_se_status_t madeira_se_wine_cpu_validate_message(uint32_t operation,
                                                         const void *message,
                                                         size_t message_size);
madeira_se_status_t madeira_se_wine_cpu_dispatch(
    const madeira_se_wine_cpu_endpoint_t *endpoint,
    uint32_t operation,
    void *message,
    size_t message_size);

madeira_se_status_t madeira_se_wine_i386_context_apply(
    const madeira_se_wine_i386_context_t *source,
    madeira_se_x86_context_t *target);
madeira_se_status_t madeira_se_wine_i386_context_capture(
    const madeira_se_x86_context_t *source,
    uint32_t valid_fields,
    madeira_se_wine_i386_context_t *target);
madeira_se_status_t madeira_se_wine_amd64_context_apply(
    const madeira_se_wine_amd64_context_t *source,
    madeira_se_x86_context_t *target);
madeira_se_status_t madeira_se_wine_amd64_context_capture(
    const madeira_se_x86_context_t *source,
    uint32_t valid_fields,
    madeira_se_wine_amd64_context_t *target);

#ifdef __cplusplus
}
#endif

#endif /* MADEIRA_SE_WINE_CPU_H */
