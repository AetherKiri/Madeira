/*
 * Madeira-SE standalone runtime boundary.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MADEIRA_SE_H
#define MADEIRA_SE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MADEIRA_SE_ABI_VERSION 1u

typedef struct madeira_se_runtime madeira_se_runtime_t;

typedef enum madeira_se_status {
    MADEIRA_SE_OK = 0,
    MADEIRA_SE_E_INVALID_ARGUMENT = -1,
    MADEIRA_SE_E_INVALID_STATE = -2,
    MADEIRA_SE_E_UNSUPPORTED = -3,
    MADEIRA_SE_E_OUT_OF_MEMORY = -4,
    MADEIRA_SE_E_BAD_IMAGE = -5,
    MADEIRA_SE_E_NOT_FOUND = -6,
    MADEIRA_SE_E_BACKEND = -7,
    MADEIRA_SE_E_UNSUPPORTED_ARCHITECTURE = -8,
    MADEIRA_SE_E_NOT_READY = -9,
} madeira_se_status_t;

typedef enum madeira_se_architecture {
    MADEIRA_SE_ARCH_UNKNOWN = 0,
    MADEIRA_SE_ARCH_X86_32 = 1,
    MADEIRA_SE_ARCH_X86_64 = 2,
} madeira_se_architecture_t;

/* Window sizes accepted by the standalone launcher and Cocoa driver. */
#define MADEIRA_SE_WINDOW_MIN_WIDTH 320u
#define MADEIRA_SE_WINDOW_MAX_WIDTH 7680u
#define MADEIRA_SE_WINDOW_MIN_HEIGHT 200u
#define MADEIRA_SE_WINDOW_MAX_HEIGHT 4320u

typedef enum madeira_se_state {
    MADEIRA_SE_STATE_CREATED = 0,
    MADEIRA_SE_STATE_OPEN = 1,
    MADEIRA_SE_STATE_PAUSED = 2,
    MADEIRA_SE_STATE_CLOSED = 3,
} madeira_se_state_t;

typedef enum madeira_se_log_level {
    MADEIRA_SE_LOG_DEBUG = 0,
    MADEIRA_SE_LOG_INFO = 1,
    MADEIRA_SE_LOG_WARNING = 2,
    MADEIRA_SE_LOG_ERROR = 3,
} madeira_se_log_level_t;

typedef enum madeira_se_audio_sample_format {
    MADEIRA_SE_AUDIO_F32_INTERLEAVED = 1,
    MADEIRA_SE_AUDIO_S16_INTERLEAVED = 2,
} madeira_se_audio_sample_format_t;

typedef enum madeira_se_input_type {
    MADEIRA_SE_INPUT_KEY = 1,
    MADEIRA_SE_INPUT_MOUSE_BUTTON = 2,
    MADEIRA_SE_INPUT_MOUSE_MOTION = 3,
    MADEIRA_SE_INPUT_GAMEPAD = 4,
    MADEIRA_SE_INPUT_TOUCH = 5,
    MADEIRA_SE_INPUT_TEXT = 6,
} madeira_se_input_type_t;

typedef struct madeira_se_launch_config {
    uint32_t version;
    const char *game_root_utf8;
    const char *executable_utf8;
    const char *working_directory_utf8;
    uint32_t flags;
} madeira_se_launch_config_t;

typedef struct madeira_se_frame {
    uint32_t version;
    uint32_t width;
    uint32_t height;
    size_t stride_bytes;
    const uint8_t *rgba8;
    size_t data_size;
    uint64_t frame_index;
    uint64_t timestamp_ns;
} madeira_se_frame_t;

typedef struct madeira_se_audio_format {
    uint32_t version;
    uint32_t sample_rate;
    uint32_t channels;
    madeira_se_audio_sample_format_t sample_format;
} madeira_se_audio_format_t;

typedef struct madeira_se_input_event {
    uint32_t version;
    madeira_se_input_type_t type;
    uint32_t code;
    int32_t value0;
    int32_t value1;
    uint64_t timestamp_ns;
} madeira_se_input_event_t;

typedef void (*madeira_se_log_fn)(void *userdata,
                                  madeira_se_log_level_t level,
                                  const char *message_utf8);
typedef uint64_t (*madeira_se_clock_fn)(void *userdata);

typedef int (*madeira_se_present_frame_fn)(void *userdata,
                                           const madeira_se_frame_t *frame);
typedef int (*madeira_se_audio_open_fn)(void *userdata,
                                        const madeira_se_audio_format_t *format);
typedef int (*madeira_se_audio_write_fn)(void *userdata,
                                         const madeira_se_audio_format_t *format,
                                         const void *samples,
                                         size_t frame_count);
typedef void (*madeira_se_audio_close_fn)(void *userdata);

/*
 * These callbacks are host-device hooks only. They do not implement Windows
 * semantics; Madeira-SE owns the Windows-facing graphics and audio layers.
 */
typedef struct madeira_se_host {
    uint32_t version;
    void *userdata;
    madeira_se_clock_fn monotonic_ns;
    madeira_se_log_fn log;
    madeira_se_present_frame_fn present_frame;
    madeira_se_audio_open_fn audio_open;
    madeira_se_audio_write_fn audio_write;
    madeira_se_audio_close_fn audio_close;
    void *reserved[4];
} madeira_se_host_t;

typedef int (*madeira_se_executor_open_fn)(void *userdata,
                                           madeira_se_runtime_t *runtime,
                                           const madeira_se_launch_config_t *config,
                                           madeira_se_architecture_t architecture);
typedef int (*madeira_se_executor_tick_fn)(void *userdata, uint64_t budget_ns);
typedef int (*madeira_se_executor_input_fn)(void *userdata,
                                            const madeira_se_input_event_t *event);
typedef int (*madeira_se_executor_pause_fn)(void *userdata);
typedef int (*madeira_se_executor_resume_fn)(void *userdata);
typedef int (*madeira_se_executor_close_fn)(void *userdata);
typedef void (*madeira_se_executor_destroy_fn)(void *userdata);

/*
 * The executor is where the future Wine + QEMU TCTI adapter plugs in. Keeping
 * it below the public runtime boundary makes the core testable without
 * AetherKiri and without pretending that CPU execution is implemented yet.
 */
typedef struct madeira_se_executor {
    uint32_t version;
    void *userdata;
    madeira_se_executor_open_fn open;
    madeira_se_executor_tick_fn tick;
    madeira_se_executor_input_fn send_input;
    madeira_se_executor_pause_fn pause;
    madeira_se_executor_resume_fn resume;
    madeira_se_executor_close_fn close;
    madeira_se_executor_destroy_fn destroy;
    void *reserved[4];
} madeira_se_executor_t;

const char *madeira_se_status_string(madeira_se_status_t status);

/* Parse an ASCII WxH client-area size used by Madeira-SE standalone mode. */
madeira_se_status_t madeira_se_parse_window_size(const char *text,
                                                 uint32_t *out_width,
                                                 uint32_t *out_height);

madeira_se_status_t madeira_se_probe_pe_architecture(const uint8_t *data,
                                                     size_t size,
                                                     madeira_se_architecture_t *out_architecture);
madeira_se_status_t madeira_se_probe_pe_file(const char *path_utf8,
                                             madeira_se_architecture_t *out_architecture);

madeira_se_status_t madeira_se_runtime_create(const madeira_se_host_t *host,
                                              const madeira_se_executor_t *executor,
                                              madeira_se_runtime_t **out_runtime);
void madeira_se_runtime_destroy(madeira_se_runtime_t *runtime);

madeira_se_status_t madeira_se_runtime_open(madeira_se_runtime_t *runtime,
                                            const madeira_se_launch_config_t *config);
madeira_se_status_t madeira_se_runtime_tick(madeira_se_runtime_t *runtime,
                                            uint64_t budget_ns);
madeira_se_status_t madeira_se_runtime_pause(madeira_se_runtime_t *runtime);
madeira_se_status_t madeira_se_runtime_resume(madeira_se_runtime_t *runtime);
madeira_se_status_t madeira_se_runtime_close(madeira_se_runtime_t *runtime);

madeira_se_status_t madeira_se_runtime_send_input(madeira_se_runtime_t *runtime,
                                                  const madeira_se_input_event_t *event);
madeira_se_status_t madeira_se_runtime_submit_frame(madeira_se_runtime_t *runtime,
                                                    const madeira_se_frame_t *frame);
madeira_se_status_t madeira_se_runtime_submit_audio(madeira_se_runtime_t *runtime,
                                                   const madeira_se_audio_format_t *format,
                                                   const void *samples,
                                                   size_t frame_count);

madeira_se_state_t madeira_se_runtime_state(const madeira_se_runtime_t *runtime);
madeira_se_architecture_t madeira_se_runtime_architecture(const madeira_se_runtime_t *runtime);
const char *madeira_se_runtime_game_root(const madeira_se_runtime_t *runtime);
const char *madeira_se_runtime_executable(const madeira_se_runtime_t *runtime);
const char *madeira_se_runtime_working_directory(const madeira_se_runtime_t *runtime);
const char *madeira_se_runtime_last_error(const madeira_se_runtime_t *runtime);

#ifdef __cplusplus
}
#endif

#endif /* MADEIRA_SE_H */
