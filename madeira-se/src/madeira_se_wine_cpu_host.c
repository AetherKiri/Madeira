/*
 * Madeira-SE host implementation of the Wine CPU-provider transport.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "madeira_se_wine_cpu_host.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct madeira_se_wine_memory_event {
    madeira_se_cpu_memory_event_t event;
    uint64_t guest_address;
    uint64_t size;
    uint32_t protection;
    struct madeira_se_wine_memory_event *next;
} madeira_se_wine_memory_event_t;

typedef struct madeira_se_wine_process {
    uint64_t handle;
    madeira_se_architecture_t architecture;
    uint32_t flags;
    uint64_t guest_address_bias;
    int terminating;
    madeira_se_wine_memory_event_t *memory_events;
    madeira_se_wine_memory_event_t *memory_events_tail;
    struct madeira_se_wine_process *next;
} madeira_se_wine_process_t;

typedef struct madeira_se_wine_thread {
    uint64_t handle;
    uint64_t process_handle;
    uint64_t thread_id;
    madeira_se_cpu_t *cpu;
    unsigned int active_calls;
    int terminating;
    struct madeira_se_wine_thread *next;
} madeira_se_wine_thread_t;

struct madeira_se_wine_cpu_host {
    madeira_se_cpu_backend_t backend;
    madeira_se_wine_cpu_endpoint_t endpoint;
    madeira_se_wine_process_t *processes;
    madeira_se_wine_thread_t *threads;
    pthread_mutex_t mutex;
    pthread_cond_t idle;
    uint64_t next_handle;
    unsigned int active_dispatches;
    int shutting_down;
};

static pthread_mutex_t active_host_mutex = PTHREAD_MUTEX_INITIALIZER;
static madeira_se_wine_cpu_host_t *active_host;

static int version_is_supported(uint32_t version)
{
    return version == 0u || version == MADEIRA_SE_CPU_ABI_VERSION;
}

static void *process_translate(void *userdata, uint64_t guest_address,
                               size_t size, uint32_t access)
{
    const madeira_se_wine_process_t *process = userdata;
    uint64_t bias;
    uint64_t host_address;

    (void)access;
    if (process == NULL || guest_address == 0u || size == 0u
        || guest_address > (uint64_t)UINTPTR_MAX)
        return NULL;
    bias = process->guest_address_bias;
    if ((process->flags
         & MADEIRA_SE_WINE_CPU_PROCESS_SPLIT_LOW_4G_ADDRESS_SPACE) != 0u) {
        if (guest_address >= UINT64_C(0x100000000)) bias = 0u;
        else if ((uint64_t)size > UINT64_C(0x100000000) - guest_address)
            return NULL;
    }
    if (bias > (uint64_t)UINTPTR_MAX - guest_address) return NULL;
    host_address = bias + guest_address;
    if ((uint64_t)size > (uint64_t)UINTPTR_MAX - host_address)
        return NULL;
    return (void *)(uintptr_t)host_address;
}

static madeira_se_wine_process_t *find_process_locked(
    const madeira_se_wine_cpu_host_t *host, uint64_t handle)
{
    madeira_se_wine_process_t *process;

    for (process = host->processes; process != NULL; process = process->next)
        if (process->handle == handle) return process;
    return NULL;
}

static madeira_se_wine_thread_t *find_thread_locked(
    const madeira_se_wine_cpu_host_t *host, uint64_t handle)
{
    madeira_se_wine_thread_t *thread;

    for (thread = host->threads; thread != NULL; thread = thread->next)
        if (thread->handle == handle) return thread;
    return NULL;
}

static uint64_t allocate_handle_locked(madeira_se_wine_cpu_host_t *host)
{
    uint64_t handle = host->next_handle++;

    if (handle == 0u) handle = host->next_handle++;
    return handle;
}

static madeira_se_status_t retain_thread_locked(
    madeira_se_wine_cpu_host_t *host, uint64_t handle,
    madeira_se_wine_thread_t **out_thread)
{
    madeira_se_wine_thread_t *thread;

    thread = find_thread_locked(host, handle);
    if (thread == NULL || thread->terminating || host->shutting_down)
        return MADEIRA_SE_E_NOT_READY;
    thread->active_calls++;
    *out_thread = thread;
    return MADEIRA_SE_OK;
}

static void release_thread(madeira_se_wine_cpu_host_t *host,
                           madeira_se_wine_thread_t *thread)
{
    (void)pthread_mutex_lock(&host->mutex);
    if (thread->active_calls > 0u) thread->active_calls--;
    if (thread->active_calls == 0u)
        (void)pthread_cond_broadcast(&host->idle);
    (void)pthread_mutex_unlock(&host->mutex);
}

static int all_threads_idle_locked(const madeira_se_wine_cpu_host_t *host)
{
    madeira_se_wine_thread_t *thread;

    for (thread = host->threads; thread != NULL; thread = thread->next)
        if (thread->active_calls != 0u) return 0;
    return 1;
}

static void destroy_thread_list(madeira_se_wine_thread_t *threads)
{
    while (threads != NULL) {
        madeira_se_wine_thread_t *next = threads->next;
        madeira_se_cpu_destroy(threads->cpu);
        free(threads);
        threads = next;
    }
}

static void destroy_process_list(madeira_se_wine_process_t *processes)
{
    while (processes != NULL) {
        madeira_se_wine_process_t *next = processes->next;
        madeira_se_wine_memory_event_t *event = processes->memory_events;

        while (event != NULL) {
            madeira_se_wine_memory_event_t *next_event = event->next;
            free(event);
            event = next_event;
        }
        free(processes);
        processes = next;
    }
}

static madeira_se_status_t record_memory_event(
    madeira_se_wine_cpu_host_t *host,
    const madeira_se_wine_cpu_memory_message_t *params)
{
    madeira_se_wine_memory_event_t *event;
    madeira_se_wine_process_t *process;

    /* Dirty notifications only invalidate translations that already exist. */
    if (params->event == MADEIRA_SE_CPU_MEMORY_DIRTY)
        return MADEIRA_SE_OK;

    event = calloc(1u, sizeof(*event));
    if (event == NULL) return MADEIRA_SE_E_OUT_OF_MEMORY;
    event->event = (madeira_se_cpu_memory_event_t)params->event;
    event->guest_address = params->guest_address;
    event->size = params->size;
    event->protection = params->protection;

    (void)pthread_mutex_lock(&host->mutex);
    process = find_process_locked(host, params->process_handle);
    if (host->shutting_down || process == NULL || process->terminating) {
        (void)pthread_mutex_unlock(&host->mutex);
        free(event);
        return MADEIRA_SE_E_NOT_READY;
    }
    if (process->memory_events_tail != NULL)
        process->memory_events_tail->next = event;
    else
        process->memory_events = event;
    process->memory_events_tail = event;
    (void)pthread_mutex_unlock(&host->mutex);
    return MADEIRA_SE_OK;
}

static int32_t dispatch_query(madeira_se_wine_cpu_host_t *host,
                              madeira_se_wine_cpu_query_message_t *params)
{
    params->provider_version = MADEIRA_SE_WINE_CPU_ABI_VERSION;
    params->capabilities = host->backend.capabilities;
    return (int32_t)MADEIRA_SE_OK;
}

static int32_t dispatch_process_init(
    madeira_se_wine_cpu_host_t *host,
    madeira_se_wine_cpu_process_init_message_t *params)
{
    madeira_se_wine_process_t *process = calloc(1u, sizeof(*process));

    if (process == NULL) return (int32_t)MADEIRA_SE_E_OUT_OF_MEMORY;
    process->architecture = (madeira_se_architecture_t)params->architecture;
    process->flags = params->flags;
    process->guest_address_bias = params->guest_address_bias;

    (void)pthread_mutex_lock(&host->mutex);
    if (host->shutting_down) {
        (void)pthread_mutex_unlock(&host->mutex);
        free(process);
        return (int32_t)MADEIRA_SE_E_NOT_READY;
    }
    process->handle = allocate_handle_locked(host);
    process->next = host->processes;
    host->processes = process;
    params->process_handle = process->handle;
    (void)pthread_mutex_unlock(&host->mutex);
    return (int32_t)MADEIRA_SE_OK;
}

static int32_t dispatch_thread_init(
    madeira_se_wine_cpu_host_t *host,
    madeira_se_wine_cpu_thread_init_message_t *params)
{
    madeira_se_wine_thread_t *thread;
    madeira_se_wine_process_t *process;
    madeira_se_wine_memory_event_t *event;
    madeira_se_memory_t memory;
    madeira_se_status_t status;

    thread = calloc(1u, sizeof(*thread));
    if (thread == NULL) return (int32_t)MADEIRA_SE_E_OUT_OF_MEMORY;

    memset(&memory, 0, sizeof(memory));
    memory.version = MADEIRA_SE_CPU_ABI_VERSION;
    memory.translate = process_translate;

    (void)pthread_mutex_lock(&host->mutex);
    process = find_process_locked(host, params->process_handle);
    if (host->shutting_down || process == NULL || process->terminating) {
        (void)pthread_mutex_unlock(&host->mutex);
        free(thread);
        return (int32_t)MADEIRA_SE_E_NOT_READY;
    }
    memory.userdata = process;
    status = madeira_se_cpu_create(&host->backend, process->architecture,
                                   &memory, &thread->cpu);
    if (status != MADEIRA_SE_OK) {
        (void)pthread_mutex_unlock(&host->mutex);
        free(thread);
        return (int32_t)status;
    }
    for (event = process->memory_events; event != NULL; event = event->next) {
        status = madeira_se_cpu_notify_memory(
            thread->cpu, event->event, event->guest_address, event->size,
            event->protection);
        if (status != MADEIRA_SE_OK) {
            madeira_se_cpu_destroy(thread->cpu);
            (void)pthread_mutex_unlock(&host->mutex);
            free(thread);
            return (int32_t)status;
        }
    }
    thread->handle = allocate_handle_locked(host);
    thread->process_handle = process->handle;
    thread->thread_id = params->thread_id;
    thread->next = host->threads;
    host->threads = thread;
    params->thread_handle = thread->handle;
    (void)pthread_mutex_unlock(&host->mutex);
    return (int32_t)MADEIRA_SE_OK;
}

static int32_t dispatch_run(madeira_se_wine_cpu_host_t *host,
                            madeira_se_wine_cpu_run_message_t *params)
{
    madeira_se_wine_thread_t *thread;
    madeira_se_status_t status;

    (void)pthread_mutex_lock(&host->mutex);
    status = retain_thread_locked(host, params->thread_handle, &thread);
    (void)pthread_mutex_unlock(&host->mutex);
    if (status != MADEIRA_SE_OK) return (int32_t)status;

    status = madeira_se_cpu_run(thread->cpu, &params->request,
                                &params->context, &params->result);
    release_thread(host, thread);
    return (int32_t)status;
}

static int32_t dispatch_interrupt(
    madeira_se_wine_cpu_host_t *host,
    const madeira_se_wine_cpu_interrupt_message_t *params)
{
    madeira_se_wine_thread_t *thread;
    madeira_se_status_t status;

    (void)pthread_mutex_lock(&host->mutex);
    status = retain_thread_locked(host, params->thread_handle, &thread);
    (void)pthread_mutex_unlock(&host->mutex);
    if (status != MADEIRA_SE_OK) return (int32_t)status;
    status = madeira_se_cpu_interrupt(thread->cpu);
    release_thread(host, thread);
    return (int32_t)status;
}

static madeira_se_status_t retain_process_threads(
    madeira_se_wine_cpu_host_t *host, uint64_t process_handle,
    madeira_se_wine_thread_t ***out_threads, size_t *out_count)
{
    madeira_se_wine_thread_t **threads;
    madeira_se_wine_thread_t *thread;
    madeira_se_wine_process_t *process;
    size_t count = 0u;
    size_t index = 0u;

    *out_threads = NULL;
    *out_count = 0u;
    (void)pthread_mutex_lock(&host->mutex);
    process = find_process_locked(host, process_handle);
    if (host->shutting_down || process == NULL || process->terminating) {
        (void)pthread_mutex_unlock(&host->mutex);
        return MADEIRA_SE_E_NOT_READY;
    }
    for (thread = host->threads; thread != NULL; thread = thread->next)
        if (thread->process_handle == process_handle && !thread->terminating)
            count++;
    if (count == 0u) {
        (void)pthread_mutex_unlock(&host->mutex);
        return MADEIRA_SE_OK;
    }
    threads = calloc(count, sizeof(*threads));
    if (threads == NULL) {
        (void)pthread_mutex_unlock(&host->mutex);
        return MADEIRA_SE_E_OUT_OF_MEMORY;
    }
    for (thread = host->threads; thread != NULL; thread = thread->next) {
        if (thread->process_handle != process_handle || thread->terminating)
            continue;
        thread->active_calls++;
        threads[index++] = thread;
    }
    (void)pthread_mutex_unlock(&host->mutex);
    *out_threads = threads;
    *out_count = count;
    return MADEIRA_SE_OK;
}

static int32_t dispatch_memory_event(
    madeira_se_wine_cpu_host_t *host,
    const madeira_se_wine_cpu_memory_message_t *params)
{
    madeira_se_wine_thread_t **threads;
    madeira_se_status_t status;
    madeira_se_status_t first_error = MADEIRA_SE_OK;
    size_t count;
    size_t index;

    status = record_memory_event(host, params);
    if (status != MADEIRA_SE_OK) return (int32_t)status;
    status = retain_process_threads(host, params->process_handle, &threads, &count);
    if (status != MADEIRA_SE_OK) return (int32_t)status;
    for (index = 0u; index < count; ++index) {
        status = madeira_se_cpu_notify_memory(
            threads[index]->cpu, (madeira_se_cpu_memory_event_t)params->event,
            params->guest_address, params->size, params->protection);
        if (first_error == MADEIRA_SE_OK && status != MADEIRA_SE_OK)
            first_error = status;
        release_thread(host, threads[index]);
    }
    free(threads);
    return (int32_t)first_error;
}

static int32_t dispatch_invalidate(
    madeira_se_wine_cpu_host_t *host,
    const madeira_se_wine_cpu_invalidate_message_t *params)
{
    madeira_se_wine_thread_t **threads;
    madeira_se_status_t status;
    madeira_se_status_t first_error = MADEIRA_SE_OK;
    size_t count;
    size_t index;

    status = retain_process_threads(host, params->process_handle, &threads, &count);
    if (status != MADEIRA_SE_OK) return (int32_t)status;
    for (index = 0u; index < count; ++index) {
        status = madeira_se_cpu_invalidate(threads[index]->cpu,
                                           params->guest_address, params->size);
        if (first_error == MADEIRA_SE_OK && status != MADEIRA_SE_OK)
            first_error = status;
        release_thread(host, threads[index]);
    }
    free(threads);
    return (int32_t)first_error;
}

static int32_t dispatch_thread_term(
    madeira_se_wine_cpu_host_t *host,
    const madeira_se_wine_cpu_thread_term_message_t *params)
{
    madeira_se_wine_thread_t **link;
    madeira_se_wine_thread_t *thread;

    (void)pthread_mutex_lock(&host->mutex);
    for (link = &host->threads; *link != NULL; link = &(*link)->next)
        if ((*link)->handle == params->thread_handle) break;
    thread = *link;
    if (thread == NULL || thread->terminating) {
        (void)pthread_mutex_unlock(&host->mutex);
        return (int32_t)MADEIRA_SE_E_NOT_READY;
    }
    thread->terminating = 1;
    while (thread->active_calls != 0u)
        (void)pthread_cond_wait(&host->idle, &host->mutex);
    *link = thread->next;
    (void)pthread_mutex_unlock(&host->mutex);

    thread->next = NULL;
    destroy_thread_list(thread);
    return (int32_t)MADEIRA_SE_OK;
}

static int32_t dispatch_process_term(
    madeira_se_wine_cpu_host_t *host,
    const madeira_se_wine_cpu_process_term_message_t *params)
{
    madeira_se_wine_process_t **process_link;
    madeira_se_wine_process_t *process;
    madeira_se_wine_thread_t **thread_link;
    madeira_se_wine_thread_t *detached = NULL;

    (void)pthread_mutex_lock(&host->mutex);
    for (process_link = &host->processes; *process_link != NULL;
         process_link = &(*process_link)->next)
        if ((*process_link)->handle == params->process_handle) break;
    process = *process_link;
    if (process == NULL || process->terminating) {
        (void)pthread_mutex_unlock(&host->mutex);
        return (int32_t)MADEIRA_SE_E_NOT_READY;
    }
    process->terminating = 1;
    for (thread_link = &host->threads; *thread_link != NULL;
         thread_link = &(*thread_link)->next)
        if ((*thread_link)->process_handle == process->handle)
            (*thread_link)->terminating = 1;

    for (;;) {
        int busy = 0;
        madeira_se_wine_thread_t *thread;
        for (thread = host->threads; thread != NULL; thread = thread->next)
            if (thread->process_handle == process->handle
                && thread->active_calls != 0u) {
                busy = 1;
                break;
            }
        if (!busy) break;
        (void)pthread_cond_wait(&host->idle, &host->mutex);
    }

    thread_link = &host->threads;
    while (*thread_link != NULL) {
        madeira_se_wine_thread_t *thread = *thread_link;
        if (thread->process_handle != process->handle) {
            thread_link = &thread->next;
            continue;
        }
        *thread_link = thread->next;
        thread->next = detached;
        detached = thread;
    }
    *process_link = process->next;
    (void)pthread_mutex_unlock(&host->mutex);

    destroy_thread_list(detached);
    process->next = NULL;
    destroy_process_list(process);
    return (int32_t)MADEIRA_SE_OK;
}

static int32_t host_dispatch(void *userdata, uint32_t operation,
                             void *message, uint32_t message_size)
{
    madeira_se_wine_cpu_host_t *host = userdata;
    madeira_se_status_t status;

    if (host == NULL) return (int32_t)MADEIRA_SE_E_INVALID_ARGUMENT;
    status = madeira_se_wine_cpu_validate_message(operation, message,
                                                  message_size);
    if (status != MADEIRA_SE_OK) return (int32_t)status;

    switch (operation) {
    case MADEIRA_SE_WINE_CPU_QUERY:
        return dispatch_query(host, message);
    case MADEIRA_SE_WINE_CPU_PROCESS_INIT:
        return dispatch_process_init(host, message);
    case MADEIRA_SE_WINE_CPU_PROCESS_TERM:
        return dispatch_process_term(host, message);
    case MADEIRA_SE_WINE_CPU_THREAD_INIT:
        return dispatch_thread_init(host, message);
    case MADEIRA_SE_WINE_CPU_THREAD_TERM:
        return dispatch_thread_term(host, message);
    case MADEIRA_SE_WINE_CPU_RUN:
        return dispatch_run(host, message);
    case MADEIRA_SE_WINE_CPU_MEMORY_EVENT:
        return dispatch_memory_event(host, message);
    case MADEIRA_SE_WINE_CPU_INVALIDATE:
        return dispatch_invalidate(host, message);
    case MADEIRA_SE_WINE_CPU_INTERRUPT:
        return dispatch_interrupt(host, message);
    default:
        return (int32_t)MADEIRA_SE_E_INVALID_ARGUMENT;
    }
}

madeira_se_status_t madeira_se_wine_cpu_host_create(
    const madeira_se_cpu_backend_t *backend,
    madeira_se_wine_cpu_host_t **out_host)
{
    madeira_se_wine_cpu_host_t *host;
    uint32_t architectures = MADEIRA_SE_CPU_CAP_X86_32
                           | MADEIRA_SE_CPU_CAP_X86_64;

    if (out_host == NULL) return MADEIRA_SE_E_INVALID_ARGUMENT;
    *out_host = NULL;
    if (backend == NULL || backend->name == NULL || backend->create == NULL
        || backend->run == NULL || backend->destroy == NULL)
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    if (!version_is_supported(backend->version)) return MADEIRA_SE_E_UNSUPPORTED;
    if ((backend->capabilities & MADEIRA_SE_CPU_CAP_NO_RUNTIME_CODEGEN) == 0u)
        return MADEIRA_SE_E_UNSUPPORTED;
    if ((backend->capabilities & architectures) == 0u)
        return MADEIRA_SE_E_UNSUPPORTED_ARCHITECTURE;

    host = calloc(1u, sizeof(*host));
    if (host == NULL) return MADEIRA_SE_E_OUT_OF_MEMORY;
    if (pthread_mutex_init(&host->mutex, NULL) != 0) {
        free(host);
        return MADEIRA_SE_E_BACKEND;
    }
    if (pthread_cond_init(&host->idle, NULL) != 0) {
        (void)pthread_mutex_destroy(&host->mutex);
        free(host);
        return MADEIRA_SE_E_BACKEND;
    }
    host->backend = *backend;
    host->next_handle = 1u;
    host->endpoint.version = MADEIRA_SE_WINE_CPU_ABI_VERSION;
    host->endpoint.size = sizeof(host->endpoint);
    host->endpoint.userdata = host;
    host->endpoint.dispatch = host_dispatch;
    *out_host = host;
    return MADEIRA_SE_OK;
}

void madeira_se_wine_cpu_host_destroy(madeira_se_wine_cpu_host_t *host)
{
    madeira_se_wine_thread_t *threads;
    madeira_se_wine_process_t *processes;
    madeira_se_wine_thread_t *thread;

    if (host == NULL) return;
    (void)pthread_mutex_lock(&active_host_mutex);
    (void)pthread_mutex_lock(&host->mutex);
    if (active_host == host) active_host = NULL;
    host->shutting_down = 1;
    (void)pthread_mutex_unlock(&active_host_mutex);
    for (thread = host->threads; thread != NULL; thread = thread->next)
        thread->terminating = 1;
    while (host->active_dispatches != 0u || !all_threads_idle_locked(host))
        (void)pthread_cond_wait(&host->idle, &host->mutex);
    threads = host->threads;
    processes = host->processes;
    host->threads = NULL;
    host->processes = NULL;
    (void)pthread_mutex_unlock(&host->mutex);

    destroy_thread_list(threads);
    destroy_process_list(processes);
    (void)pthread_cond_destroy(&host->idle);
    (void)pthread_mutex_destroy(&host->mutex);
    free(host);
}

const madeira_se_wine_cpu_endpoint_t *madeira_se_wine_cpu_host_endpoint(
    madeira_se_wine_cpu_host_t *host)
{
    return host != NULL ? &host->endpoint : NULL;
}

madeira_se_status_t madeira_se_wine_cpu_host_activate(
    madeira_se_wine_cpu_host_t *host)
{
    madeira_se_status_t status = MADEIRA_SE_OK;

    if (host == NULL) return MADEIRA_SE_E_INVALID_ARGUMENT;
    (void)pthread_mutex_lock(&active_host_mutex);
    (void)pthread_mutex_lock(&host->mutex);
    if (host->shutting_down)
        status = MADEIRA_SE_E_NOT_READY;
    else if (active_host != NULL && active_host != host)
        status = MADEIRA_SE_E_INVALID_STATE;
    else
        active_host = host;
    (void)pthread_mutex_unlock(&host->mutex);
    (void)pthread_mutex_unlock(&active_host_mutex);
    return status;
}

void madeira_se_wine_cpu_host_deactivate(madeira_se_wine_cpu_host_t *host)
{
    (void)pthread_mutex_lock(&active_host_mutex);
    if (active_host == host) active_host = NULL;
    (void)pthread_mutex_unlock(&active_host_mutex);
}

int32_t madeira_se_wine_cpu_dispatch_message(uint32_t operation,
                                             void *message,
                                             uint32_t message_size)
{
    madeira_se_wine_cpu_host_t *host;
    madeira_se_status_t status;

    (void)pthread_mutex_lock(&active_host_mutex);
    host = active_host;
    if (host == NULL) {
        (void)pthread_mutex_unlock(&active_host_mutex);
        return (int32_t)MADEIRA_SE_E_NOT_READY;
    }
    (void)pthread_mutex_lock(&host->mutex);
    if (host->shutting_down) {
        (void)pthread_mutex_unlock(&host->mutex);
        (void)pthread_mutex_unlock(&active_host_mutex);
        return (int32_t)MADEIRA_SE_E_NOT_READY;
    }
    host->active_dispatches++;
    (void)pthread_mutex_unlock(&host->mutex);
    (void)pthread_mutex_unlock(&active_host_mutex);

    status = madeira_se_wine_cpu_dispatch(&host->endpoint, operation, message,
                                          message_size);

    (void)pthread_mutex_lock(&host->mutex);
    host->active_dispatches--;
    if (host->active_dispatches == 0u)
        (void)pthread_cond_broadcast(&host->idle);
    (void)pthread_mutex_unlock(&host->mutex);
    return (int32_t)status;
}
