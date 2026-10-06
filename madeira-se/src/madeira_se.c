/*
 * Madeira-SE standalone runtime boundary.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "madeira_se.h"

#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct madeira_se_runtime {
    madeira_se_host_t host;
    madeira_se_executor_t executor;
    madeira_se_state_t state;
    madeira_se_architecture_t architecture;
    char *game_root;
    char *executable;
    char *working_directory;
    char last_error[256];
    int audio_opened;
    madeira_se_audio_format_t audio_format;
};

static void clear_error(madeira_se_runtime_t *runtime)
{
    if (runtime != NULL) runtime->last_error[0] = '\0';
}

static madeira_se_status_t fail(madeira_se_runtime_t *runtime,
                                madeira_se_status_t status,
                                const char *message)
{
    if (runtime != NULL) {
        if (message != NULL) {
            (void)snprintf(runtime->last_error, sizeof(runtime->last_error), "%s", message);
            if (runtime->host.log != NULL)
                runtime->host.log(runtime->host.userdata, MADEIRA_SE_LOG_ERROR, runtime->last_error);
        } else {
            clear_error(runtime);
        }
    }
    return status;
}

static int version_is_supported(uint32_t version)
{
    return version == 0u || version == MADEIRA_SE_ABI_VERSION;
}

static char *duplicate_string(const char *value)
{
    size_t length;
    char *copy;

    if (value == NULL) return NULL;
    length = strlen(value);
    copy = (char *)malloc(length + 1u);
    if (copy == NULL) return NULL;
    memcpy(copy, value, length + 1u);
    return copy;
}

static int is_absolute_path(const char *path)
{
    if (path == NULL || path[0] == '\0') return 0;
    return path[0] == '/';
}

static char *join_path(const char *root, const char *path)
{
    size_t root_length;
    size_t path_length;
    int needs_separator;
    char *joined;

    if (path == NULL) return NULL;
    if (is_absolute_path(path) || root == NULL || root[0] == '\0') return duplicate_string(path);

    root_length = strlen(root);
    path_length = strlen(path);
    needs_separator = root_length > 0u && root[root_length - 1u] != '/';
    joined = (char *)malloc(root_length + (size_t)needs_separator + path_length + 1u);
    if (joined == NULL) return NULL;
    memcpy(joined, root, root_length);
    if (needs_separator) joined[root_length] = '/';
    memcpy(joined + root_length + (size_t)needs_separator, path, path_length + 1u);
    return joined;
}

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8u);
}

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0]
         | ((uint32_t)data[1] << 8u)
         | ((uint32_t)data[2] << 16u)
         | ((uint32_t)data[3] << 24u);
}

static madeira_se_status_t architecture_from_machine(uint16_t machine,
                                                     madeira_se_architecture_t *out_architecture)
{
    if (out_architecture == NULL) return MADEIRA_SE_E_INVALID_ARGUMENT;

    switch (machine) {
    case 0x014cu: /* IMAGE_FILE_MACHINE_I386 */
        *out_architecture = MADEIRA_SE_ARCH_X86_32;
        return MADEIRA_SE_OK;
    case 0x8664u: /* IMAGE_FILE_MACHINE_AMD64 */
        *out_architecture = MADEIRA_SE_ARCH_X86_64;
        return MADEIRA_SE_OK;
    default:
        *out_architecture = MADEIRA_SE_ARCH_UNKNOWN;
        return MADEIRA_SE_E_UNSUPPORTED_ARCHITECTURE;
    }
}

const char *madeira_se_status_string(madeira_se_status_t status)
{
    switch (status) {
    case MADEIRA_SE_OK: return "ok";
    case MADEIRA_SE_E_INVALID_ARGUMENT: return "invalid argument";
    case MADEIRA_SE_E_INVALID_STATE: return "invalid state";
    case MADEIRA_SE_E_UNSUPPORTED: return "unsupported";
    case MADEIRA_SE_E_OUT_OF_MEMORY: return "out of memory";
    case MADEIRA_SE_E_BAD_IMAGE: return "bad PE image";
    case MADEIRA_SE_E_NOT_FOUND: return "not found";
    case MADEIRA_SE_E_BACKEND: return "backend failure";
    case MADEIRA_SE_E_UNSUPPORTED_ARCHITECTURE: return "unsupported architecture";
    case MADEIRA_SE_E_NOT_READY: return "executor not ready";
    default: return "unknown error";
    }
}

madeira_se_status_t madeira_se_parse_window_size(const char *text,
                                                 uint32_t *out_width,
                                                 uint32_t *out_height)
{
    const char *separator;
    char *end;
    unsigned long width;
    unsigned long height;

    if (text == NULL || out_width == NULL || out_height == NULL)
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    *out_width = 0u;
    *out_height = 0u;
    if (text[0] < '0' || text[0] > '9') return MADEIRA_SE_E_INVALID_ARGUMENT;

    separator = strchr(text, 'x');
    if (separator == NULL) separator = strchr(text, 'X');
    if (separator == text || separator == NULL || !separator[1]
        || strchr(separator + 1, 'x') != NULL
        || strchr(separator + 1, 'X') != NULL)
        return MADEIRA_SE_E_INVALID_ARGUMENT;

    errno = 0;
    width = strtoul(text, &end, 10);
    if (errno != 0 || end != separator || width < MADEIRA_SE_WINDOW_MIN_WIDTH
        || width > MADEIRA_SE_WINDOW_MAX_WIDTH)
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    if (separator[1] < '0' || separator[1] > '9')
        return MADEIRA_SE_E_INVALID_ARGUMENT;

    errno = 0;
    height = strtoul(separator + 1, &end, 10);
    if (errno != 0 || *end != '\0' || height < MADEIRA_SE_WINDOW_MIN_HEIGHT
        || height > MADEIRA_SE_WINDOW_MAX_HEIGHT)
        return MADEIRA_SE_E_INVALID_ARGUMENT;

    *out_width = (uint32_t)width;
    *out_height = (uint32_t)height;
    return MADEIRA_SE_OK;
}

madeira_se_status_t madeira_se_probe_pe_architecture(const uint8_t *data,
                                                     size_t size,
                                                     madeira_se_architecture_t *out_architecture)
{
    uint32_t pe_offset;

    if (data == NULL || out_architecture == NULL) return MADEIRA_SE_E_INVALID_ARGUMENT;
    *out_architecture = MADEIRA_SE_ARCH_UNKNOWN;
    if (size < 64u || data[0] != 'M' || data[1] != 'Z') return MADEIRA_SE_E_BAD_IMAGE;

    pe_offset = read_u32_le(data + 0x3cu);
    if ((uint64_t)pe_offset + 6u > (uint64_t)size) return MADEIRA_SE_E_BAD_IMAGE;
    if (data[pe_offset] != 'P' || data[pe_offset + 1u] != 'E'
        || data[pe_offset + 2u] != 0u || data[pe_offset + 3u] != 0u)
        return MADEIRA_SE_E_BAD_IMAGE;

    return architecture_from_machine(read_u16_le(data + pe_offset + 4u), out_architecture);
}

madeira_se_status_t madeira_se_probe_pe_file(const char *path_utf8,
                                             madeira_se_architecture_t *out_architecture)
{
    FILE *file;
    uint8_t dos_header[64];
    uint8_t coff_header[6];
    uint32_t pe_offset;

    if (path_utf8 == NULL || out_architecture == NULL) return MADEIRA_SE_E_INVALID_ARGUMENT;
    *out_architecture = MADEIRA_SE_ARCH_UNKNOWN;

    file = fopen(path_utf8, "rb");
    if (file == NULL) return MADEIRA_SE_E_NOT_FOUND;
    if (fread(dos_header, 1u, sizeof(dos_header), file) != sizeof(dos_header)) {
        (void)fclose(file);
        return MADEIRA_SE_E_BAD_IMAGE;
    }
    if (dos_header[0] != 'M' || dos_header[1] != 'Z') {
        (void)fclose(file);
        return MADEIRA_SE_E_BAD_IMAGE;
    }
    pe_offset = read_u32_le(dos_header + 0x3cu);
    if ((uint64_t)pe_offset > (uint64_t)LONG_MAX) {
        (void)fclose(file);
        return MADEIRA_SE_E_BAD_IMAGE;
    }
    if (fseek(file, (long)pe_offset, SEEK_SET) != 0
        || fread(coff_header, 1u, sizeof(coff_header), file) != sizeof(coff_header)) {
        (void)fclose(file);
        return MADEIRA_SE_E_BAD_IMAGE;
    }
    (void)fclose(file);

    if (coff_header[0] != 'P' || coff_header[1] != 'E'
        || coff_header[2] != 0u || coff_header[3] != 0u)
        return MADEIRA_SE_E_BAD_IMAGE;
    return architecture_from_machine(read_u16_le(coff_header + 4u), out_architecture);
}

madeira_se_status_t madeira_se_runtime_create(const madeira_se_host_t *host,
                                              const madeira_se_executor_t *executor,
                                              madeira_se_runtime_t **out_runtime)
{
    madeira_se_runtime_t *runtime;

    if (out_runtime == NULL) return MADEIRA_SE_E_INVALID_ARGUMENT;
    *out_runtime = NULL;
    if (host != NULL && !version_is_supported(host->version)) return MADEIRA_SE_E_UNSUPPORTED;
    if (executor != NULL && !version_is_supported(executor->version)) return MADEIRA_SE_E_UNSUPPORTED;

    runtime = (madeira_se_runtime_t *)calloc(1u, sizeof(*runtime));
    if (runtime == NULL) return MADEIRA_SE_E_OUT_OF_MEMORY;
    if (host != NULL) runtime->host = *host;
    if (executor != NULL) runtime->executor = *executor;
    runtime->state = MADEIRA_SE_STATE_CREATED;
    runtime->architecture = MADEIRA_SE_ARCH_UNKNOWN;
    *out_runtime = runtime;
    return MADEIRA_SE_OK;
}

void madeira_se_runtime_destroy(madeira_se_runtime_t *runtime)
{
    if (runtime == NULL) return;
    (void)madeira_se_runtime_close(runtime);
    if (runtime->executor.destroy != NULL) runtime->executor.destroy(runtime->executor.userdata);
    free(runtime->game_root);
    free(runtime->executable);
    free(runtime->working_directory);
    free(runtime);
}

madeira_se_status_t madeira_se_runtime_open(madeira_se_runtime_t *runtime,
                                            const madeira_se_launch_config_t *config)
{
    madeira_se_status_t status;
    madeira_se_architecture_t architecture;
    char *game_root;
    char *executable;
    char *working_directory;

    if (runtime == NULL || config == NULL || config->executable_utf8 == NULL)
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    if (!version_is_supported(config->version)) return fail(runtime, MADEIRA_SE_E_UNSUPPORTED, "unsupported launch config version");
    if (runtime->state != MADEIRA_SE_STATE_CREATED)
        return fail(runtime, MADEIRA_SE_E_INVALID_STATE, "runtime is already open or closed");

    game_root = duplicate_string(config->game_root_utf8 != NULL ? config->game_root_utf8 : "");
    executable = join_path(config->game_root_utf8, config->executable_utf8);
    working_directory = join_path(config->game_root_utf8,
                                  config->working_directory_utf8 != NULL
                                      ? config->working_directory_utf8
                                      : (config->game_root_utf8 != NULL ? config->game_root_utf8 : ""));
    if (game_root == NULL || executable == NULL || working_directory == NULL) {
        free(game_root);
        free(executable);
        free(working_directory);
        return fail(runtime, MADEIRA_SE_E_OUT_OF_MEMORY, "could not copy launch paths");
    }

    status = madeira_se_probe_pe_file(executable, &architecture);
    if (status != MADEIRA_SE_OK) {
        free(game_root);
        free(executable);
        free(working_directory);
        return fail(runtime, status, madeira_se_status_string(status));
    }

    runtime->game_root = game_root;
    runtime->executable = executable;
    runtime->working_directory = working_directory;
    runtime->architecture = architecture;
    runtime->state = MADEIRA_SE_STATE_OPEN;
    clear_error(runtime);

    if (runtime->host.log != NULL)
        runtime->host.log(runtime->host.userdata, MADEIRA_SE_LOG_INFO, "Madeira-SE runtime opened");

    if (runtime->executor.open != NULL) {
        if (runtime->executor.open(runtime->executor.userdata, runtime, config, architecture) != 0) {
            runtime->state = MADEIRA_SE_STATE_CREATED;
            runtime->architecture = MADEIRA_SE_ARCH_UNKNOWN;
            free(runtime->game_root);
            free(runtime->executable);
            free(runtime->working_directory);
            runtime->game_root = NULL;
            runtime->executable = NULL;
            runtime->working_directory = NULL;
            return fail(runtime, MADEIRA_SE_E_BACKEND, "executor failed to open the image");
        }
    }
    return MADEIRA_SE_OK;
}

madeira_se_status_t madeira_se_runtime_tick(madeira_se_runtime_t *runtime,
                                            uint64_t budget_ns)
{
    if (runtime == NULL) return MADEIRA_SE_E_INVALID_ARGUMENT;
    if (runtime->state == MADEIRA_SE_STATE_PAUSED) return MADEIRA_SE_OK;
    if (runtime->state != MADEIRA_SE_STATE_OPEN)
        return fail(runtime, MADEIRA_SE_E_INVALID_STATE, "runtime is not open");
    if (runtime->executor.tick == NULL)
        return fail(runtime, MADEIRA_SE_E_NOT_READY, "no CPU executor is attached");
    return runtime->executor.tick(runtime->executor.userdata, budget_ns) == 0
        ? MADEIRA_SE_OK
        : fail(runtime, MADEIRA_SE_E_BACKEND, "executor tick failed");
}

madeira_se_status_t madeira_se_runtime_pause(madeira_se_runtime_t *runtime)
{
    if (runtime == NULL) return MADEIRA_SE_E_INVALID_ARGUMENT;
    if (runtime->state != MADEIRA_SE_STATE_OPEN)
        return fail(runtime, MADEIRA_SE_E_INVALID_STATE, "runtime is not open");
    if (runtime->executor.pause != NULL && runtime->executor.pause(runtime->executor.userdata) != 0)
        return fail(runtime, MADEIRA_SE_E_BACKEND, "executor pause failed");
    runtime->state = MADEIRA_SE_STATE_PAUSED;
    return MADEIRA_SE_OK;
}

madeira_se_status_t madeira_se_runtime_resume(madeira_se_runtime_t *runtime)
{
    if (runtime == NULL) return MADEIRA_SE_E_INVALID_ARGUMENT;
    if (runtime->state != MADEIRA_SE_STATE_PAUSED)
        return fail(runtime, MADEIRA_SE_E_INVALID_STATE, "runtime is not paused");
    if (runtime->executor.resume != NULL && runtime->executor.resume(runtime->executor.userdata) != 0)
        return fail(runtime, MADEIRA_SE_E_BACKEND, "executor resume failed");
    runtime->state = MADEIRA_SE_STATE_OPEN;
    return MADEIRA_SE_OK;
}

madeira_se_status_t madeira_se_runtime_close(madeira_se_runtime_t *runtime)
{
    if (runtime == NULL) return MADEIRA_SE_E_INVALID_ARGUMENT;
    if (runtime->state == MADEIRA_SE_STATE_CLOSED) return MADEIRA_SE_OK;
    if (runtime->state == MADEIRA_SE_STATE_CREATED) {
        runtime->state = MADEIRA_SE_STATE_CLOSED;
        return MADEIRA_SE_OK;
    }
    if (runtime->executor.close != NULL && runtime->executor.close(runtime->executor.userdata) != 0)
        return fail(runtime, MADEIRA_SE_E_BACKEND, "executor close failed");
    if (runtime->audio_opened && runtime->host.audio_close != NULL)
        runtime->host.audio_close(runtime->host.userdata);
    runtime->audio_opened = 0;
    runtime->state = MADEIRA_SE_STATE_CLOSED;
    return MADEIRA_SE_OK;
}

madeira_se_status_t madeira_se_runtime_send_input(madeira_se_runtime_t *runtime,
                                                  const madeira_se_input_event_t *event)
{
    if (runtime == NULL || event == NULL) return MADEIRA_SE_E_INVALID_ARGUMENT;
    if (!version_is_supported(event->version)) return MADEIRA_SE_E_UNSUPPORTED;
    if (runtime->state != MADEIRA_SE_STATE_OPEN && runtime->state != MADEIRA_SE_STATE_PAUSED)
        return fail(runtime, MADEIRA_SE_E_INVALID_STATE, "runtime is not open");
    if (runtime->executor.send_input == NULL)
        return fail(runtime, MADEIRA_SE_E_NOT_READY, "no input executor is attached");
    return runtime->executor.send_input(runtime->executor.userdata, event) == 0
        ? MADEIRA_SE_OK
        : fail(runtime, MADEIRA_SE_E_BACKEND, "executor rejected input");
}

madeira_se_status_t madeira_se_runtime_submit_frame(madeira_se_runtime_t *runtime,
                                                    const madeira_se_frame_t *frame)
{
    uint64_t required_size;

    if (runtime == NULL || frame == NULL || frame->rgba8 == NULL)
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    if (!version_is_supported(frame->version)) return MADEIRA_SE_E_UNSUPPORTED;
    if (runtime->state != MADEIRA_SE_STATE_OPEN)
        return fail(runtime, MADEIRA_SE_E_INVALID_STATE, "runtime is not open");
    if (frame->width == 0u || frame->height == 0u
        || (size_t)frame->width > SIZE_MAX / 4u
        || frame->stride_bytes < (size_t)frame->width * 4u)
        return fail(runtime, MADEIRA_SE_E_INVALID_ARGUMENT, "invalid frame dimensions");
    required_size = (uint64_t)frame->stride_bytes * (uint64_t)frame->height;
    if (required_size > (uint64_t)frame->data_size)
        return fail(runtime, MADEIRA_SE_E_INVALID_ARGUMENT, "frame buffer is too small");
    if (runtime->host.present_frame == NULL)
        return fail(runtime, MADEIRA_SE_E_NOT_READY, "no graphics sink is attached");
    return runtime->host.present_frame(runtime->host.userdata, frame) == 0
        ? MADEIRA_SE_OK
        : fail(runtime, MADEIRA_SE_E_BACKEND, "graphics sink rejected frame");
}

madeira_se_status_t madeira_se_runtime_submit_audio(madeira_se_runtime_t *runtime,
                                                   const madeira_se_audio_format_t *format,
                                                   const void *samples,
                                                   size_t frame_count)
{
    if (runtime == NULL || format == NULL || samples == NULL || frame_count == 0u)
        return MADEIRA_SE_E_INVALID_ARGUMENT;
    if (!version_is_supported(format->version)) return MADEIRA_SE_E_UNSUPPORTED;
    if (runtime->state != MADEIRA_SE_STATE_OPEN)
        return fail(runtime, MADEIRA_SE_E_INVALID_STATE, "runtime is not open");
    if (format->sample_rate == 0u || format->channels == 0u
        || (format->sample_format != MADEIRA_SE_AUDIO_F32_INTERLEAVED
            && format->sample_format != MADEIRA_SE_AUDIO_S16_INTERLEAVED))
        return fail(runtime, MADEIRA_SE_E_INVALID_ARGUMENT, "invalid audio format");
    if (runtime->host.audio_write == NULL)
        return fail(runtime, MADEIRA_SE_E_NOT_READY, "no audio sink is attached");

    if (!runtime->audio_opened) {
        if (runtime->host.audio_open != NULL
            && runtime->host.audio_open(runtime->host.userdata, format) != 0)
            return fail(runtime, MADEIRA_SE_E_BACKEND, "audio sink failed to open");
        runtime->audio_format = *format;
        runtime->audio_opened = 1;
    } else if (memcmp(&runtime->audio_format, format, sizeof(*format)) != 0) {
        return fail(runtime, MADEIRA_SE_E_INVALID_ARGUMENT, "audio format changed while streaming");
    }

    return runtime->host.audio_write(runtime->host.userdata, format, samples, frame_count) == 0
        ? MADEIRA_SE_OK
        : fail(runtime, MADEIRA_SE_E_BACKEND, "audio sink rejected samples");
}

madeira_se_state_t madeira_se_runtime_state(const madeira_se_runtime_t *runtime)
{
    return runtime != NULL ? runtime->state : MADEIRA_SE_STATE_CLOSED;
}

madeira_se_architecture_t madeira_se_runtime_architecture(const madeira_se_runtime_t *runtime)
{
    return runtime != NULL ? runtime->architecture : MADEIRA_SE_ARCH_UNKNOWN;
}

const char *madeira_se_runtime_game_root(const madeira_se_runtime_t *runtime)
{
    return runtime != NULL ? runtime->game_root : NULL;
}

const char *madeira_se_runtime_executable(const madeira_se_runtime_t *runtime)
{
    return runtime != NULL ? runtime->executable : NULL;
}

const char *madeira_se_runtime_working_directory(const madeira_se_runtime_t *runtime)
{
    return runtime != NULL ? runtime->working_directory : NULL;
}

const char *madeira_se_runtime_last_error(const madeira_se_runtime_t *runtime)
{
    return runtime != NULL ? runtime->last_error : "invalid runtime";
}
