/*
 * Madeira-SE no-JIT CPU backend boundary.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MADEIRA_SE_CPU_H
#define MADEIRA_SE_CPU_H

#include "madeira_se.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MADEIRA_SE_CPU_ABI_VERSION 1u

typedef struct madeira_se_cpu madeira_se_cpu_t;

typedef enum madeira_se_x86_register {
    MADEIRA_SE_X86_RAX = 0,
    MADEIRA_SE_X86_RCX = 1,
    MADEIRA_SE_X86_RDX = 2,
    MADEIRA_SE_X86_RBX = 3,
    MADEIRA_SE_X86_RSP = 4,
    MADEIRA_SE_X86_RBP = 5,
    MADEIRA_SE_X86_RSI = 6,
    MADEIRA_SE_X86_RDI = 7,
    MADEIRA_SE_X86_R8 = 8,
    MADEIRA_SE_X86_R9 = 9,
    MADEIRA_SE_X86_R10 = 10,
    MADEIRA_SE_X86_R11 = 11,
    MADEIRA_SE_X86_R12 = 12,
    MADEIRA_SE_X86_R13 = 13,
    MADEIRA_SE_X86_R14 = 14,
    MADEIRA_SE_X86_R15 = 15,
    MADEIRA_SE_X86_REGISTER_COUNT = 16,
} madeira_se_x86_register_t;

typedef enum madeira_se_x86_segment {
    MADEIRA_SE_X86_SEGMENT_CS = 0,
    MADEIRA_SE_X86_SEGMENT_SS = 1,
    MADEIRA_SE_X86_SEGMENT_DS = 2,
    MADEIRA_SE_X86_SEGMENT_ES = 3,
    MADEIRA_SE_X86_SEGMENT_FS = 4,
    MADEIRA_SE_X86_SEGMENT_GS = 5,
    MADEIRA_SE_X86_SEGMENT_COUNT = 6,
} madeira_se_x86_segment_t;

/*
 * Canonical state exchanged between Wine's CPU bridge and QEMU TCTI.
 * The lower halves are used in x86-32 mode. fxsave contains the architectural
 * 512-byte FXSAVE image; ymm_hi contains the upper 128 bits of YMM0..15.
 */
typedef struct madeira_se_x86_context {
    uint32_t version;
    uint32_t architecture;
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
} madeira_se_x86_context_t;

typedef enum madeira_se_memory_access {
    MADEIRA_SE_MEMORY_READ = 1u << 0,
    MADEIRA_SE_MEMORY_WRITE = 1u << 1,
    MADEIRA_SE_MEMORY_EXECUTE = 1u << 2,
    MADEIRA_SE_MEMORY_ATOMIC = 1u << 3,
} madeira_se_memory_access_t;

typedef int (*madeira_se_memory_read_fn)(void *userdata,
                                         uint64_t guest_address,
                                         void *destination,
                                         size_t size,
                                         uint32_t access);
typedef int (*madeira_se_memory_write_fn)(void *userdata,
                                          uint64_t guest_address,
                                          const void *source,
                                          size_t size,
                                          uint32_t access);
typedef void *(*madeira_se_memory_translate_fn)(void *userdata,
                                                uint64_t guest_address,
                                                size_t size,
                                                uint32_t access);
typedef int (*madeira_se_memory_compare_exchange_fn)(void *userdata,
                                                      uint64_t guest_address,
                                                      const void *expected,
                                                      const void *desired,
                                                      void *observed,
                                                      size_t size);

/*
 * TCTI can use translate() for Wine's directly mapped address space. read()
 * and write() are the checked fallback used for faulting or indirect accesses.
 * A provider must expose translate(), or both read() and write().
 */
typedef struct madeira_se_memory {
    uint32_t version;
    void *userdata;
    madeira_se_memory_read_fn read;
    madeira_se_memory_write_fn write;
    madeira_se_memory_translate_fn translate;
    madeira_se_memory_compare_exchange_fn compare_exchange;
    void *reserved[4];
} madeira_se_memory_t;

typedef enum madeira_se_cpu_exit_reason {
    MADEIRA_SE_CPU_EXIT_NONE = 0,
    MADEIRA_SE_CPU_EXIT_BUDGET = 1,
    MADEIRA_SE_CPU_EXIT_SYSCALL = 2,
    MADEIRA_SE_CPU_EXIT_UNIX_CALL = 3,
    MADEIRA_SE_CPU_EXIT_EXCEPTION = 4,
    MADEIRA_SE_CPU_EXIT_HALT = 5,
    MADEIRA_SE_CPU_EXIT_INTERRUPTED = 6,
} madeira_se_cpu_exit_reason_t;

typedef struct madeira_se_cpu_run_request {
    uint32_t version;
    uint64_t max_instructions;
    uint64_t deadline_ns;
    /* Guest addresses that end a Wine syscall or Unix-call dispatch block. */
    uint64_t syscall_dispatcher;
    uint64_t unix_call_dispatcher;
    uint32_t flags;
    uint32_t reserved;
} madeira_se_cpu_run_request_t;

/* Run flags are opt-in because a reused context is only valid after a
 * previous budget exit on the same backend instance. */
enum {
    MADEIRA_SE_CPU_RUN_REUSE_CONTEXT = 1u << 0,
};

typedef struct madeira_se_cpu_run_result {
    uint32_t version;
    uint32_t reason;
    uint64_t instructions_executed;
    uint64_t service_number;
    uint64_t fault_address;
    uint32_t exception_vector;
    uint32_t exception_error_code;
    uint64_t reserved[4];
} madeira_se_cpu_run_result_t;

/* Result flags are carried in reserved[0] to preserve the ABI layout. */
enum {
    MADEIRA_SE_CPU_RESULT_CONTEXT_UNCHANGED = 1u << 0,
};

typedef enum madeira_se_cpu_memory_event {
    MADEIRA_SE_CPU_MEMORY_MAP = 1,
    MADEIRA_SE_CPU_MEMORY_UNMAP = 2,
    MADEIRA_SE_CPU_MEMORY_PROTECT = 3,
    MADEIRA_SE_CPU_MEMORY_DIRTY = 4,
} madeira_se_cpu_memory_event_t;

enum {
    MADEIRA_SE_CPU_CAP_NO_RUNTIME_CODEGEN = 1u << 0,
    MADEIRA_SE_CPU_CAP_X86_32 = 1u << 1,
    MADEIRA_SE_CPU_CAP_X86_64 = 1u << 2,
};

typedef int (*madeira_se_cpu_backend_create_fn)(void *userdata,
                                                 madeira_se_architecture_t architecture,
                                                 const madeira_se_memory_t *memory,
                                                 void **out_instance);
typedef int (*madeira_se_cpu_backend_run_fn)(void *userdata,
                                              void *instance,
                                              const madeira_se_cpu_run_request_t *request,
                                              madeira_se_x86_context_t *context,
                                              madeira_se_cpu_run_result_t *result);
typedef int (*madeira_se_cpu_backend_interrupt_fn)(void *userdata, void *instance);
typedef int (*madeira_se_cpu_backend_memory_event_fn)(void *userdata,
                                                       void *instance,
                                                       madeira_se_cpu_memory_event_t event,
                                                       uint64_t guest_address,
                                                       uint64_t size,
                                                       uint32_t protection);
typedef int (*madeira_se_cpu_backend_invalidate_fn)(void *userdata,
                                                     void *instance,
                                                     uint64_t guest_address,
                                                     uint64_t size);
typedef void (*madeira_se_cpu_backend_destroy_fn)(void *userdata, void *instance);

/*
 * Optional execution counters. A backend that can count guest work without
 * changing guest semantics fills these in; backends that cannot leave the
 * callback NULL and the host falls back to per-run accounting.
 */
typedef struct madeira_se_cpu_perf_counters {
    uint32_t version;
    uint32_t reserved;
    uint64_t guest_instructions;
    uint64_t tb_entries;
    uint64_t tb_translations;
    uint64_t translated_instructions;
} madeira_se_cpu_perf_counters_t;

typedef void (*madeira_se_cpu_backend_perf_fn)(
    void *userdata,
    void *instance,
    madeira_se_cpu_perf_counters_t *out_counters);

/*
 * QEMU's complete x86 frontend plus the UTM aarch64-tcti backend implements
 * this table. Madeira-SE rejects a backend unless it declares that it never
 * generates executable code at runtime.
 */
typedef struct madeira_se_cpu_backend {
    uint32_t version;
    const char *name;
    uint32_t capabilities;
    void *userdata;
    madeira_se_cpu_backend_create_fn create;
    madeira_se_cpu_backend_run_fn run;
    madeira_se_cpu_backend_interrupt_fn interrupt;
    madeira_se_cpu_backend_memory_event_fn memory_event;
    madeira_se_cpu_backend_invalidate_fn invalidate;
    madeira_se_cpu_backend_destroy_fn destroy;
    madeira_se_cpu_backend_perf_fn perf_counters;
    void *reserved[4];
} madeira_se_cpu_backend_t;

madeira_se_status_t madeira_se_cpu_create(const madeira_se_cpu_backend_t *backend,
                                          madeira_se_architecture_t architecture,
                                          const madeira_se_memory_t *memory,
                                          madeira_se_cpu_t **out_cpu);
void madeira_se_cpu_destroy(madeira_se_cpu_t *cpu);

madeira_se_status_t madeira_se_cpu_run(madeira_se_cpu_t *cpu,
                                       const madeira_se_cpu_run_request_t *request,
                                       madeira_se_x86_context_t *context,
                                       madeira_se_cpu_run_result_t *result);
madeira_se_status_t madeira_se_cpu_interrupt(madeira_se_cpu_t *cpu);
madeira_se_status_t madeira_se_cpu_notify_memory(madeira_se_cpu_t *cpu,
                                                 madeira_se_cpu_memory_event_t event,
                                                 uint64_t guest_address,
                                                 uint64_t size,
                                                 uint32_t protection);
madeira_se_status_t madeira_se_cpu_invalidate(madeira_se_cpu_t *cpu,
                                              uint64_t guest_address,
                                              uint64_t size);

const char *madeira_se_cpu_backend_name(const madeira_se_cpu_t *cpu);
madeira_se_architecture_t madeira_se_cpu_architecture(const madeira_se_cpu_t *cpu);

#ifdef __cplusplus
}
#endif

#endif /* MADEIRA_SE_CPU_H */
