/*
 * Madeira-SE Wine CPU host tests.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "madeira_se_wine_cpu_host.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

struct fake_backend;

struct fake_cpu {
    struct fake_backend *backend;
    madeira_se_architecture_t architecture;
    madeira_se_memory_t memory;
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    int running;
    int interrupted;
};

struct fake_backend {
    struct fake_cpu *last_cpu;
    unsigned int creates;
    unsigned int runs;
    unsigned int interrupts;
    unsigned int memory_events;
    unsigned int invalidations;
    unsigned int destroys;
    madeira_se_cpu_memory_event_t last_event;
    uint64_t last_address;
    uint64_t last_size;
    uint32_t last_protection;
};

static int fake_create(void *userdata, madeira_se_architecture_t architecture,
                       const madeira_se_memory_t *memory, void **out_instance)
{
    struct fake_backend *backend = userdata;
    struct fake_cpu *cpu = calloc(1u, sizeof(*cpu));

    if (cpu == NULL) return 1;
    cpu->backend = backend;
    cpu->architecture = architecture;
    cpu->memory = *memory;
    if (pthread_mutex_init(&cpu->mutex, NULL) != 0) {
        free(cpu);
        return 1;
    }
    if (pthread_cond_init(&cpu->changed, NULL) != 0) {
        (void)pthread_mutex_destroy(&cpu->mutex);
        free(cpu);
        return 1;
    }
    backend->creates++;
    backend->last_cpu = cpu;
    *out_instance = cpu;
    return 0;
}

static int fake_run(void *userdata, void *instance,
                    const madeira_se_cpu_run_request_t *request,
                    madeira_se_x86_context_t *context,
                    madeira_se_cpu_run_result_t *result)
{
    struct fake_backend *backend = userdata;
    struct fake_cpu *cpu = instance;

    if (request->max_instructions == 0u
        || context->architecture != (uint32_t)cpu->architecture
        || cpu->memory.translate(cpu->memory.userdata, 0x1234u, 4u,
                                 MADEIRA_SE_MEMORY_READ)
           != (void *)(uintptr_t)0x1234u)
        return 1;

    (void)pthread_mutex_lock(&cpu->mutex);
    cpu->running = 1;
    (void)pthread_cond_broadcast(&cpu->changed);
    while (!cpu->interrupted)
        (void)pthread_cond_wait(&cpu->changed, &cpu->mutex);
    (void)pthread_mutex_unlock(&cpu->mutex);

    backend->runs++;
    context->gpr[MADEIRA_SE_X86_RAX] = 0xfeedfaceu;
    result->version = MADEIRA_SE_CPU_ABI_VERSION;
    result->reason = MADEIRA_SE_CPU_EXIT_INTERRUPTED;
    result->instructions_executed = 17u;
    return 0;
}

static int fake_interrupt(void *userdata, void *instance)
{
    struct fake_backend *backend = userdata;
    struct fake_cpu *cpu = instance;

    (void)pthread_mutex_lock(&cpu->mutex);
    cpu->interrupted = 1;
    backend->interrupts++;
    (void)pthread_cond_broadcast(&cpu->changed);
    (void)pthread_mutex_unlock(&cpu->mutex);
    return 0;
}

static int fake_memory_event(void *userdata, void *instance,
                             madeira_se_cpu_memory_event_t event,
                             uint64_t guest_address, uint64_t size,
                             uint32_t protection)
{
    struct fake_backend *backend = userdata;

    (void)instance;
    backend->memory_events++;
    backend->last_event = event;
    backend->last_address = guest_address;
    backend->last_size = size;
    backend->last_protection = protection;
    return 0;
}

static int fake_invalidate(void *userdata, void *instance,
                           uint64_t guest_address, uint64_t size)
{
    struct fake_backend *backend = userdata;

    (void)instance;
    backend->invalidations++;
    backend->last_address = guest_address;
    backend->last_size = size;
    return 0;
}

static void fake_destroy(void *userdata, void *instance)
{
    struct fake_backend *backend = userdata;
    struct fake_cpu *cpu = instance;

    backend->destroys++;
    if (backend->last_cpu == cpu) backend->last_cpu = NULL;
    (void)pthread_cond_destroy(&cpu->changed);
    (void)pthread_mutex_destroy(&cpu->mutex);
    free(cpu);
}

static void init_message(void *message, size_t size)
{
    madeira_se_wine_cpu_message_header_t *header = message;

    memset(message, 0, size);
    header->version = MADEIRA_SE_WINE_CPU_ABI_VERSION;
    header->size = (uint32_t)size;
}

struct run_job {
    const madeira_se_wine_cpu_endpoint_t *endpoint;
    madeira_se_wine_cpu_run_message_t message;
    madeira_se_status_t status;
};

static void *run_job_main(void *opaque)
{
    struct run_job *job = opaque;

    job->status = madeira_se_wine_cpu_dispatch(
        job->endpoint, MADEIRA_SE_WINE_CPU_RUN,
        &job->message, sizeof(job->message));
    return NULL;
}

static int test_host_lifecycle(void)
{
    struct fake_backend state;
    madeira_se_cpu_backend_t backend;
    madeira_se_wine_cpu_host_t *host = NULL;
    const madeira_se_wine_cpu_endpoint_t *endpoint;
    madeira_se_wine_cpu_query_message_t query;
    madeira_se_wine_cpu_process_init_message_t process;
    madeira_se_wine_cpu_thread_init_message_t thread;
    madeira_se_wine_cpu_memory_message_t memory;
    madeira_se_wine_cpu_invalidate_message_t invalidate;
    madeira_se_wine_cpu_interrupt_message_t interrupt;
    madeira_se_wine_cpu_thread_term_message_t thread_term;
    madeira_se_wine_cpu_process_term_message_t process_term;
    struct run_job run;
    pthread_t worker;

    memset(&state, 0, sizeof(state));
    memset(&backend, 0, sizeof(backend));
    backend.version = MADEIRA_SE_CPU_ABI_VERSION;
    backend.name = "test-tcti";
    backend.capabilities = MADEIRA_SE_CPU_CAP_NO_RUNTIME_CODEGEN
                         | MADEIRA_SE_CPU_CAP_X86_32
                         | MADEIRA_SE_CPU_CAP_X86_64;
    backend.userdata = &state;
    backend.create = fake_create;
    backend.run = fake_run;
    backend.interrupt = fake_interrupt;
    backend.memory_event = fake_memory_event;
    backend.invalidate = fake_invalidate;
    backend.destroy = fake_destroy;

    CHECK(madeira_se_wine_cpu_host_create(&backend, &host) == MADEIRA_SE_OK);
    endpoint = madeira_se_wine_cpu_host_endpoint(host);
    CHECK(endpoint != NULL);

    init_message(&query, sizeof(query));
    CHECK(madeira_se_wine_cpu_host_activate(host) == MADEIRA_SE_OK);
    CHECK(madeira_se_wine_cpu_dispatch_message(MADEIRA_SE_WINE_CPU_QUERY,
                                               &query, sizeof(query))
          == MADEIRA_SE_OK);
    CHECK(query.provider_version == MADEIRA_SE_WINE_CPU_ABI_VERSION);
    CHECK(query.capabilities == backend.capabilities);
    madeira_se_wine_cpu_host_deactivate(host);
    CHECK(madeira_se_wine_cpu_dispatch_message(MADEIRA_SE_WINE_CPU_QUERY,
                                               &query, sizeof(query))
          == MADEIRA_SE_E_NOT_READY);
    CHECK(madeira_se_wine_cpu_host_activate(host) == MADEIRA_SE_OK);

    init_message(&process, sizeof(process));
    process.architecture = MADEIRA_SE_ARCH_X86_32;
    process.flags = MADEIRA_SE_WINE_CPU_PROCESS_DIRECT_ADDRESS_SPACE;
    CHECK(madeira_se_wine_cpu_dispatch(endpoint,
                                       MADEIRA_SE_WINE_CPU_PROCESS_INIT,
                                       &process, sizeof(process)) == MADEIRA_SE_OK);
    CHECK(process.process_handle != 0u);

    /* Wine reports initial address-space mappings before its first x86 thread. */
    init_message(&memory, sizeof(memory));
    memory.process_handle = process.process_handle;
    memory.event = MADEIRA_SE_CPU_MEMORY_MAP;
    memory.protection = MADEIRA_SE_MEMORY_READ | MADEIRA_SE_MEMORY_WRITE;
    memory.guest_address = 0x300000u;
    memory.size = 0x1000u;
    CHECK(madeira_se_wine_cpu_dispatch(endpoint,
                                       MADEIRA_SE_WINE_CPU_MEMORY_EVENT,
                                       &memory, sizeof(memory)) == MADEIRA_SE_OK);
    CHECK(state.memory_events == 0u);

    init_message(&thread, sizeof(thread));
    thread.process_handle = process.process_handle;
    thread.thread_id = 42u;
    CHECK(madeira_se_wine_cpu_dispatch(endpoint,
                                       MADEIRA_SE_WINE_CPU_THREAD_INIT,
                                       &thread, sizeof(thread)) == MADEIRA_SE_OK);
    CHECK(thread.thread_handle != 0u);
    CHECK(state.creates == 1u);
    CHECK(state.last_cpu != NULL);
    CHECK(state.last_cpu->architecture == MADEIRA_SE_ARCH_X86_32);
    CHECK(state.memory_events == 1u);
    CHECK(state.last_event == MADEIRA_SE_CPU_MEMORY_MAP);
    CHECK(state.last_address == 0x300000u);

    init_message(&memory, sizeof(memory));
    memory.process_handle = process.process_handle;
    memory.event = MADEIRA_SE_CPU_MEMORY_PROTECT;
    memory.protection = MADEIRA_SE_MEMORY_READ | MADEIRA_SE_MEMORY_EXECUTE;
    memory.guest_address = 0x400000u;
    memory.size = 0x2000u;
    CHECK(madeira_se_wine_cpu_dispatch(endpoint,
                                       MADEIRA_SE_WINE_CPU_MEMORY_EVENT,
                                       &memory, sizeof(memory)) == MADEIRA_SE_OK);
    CHECK(state.memory_events == 2u);
    CHECK(state.last_event == MADEIRA_SE_CPU_MEMORY_PROTECT);
    CHECK(state.last_address == memory.guest_address);
    CHECK(state.last_size == memory.size);
    CHECK(state.last_protection == memory.protection);

    init_message(&invalidate, sizeof(invalidate));
    invalidate.process_handle = process.process_handle;
    invalidate.guest_address = 0x401000u;
    invalidate.size = 0x100u;
    CHECK(madeira_se_wine_cpu_dispatch(endpoint,
                                       MADEIRA_SE_WINE_CPU_INVALIDATE,
                                       &invalidate, sizeof(invalidate))
          == MADEIRA_SE_OK);
    CHECK(state.invalidations == 1u);

    memset(&run, 0, sizeof(run));
    run.endpoint = endpoint;
    init_message(&run.message, sizeof(run.message));
    run.message.thread_handle = thread.thread_handle;
    run.message.request.version = MADEIRA_SE_CPU_ABI_VERSION;
    run.message.request.max_instructions = 1000u;
    run.message.request.syscall_dispatcher = 0x70000000u;
    run.message.request.unix_call_dispatcher = 0x70000002u;
    run.message.context.version = MADEIRA_SE_CPU_ABI_VERSION;
    run.message.context.architecture = MADEIRA_SE_ARCH_X86_32;
    run.message.result.version = MADEIRA_SE_CPU_ABI_VERSION;
    CHECK(pthread_create(&worker, NULL, run_job_main, &run) == 0);

    (void)pthread_mutex_lock(&state.last_cpu->mutex);
    while (!state.last_cpu->running)
        (void)pthread_cond_wait(&state.last_cpu->changed,
                                &state.last_cpu->mutex);
    (void)pthread_mutex_unlock(&state.last_cpu->mutex);

    init_message(&interrupt, sizeof(interrupt));
    interrupt.thread_handle = thread.thread_handle;
    CHECK(madeira_se_wine_cpu_dispatch(endpoint, MADEIRA_SE_WINE_CPU_INTERRUPT,
                                       &interrupt, sizeof(interrupt))
          == MADEIRA_SE_OK);
    CHECK(pthread_join(worker, NULL) == 0);
    CHECK(run.status == MADEIRA_SE_OK);
    CHECK(run.message.result.reason == MADEIRA_SE_CPU_EXIT_INTERRUPTED);
    CHECK(run.message.result.instructions_executed == 17u);
    CHECK(run.message.context.gpr[MADEIRA_SE_X86_RAX] == 0xfeedfaceu);
    CHECK(state.runs == 1u);
    CHECK(state.interrupts == 1u);

    init_message(&thread_term, sizeof(thread_term));
    thread_term.thread_handle = thread.thread_handle;
    CHECK(madeira_se_wine_cpu_dispatch(endpoint,
                                       MADEIRA_SE_WINE_CPU_THREAD_TERM,
                                       &thread_term, sizeof(thread_term))
          == MADEIRA_SE_OK);
    CHECK(state.destroys == 1u);

    init_message(&process_term, sizeof(process_term));
    process_term.process_handle = process.process_handle;
    CHECK(madeira_se_wine_cpu_dispatch(endpoint,
                                       MADEIRA_SE_WINE_CPU_PROCESS_TERM,
                                       &process_term, sizeof(process_term))
          == MADEIRA_SE_OK);

    CHECK(madeira_se_wine_cpu_dispatch(endpoint, MADEIRA_SE_WINE_CPU_RUN,
                                       &run.message, sizeof(run.message))
          == MADEIRA_SE_E_NOT_READY);

    madeira_se_wine_cpu_host_destroy(host);
    CHECK(state.creates == state.destroys);
    return 0;
}

static int test_process_term_cleans_threads(void)
{
    struct fake_backend state;
    madeira_se_cpu_backend_t backend;
    madeira_se_wine_cpu_host_t *host = NULL;
    const madeira_se_wine_cpu_endpoint_t *endpoint;
    madeira_se_wine_cpu_process_init_message_t process;
    madeira_se_wine_cpu_thread_init_message_t thread;
    madeira_se_wine_cpu_process_term_message_t process_term;

    memset(&state, 0, sizeof(state));
    memset(&backend, 0, sizeof(backend));
    backend.version = MADEIRA_SE_CPU_ABI_VERSION;
    backend.name = "test-tcti";
    backend.capabilities = MADEIRA_SE_CPU_CAP_NO_RUNTIME_CODEGEN
                         | MADEIRA_SE_CPU_CAP_X86_64;
    backend.userdata = &state;
    backend.create = fake_create;
    backend.run = fake_run;
    backend.interrupt = fake_interrupt;
    backend.memory_event = fake_memory_event;
    backend.invalidate = fake_invalidate;
    backend.destroy = fake_destroy;

    CHECK(madeira_se_wine_cpu_host_create(&backend, &host) == MADEIRA_SE_OK);
    endpoint = madeira_se_wine_cpu_host_endpoint(host);

    init_message(&process, sizeof(process));
    process.architecture = MADEIRA_SE_ARCH_X86_64;
    process.flags = MADEIRA_SE_WINE_CPU_PROCESS_DIRECT_ADDRESS_SPACE;
    CHECK(madeira_se_wine_cpu_dispatch(endpoint,
                                       MADEIRA_SE_WINE_CPU_PROCESS_INIT,
                                       &process, sizeof(process)) == MADEIRA_SE_OK);
    init_message(&thread, sizeof(thread));
    thread.process_handle = process.process_handle;
    thread.thread_id = 7u;
    CHECK(madeira_se_wine_cpu_dispatch(endpoint,
                                       MADEIRA_SE_WINE_CPU_THREAD_INIT,
                                       &thread, sizeof(thread)) == MADEIRA_SE_OK);

    init_message(&process_term, sizeof(process_term));
    process_term.process_handle = process.process_handle;
    CHECK(madeira_se_wine_cpu_dispatch(endpoint,
                                       MADEIRA_SE_WINE_CPU_PROCESS_TERM,
                                       &process_term, sizeof(process_term))
          == MADEIRA_SE_OK);
    CHECK(state.creates == 1u);
    CHECK(state.destroys == 1u);

    madeira_se_wine_cpu_host_destroy(host);
    return 0;
}

static int test_biased_address_space(void)
{
    const uint64_t bias = UINT64_C(0x7000000000);
    struct fake_backend state;
    madeira_se_cpu_backend_t backend;
    madeira_se_wine_cpu_host_t *host = NULL;
    const madeira_se_wine_cpu_endpoint_t *endpoint;
    madeira_se_wine_cpu_process_init_message_t process;
    madeira_se_wine_cpu_thread_init_message_t thread;
    madeira_se_wine_cpu_process_term_message_t process_term;
    void *translated;

    memset(&state, 0, sizeof(state));
    memset(&backend, 0, sizeof(backend));
    backend.version = MADEIRA_SE_CPU_ABI_VERSION;
    backend.name = "test-tcti";
    backend.capabilities = MADEIRA_SE_CPU_CAP_NO_RUNTIME_CODEGEN
                         | MADEIRA_SE_CPU_CAP_X86_32;
    backend.userdata = &state;
    backend.create = fake_create;
    backend.run = fake_run;
    backend.interrupt = fake_interrupt;
    backend.memory_event = fake_memory_event;
    backend.invalidate = fake_invalidate;
    backend.destroy = fake_destroy;

    CHECK(madeira_se_wine_cpu_host_create(&backend, &host) == MADEIRA_SE_OK);
    endpoint = madeira_se_wine_cpu_host_endpoint(host);

    init_message(&process, sizeof(process));
    process.architecture = MADEIRA_SE_ARCH_X86_32;
    process.flags = MADEIRA_SE_WINE_CPU_PROCESS_BIASED_ADDRESS_SPACE;
    process.guest_address_bias = bias;
    CHECK(madeira_se_wine_cpu_dispatch(endpoint,
                                       MADEIRA_SE_WINE_CPU_PROCESS_INIT,
                                       &process, sizeof(process)) == MADEIRA_SE_OK);

    init_message(&thread, sizeof(thread));
    thread.process_handle = process.process_handle;
    thread.thread_id = 9u;
    CHECK(madeira_se_wine_cpu_dispatch(endpoint,
                                       MADEIRA_SE_WINE_CPU_THREAD_INIT,
                                       &thread, sizeof(thread)) == MADEIRA_SE_OK);
    CHECK(state.last_cpu != NULL);
    translated = state.last_cpu->memory.translate(
        state.last_cpu->memory.userdata, 0x1234u, 4u,
        MADEIRA_SE_MEMORY_READ);
    CHECK(translated == (void *)(uintptr_t)(bias + 0x1234u));

    init_message(&process_term, sizeof(process_term));
    process_term.process_handle = process.process_handle;
    CHECK(madeira_se_wine_cpu_dispatch(endpoint,
                                       MADEIRA_SE_WINE_CPU_PROCESS_TERM,
                                       &process_term, sizeof(process_term))
          == MADEIRA_SE_OK);
    madeira_se_wine_cpu_host_destroy(host);
    return 0;
}

static int test_split_low_4g_address_space(void)
{
    const uint64_t bias = UINT64_C(0x7000000000);
    struct fake_backend state;
    madeira_se_cpu_backend_t backend;
    madeira_se_wine_cpu_host_t *host = NULL;
    const madeira_se_wine_cpu_endpoint_t *endpoint;
    madeira_se_wine_cpu_process_init_message_t process;
    madeira_se_wine_cpu_thread_init_message_t thread;
    madeira_se_wine_cpu_process_term_message_t process_term;
    void *translated;

    memset(&state, 0, sizeof(state));
    memset(&backend, 0, sizeof(backend));
    backend.version = MADEIRA_SE_CPU_ABI_VERSION;
    backend.name = "test-tcti";
    backend.capabilities = MADEIRA_SE_CPU_CAP_NO_RUNTIME_CODEGEN
                         | MADEIRA_SE_CPU_CAP_X86_64;
    backend.userdata = &state;
    backend.create = fake_create;
    backend.run = fake_run;
    backend.interrupt = fake_interrupt;
    backend.memory_event = fake_memory_event;
    backend.invalidate = fake_invalidate;
    backend.destroy = fake_destroy;

    CHECK(madeira_se_wine_cpu_host_create(&backend, &host) == MADEIRA_SE_OK);
    endpoint = madeira_se_wine_cpu_host_endpoint(host);
    init_message(&process, sizeof(process));
    process.architecture = MADEIRA_SE_ARCH_X86_64;
    process.flags = MADEIRA_SE_WINE_CPU_PROCESS_SPLIT_LOW_4G_ADDRESS_SPACE;
    process.guest_address_bias = bias;
    CHECK(madeira_se_wine_cpu_dispatch(endpoint,
                                       MADEIRA_SE_WINE_CPU_PROCESS_INIT,
                                       &process, sizeof(process)) == MADEIRA_SE_OK);
    init_message(&thread, sizeof(thread));
    thread.process_handle = process.process_handle;
    thread.thread_id = 10u;
    CHECK(madeira_se_wine_cpu_dispatch(endpoint,
                                       MADEIRA_SE_WINE_CPU_THREAD_INIT,
                                       &thread, sizeof(thread)) == MADEIRA_SE_OK);
    translated = state.last_cpu->memory.translate(
        state.last_cpu->memory.userdata, UINT64_C(0x7ffe1000), 8u,
        MADEIRA_SE_MEMORY_READ);
    CHECK(translated == (void *)(uintptr_t)(bias + UINT64_C(0x7ffe1000)));
    translated = state.last_cpu->memory.translate(
        state.last_cpu->memory.userdata, UINT64_C(0x140000000), 8u,
        MADEIRA_SE_MEMORY_READ);
    CHECK(translated == (void *)(uintptr_t)UINT64_C(0x140000000));
    CHECK(state.last_cpu->memory.translate(
              state.last_cpu->memory.userdata, UINT64_C(0xfffffff8), 16u,
              MADEIRA_SE_MEMORY_READ) == NULL);

    init_message(&process_term, sizeof(process_term));
    process_term.process_handle = process.process_handle;
    CHECK(madeira_se_wine_cpu_dispatch(endpoint,
                                       MADEIRA_SE_WINE_CPU_PROCESS_TERM,
                                       &process_term, sizeof(process_term))
          == MADEIRA_SE_OK);
    madeira_se_wine_cpu_host_destroy(host);
    return 0;
}

static int test_rejects_codegen_backend(void)
{
    madeira_se_cpu_backend_t backend;
    madeira_se_wine_cpu_host_t *host = NULL;

    memset(&backend, 0, sizeof(backend));
    backend.version = MADEIRA_SE_CPU_ABI_VERSION;
    backend.name = "jit";
    backend.capabilities = MADEIRA_SE_CPU_CAP_X86_32;
    backend.create = fake_create;
    backend.run = fake_run;
    backend.destroy = fake_destroy;
    CHECK(madeira_se_wine_cpu_host_create(&backend, &host)
          == MADEIRA_SE_E_UNSUPPORTED);
    CHECK(host == NULL);
    return 0;
}

int main(void)
{
    CHECK(test_host_lifecycle() == 0);
    CHECK(test_process_term_cleans_threads() == 0);
    CHECK(test_biased_address_space() == 0);
    CHECK(test_split_low_4g_address_space() == 0);
    CHECK(test_rejects_codegen_backend() == 0);
    puts("Madeira-SE Wine CPU host tests passed");
    return 0;
}
