/*
 * Madeira-SE core unit tests.
 *
 * Copyright (C) 2026 The Madeira contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "madeira_se.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

struct fake_host {
    unsigned log_count;
    unsigned frame_count;
    unsigned audio_open_count;
    unsigned audio_write_count;
    unsigned audio_close_count;
    size_t audio_frames;
};

struct fake_executor {
    unsigned open_count;
    unsigned tick_count;
    unsigned input_count;
    unsigned pause_count;
    unsigned resume_count;
    unsigned close_count;
};

static void fake_log(void *userdata, madeira_se_log_level_t level, const char *message)
{
    struct fake_host *host = (struct fake_host *)userdata;
    (void)level;
    (void)message;
    host->log_count++;
}

static int fake_present(void *userdata, const madeira_se_frame_t *frame)
{
    struct fake_host *host = (struct fake_host *)userdata;
    CHECK(frame->width == 2u);
    CHECK(frame->height == 2u);
    host->frame_count++;
    return 0;
}

static int fake_audio_open(void *userdata, const madeira_se_audio_format_t *format)
{
    struct fake_host *host = (struct fake_host *)userdata;
    CHECK(format->sample_rate == 48000u);
    CHECK(format->channels == 2u);
    host->audio_open_count++;
    return 0;
}

static int fake_audio_write(void *userdata,
                            const madeira_se_audio_format_t *format,
                            const void *samples,
                            size_t frame_count)
{
    struct fake_host *host = (struct fake_host *)userdata;
    CHECK(format->sample_format == MADEIRA_SE_AUDIO_F32_INTERLEAVED);
    CHECK(samples != NULL);
    host->audio_write_count++;
    host->audio_frames += frame_count;
    return 0;
}

static void fake_audio_close(void *userdata)
{
    struct fake_host *host = (struct fake_host *)userdata;
    host->audio_close_count++;
}

static int fake_executor_open(void *userdata,
                              madeira_se_runtime_t *runtime,
                              const madeira_se_launch_config_t *config,
                              madeira_se_architecture_t architecture)
{
    struct fake_executor *executor = (struct fake_executor *)userdata;
    CHECK(runtime != NULL);
    CHECK(config != NULL);
    CHECK(architecture == MADEIRA_SE_ARCH_X86_32);
    executor->open_count++;
    return 0;
}

static int fake_executor_tick(void *userdata, uint64_t budget_ns)
{
    struct fake_executor *executor = (struct fake_executor *)userdata;
    CHECK(budget_ns == 1000000u);
    executor->tick_count++;
    return 0;
}

static int fake_executor_input(void *userdata, const madeira_se_input_event_t *event)
{
    struct fake_executor *executor = (struct fake_executor *)userdata;
    CHECK(event->type == MADEIRA_SE_INPUT_KEY);
    executor->input_count++;
    return 0;
}

static int fake_executor_pause(void *userdata)
{
    struct fake_executor *executor = (struct fake_executor *)userdata;
    executor->pause_count++;
    return 0;
}

static int fake_executor_resume(void *userdata)
{
    struct fake_executor *executor = (struct fake_executor *)userdata;
    executor->resume_count++;
    return 0;
}

static int fake_executor_close(void *userdata)
{
    struct fake_executor *executor = (struct fake_executor *)userdata;
    executor->close_count++;
    return 0;
}

static void write_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value & 0xffu);
    data[1] = (uint8_t)(value >> 8u);
}

static void write_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xffu);
    data[1] = (uint8_t)((value >> 8u) & 0xffu);
    data[2] = (uint8_t)((value >> 16u) & 0xffu);
    data[3] = (uint8_t)(value >> 24u);
}

static void make_pe_fixture(uint8_t *data, size_t size, uint16_t machine)
{
    memset(data, 0, size);
    data[0] = 'M';
    data[1] = 'Z';
    write_u32_le(data + 0x3c, 0x40u);
    data[0x40] = 'P';
    data[0x41] = 'E';
    write_u16_le(data + 0x44, machine);
}

static int test_probe(void)
{
    uint8_t image[128];
    madeira_se_architecture_t architecture;

    make_pe_fixture(image, sizeof(image), 0x014cu);
    CHECK(madeira_se_probe_pe_architecture(image, sizeof(image), &architecture) == MADEIRA_SE_OK);
    CHECK(architecture == MADEIRA_SE_ARCH_X86_32);

    make_pe_fixture(image, sizeof(image), 0x8664u);
    CHECK(madeira_se_probe_pe_architecture(image, sizeof(image), &architecture) == MADEIRA_SE_OK);
    CHECK(architecture == MADEIRA_SE_ARCH_X86_64);

    make_pe_fixture(image, sizeof(image), 0xaa64u);
    CHECK(madeira_se_probe_pe_architecture(image, sizeof(image), &architecture)
          == MADEIRA_SE_E_UNSUPPORTED_ARCHITECTURE);
    CHECK(architecture == MADEIRA_SE_ARCH_UNKNOWN);

    image[0] = 'X';
    CHECK(madeira_se_probe_pe_architecture(image, sizeof(image), &architecture) == MADEIRA_SE_E_BAD_IMAGE);

    make_pe_fixture(image, sizeof(image), 0x014cu);
    write_u32_le(image + 0x3c, 0x1000u);
    CHECK(madeira_se_probe_pe_architecture(image, sizeof(image), &architecture) == MADEIRA_SE_E_BAD_IMAGE);
    return 0;
}

static int create_fixture_file(char *path, size_t path_size)
{
    int fd;
    uint8_t image[128];

    (void)snprintf(path, path_size, "/tmp/madeira-se-test-XXXXXX");
    fd = mkstemp(path);
    if (fd < 0) return 1;
    make_pe_fixture(image, sizeof(image), 0x014cu);
    if (write(fd, image, sizeof(image)) != (ssize_t)sizeof(image)) {
        (void)close(fd);
        (void)unlink(path);
        return 1;
    }
    (void)close(fd);
    return 0;
}

static int test_runtime_lifecycle(void)
{
    struct fake_host host_state = {0};
    struct fake_executor executor_state = {0};
    madeira_se_host_t host = {
        .version = MADEIRA_SE_ABI_VERSION,
        .userdata = &host_state,
        .log = fake_log,
        .present_frame = fake_present,
        .audio_open = fake_audio_open,
        .audio_write = fake_audio_write,
        .audio_close = fake_audio_close,
    };
    madeira_se_executor_t executor = {
        .version = MADEIRA_SE_ABI_VERSION,
        .userdata = &executor_state,
        .open = fake_executor_open,
        .tick = fake_executor_tick,
        .send_input = fake_executor_input,
        .pause = fake_executor_pause,
        .resume = fake_executor_resume,
        .close = fake_executor_close,
    };
    madeira_se_runtime_t *runtime = NULL;
    madeira_se_launch_config_t config;
    madeira_se_frame_t frame;
    madeira_se_audio_format_t audio_format;
    madeira_se_input_event_t input;
    uint8_t pixels[32] = {0};
    float samples[8] = {0};
    char path[64];

    CHECK(create_fixture_file(path, sizeof(path)) == 0);
    config.version = MADEIRA_SE_ABI_VERSION;
    config.game_root_utf8 = "/tmp";
    config.executable_utf8 = path + 5; /* skip "/tmp/" */
    config.working_directory_utf8 = NULL;
    config.flags = 0u;

    CHECK(madeira_se_runtime_create(&host, &executor, &runtime) == MADEIRA_SE_OK);
    CHECK(madeira_se_runtime_state(runtime) == MADEIRA_SE_STATE_CREATED);
    CHECK(madeira_se_runtime_open(runtime, &config) == MADEIRA_SE_OK);
    CHECK(madeira_se_runtime_state(runtime) == MADEIRA_SE_STATE_OPEN);
    CHECK(madeira_se_runtime_architecture(runtime) == MADEIRA_SE_ARCH_X86_32);
    CHECK(strcmp(madeira_se_runtime_game_root(runtime), "/tmp") == 0);
    CHECK(strcmp(madeira_se_runtime_executable(runtime), path) == 0);
    CHECK(strcmp(madeira_se_runtime_working_directory(runtime), "/tmp") == 0);
    CHECK(executor_state.open_count == 1u);

    CHECK(madeira_se_runtime_tick(runtime, 1000000u) == MADEIRA_SE_OK);
    CHECK(executor_state.tick_count == 1u);

    frame.version = MADEIRA_SE_ABI_VERSION;
    frame.width = 2u;
    frame.height = 2u;
    frame.stride_bytes = 8u;
    frame.rgba8 = pixels;
    frame.data_size = sizeof(pixels);
    frame.frame_index = 1u;
    frame.timestamp_ns = 2u;
    CHECK(madeira_se_runtime_submit_frame(runtime, &frame) == MADEIRA_SE_OK);
    CHECK(host_state.frame_count == 1u);

    audio_format.version = MADEIRA_SE_ABI_VERSION;
    audio_format.sample_rate = 48000u;
    audio_format.channels = 2u;
    audio_format.sample_format = MADEIRA_SE_AUDIO_F32_INTERLEAVED;
    CHECK(madeira_se_runtime_submit_audio(runtime, &audio_format, samples, 4u) == MADEIRA_SE_OK);
    CHECK(host_state.audio_open_count == 1u);
    CHECK(host_state.audio_write_count == 1u);
    CHECK(host_state.audio_frames == 4u);

    input.version = MADEIRA_SE_ABI_VERSION;
    input.type = MADEIRA_SE_INPUT_KEY;
    input.code = 30u;
    input.value0 = 1;
    input.value1 = 0;
    input.timestamp_ns = 3u;
    CHECK(madeira_se_runtime_send_input(runtime, &input) == MADEIRA_SE_OK);
    CHECK(executor_state.input_count == 1u);

    CHECK(madeira_se_runtime_pause(runtime) == MADEIRA_SE_OK);
    CHECK(madeira_se_runtime_state(runtime) == MADEIRA_SE_STATE_PAUSED);
    CHECK(madeira_se_runtime_tick(runtime, 1000000u) == MADEIRA_SE_OK);
    CHECK(executor_state.tick_count == 1u);
    CHECK(madeira_se_runtime_resume(runtime) == MADEIRA_SE_OK);
    CHECK(madeira_se_runtime_state(runtime) == MADEIRA_SE_STATE_OPEN);
    CHECK(executor_state.pause_count == 1u);
    CHECK(executor_state.resume_count == 1u);

    CHECK(madeira_se_runtime_close(runtime) == MADEIRA_SE_OK);
    CHECK(madeira_se_runtime_state(runtime) == MADEIRA_SE_STATE_CLOSED);
    CHECK(executor_state.close_count == 1u);
    CHECK(host_state.audio_close_count == 1u);
    CHECK(host_state.log_count >= 1u);
    madeira_se_runtime_destroy(runtime);
    (void)unlink(path);
    return 0;
}

static int test_validation(void)
{
    madeira_se_runtime_t *runtime = NULL;
    madeira_se_host_t host = {.version = MADEIRA_SE_ABI_VERSION};
    madeira_se_launch_config_t config;
    madeira_se_frame_t frame = {0};
    madeira_se_audio_format_t format = {0};
    char path[64];

    CHECK(create_fixture_file(path, sizeof(path)) == 0);
    config.version = MADEIRA_SE_ABI_VERSION;
    config.game_root_utf8 = "/tmp";
    config.executable_utf8 = path + 5;
    config.working_directory_utf8 = NULL;
    config.flags = 0u;
    CHECK(madeira_se_runtime_create(&host, NULL, &runtime) == MADEIRA_SE_OK);
    CHECK(madeira_se_runtime_open(runtime, &config) == MADEIRA_SE_OK);
    CHECK(madeira_se_runtime_submit_frame(runtime, &frame) == MADEIRA_SE_E_INVALID_ARGUMENT);
    {
        uint8_t pixel = 0;
        frame.version = MADEIRA_SE_ABI_VERSION;
        frame.width = UINT32_MAX;
        frame.height = 1u;
        frame.stride_bytes = SIZE_MAX;
        frame.rgba8 = &pixel;
        frame.data_size = sizeof(pixel);
        CHECK(madeira_se_runtime_submit_frame(runtime, &frame) == MADEIRA_SE_E_INVALID_ARGUMENT);
    }
    CHECK(madeira_se_runtime_submit_audio(runtime, &format, "x", 1u)
          == MADEIRA_SE_E_INVALID_ARGUMENT);
    CHECK(madeira_se_runtime_tick(runtime, 0u) == MADEIRA_SE_E_NOT_READY);
    madeira_se_runtime_destroy(runtime);
    (void)unlink(path);
    return 0;
}

static int test_window_size_parser(void)
{
    uint32_t width = 0u;
    uint32_t height = 0u;

    CHECK(madeira_se_parse_window_size("1280x720", &width, &height) == MADEIRA_SE_OK);
    CHECK(width == 1280u && height == 720u);
    CHECK(madeira_se_parse_window_size("320X200", &width, &height) == MADEIRA_SE_OK);
    CHECK(width == 320u && height == 200u);
    CHECK(madeira_se_parse_window_size("319x720", &width, &height)
          == MADEIRA_SE_E_INVALID_ARGUMENT);
    CHECK(madeira_se_parse_window_size("1280x199", &width, &height)
          == MADEIRA_SE_E_INVALID_ARGUMENT);
    CHECK(madeira_se_parse_window_size("7681x720", &width, &height)
          == MADEIRA_SE_E_INVALID_ARGUMENT);
    CHECK(madeira_se_parse_window_size("1280x4321", &width, &height)
          == MADEIRA_SE_E_INVALID_ARGUMENT);
    CHECK(madeira_se_parse_window_size("1280", &width, &height)
          == MADEIRA_SE_E_INVALID_ARGUMENT);
    CHECK(madeira_se_parse_window_size("1280x720x1", &width, &height)
          == MADEIRA_SE_E_INVALID_ARGUMENT);
    CHECK(madeira_se_parse_window_size(NULL, &width, &height)
          == MADEIRA_SE_E_INVALID_ARGUMENT);
    CHECK(madeira_se_parse_window_size("1280x720", NULL, &height)
          == MADEIRA_SE_E_INVALID_ARGUMENT);
    return 0;
}

int main(void)
{
    CHECK(test_probe() == 0);
    CHECK(test_runtime_lifecycle() == 0);
    CHECK(test_validation() == 0);
    CHECK(test_window_size_parser() == 0);
    puts("Madeira-SE core tests passed");
    return 0;
}
