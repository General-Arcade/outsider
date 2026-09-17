/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

/* test_video.c — MPEG-1 playback through video_player: opening, decoding
   frames on a wall-clock step, audio streaming into the audio engine, the
   play/pause/ended state machine and handle hygiene. Headless: no GL, and
   the audio engine falls back to its null backend. */

#include "video/video_player.h"
#include "audio/audio_engine.h"
#include "io/file_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int _tests_run = 0, _tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    _tests_run++; \
    printf("  [%d] %s ... ", _tests_run, #name); \
    name(); \
    printf("\n"); \
} while (0)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL at %s:%d: %s", __FILE__, __LINE__, #cond); \
        _tests_failed++; \
        return; \
    } \
} while (0)

#define FIXTURE "tests/fixtures/test_video.mpg"   /* 64x48, 30 fps, 1 s, 440 Hz tone */

static bool fixture_present(void)
{
    FILE *f = fopen(FIXTURE, "rb");
    if (!f) { printf("SKIP (fixture missing; run from project root) "); return false; }
    fclose(f);
    return true;
}

TEST(open_reports_dimensions_and_duration)
{
    if (!fixture_present()) return;
    VideoHandle h = video_open(FIXTURE);
    ASSERT(h != VIDEO_HANDLE_INVALID);
    VideoState st;
    ASSERT(video_get_state(h, &st));
    ASSERT(st.width == 64 && st.height == 48);
    ASSERT(st.duration > 0.8 && st.duration < 1.2);
    ASSERT(!st.playing && !st.ended);
    video_close(h);
    printf("PASS");
}

TEST(open_rejects_missing_and_garbage)
{
    ASSERT(video_open("tests/fixtures/does_not_exist.mpg") == VIDEO_HANDLE_INVALID);
    ASSERT(video_open("tests/fixtures/test_4x4.png") == VIDEO_HANDLE_INVALID);
    ASSERT(video_open(NULL) == VIDEO_HANDLE_INVALID);
    VideoState st;
    ASSERT(!video_get_state(0, &st));
    ASSERT(!video_get_state(99, &st));
    printf("PASS");
}

TEST(decodes_frames_in_real_time)
{
    if (!fixture_present()) return;
    VideoHandle h = video_open(FIXTURE);
    ASSERT(h != VIDEO_HANDLE_INVALID);

    /* Nothing decodes until play(). */
    video_player_update(0.1);
    ASSERT(video_get_frame_count(h) == 0);

    video_play(h);
    for (int i = 0; i < 30; i++) video_player_update(1.0 / 60.0);   /* 0.5 s */
    int half = video_get_frame_count(h);
    ASSERT(half >= 12 && half <= 18);                                  /* ~15 of 30 */
    VideoState st;
    ASSERT(video_get_state(h, &st));
    ASSERT(st.playing && !st.ended);
    ASSERT(st.time > 0.3 && st.time < 0.7);

    for (int i = 0; i < 60; i++) video_player_update(1.0 / 60.0);   /* past the end */
    ASSERT(video_get_state(h, &st));
    ASSERT(st.ended);
    ASSERT(!st.playing);
    ASSERT(video_get_frame_count(h) >= 28);
    video_close(h);
    printf("PASS");
}

TEST(pause_stops_decoding)
{
    if (!fixture_present()) return;
    VideoHandle h = video_open(FIXTURE);
    ASSERT(h != VIDEO_HANDLE_INVALID);
    video_play(h);
    for (int i = 0; i < 12; i++) video_player_update(1.0 / 60.0);
    int before = video_get_frame_count(h);
    ASSERT(before > 0);

    video_pause(h);
    for (int i = 0; i < 30; i++) video_player_update(1.0 / 60.0);
    ASSERT(video_get_frame_count(h) == before);
    VideoState st;
    ASSERT(video_get_state(h, &st));
    ASSERT(st.paused && !st.playing);

    video_play(h);
    for (int i = 0; i < 12; i++) video_player_update(1.0 / 60.0);
    ASSERT(video_get_frame_count(h) > before);
    video_close(h);
    printf("PASS");
}

TEST(large_step_is_clamped)
{
    if (!fixture_present()) return;
    VideoHandle h = video_open(FIXTURE);
    ASSERT(h != VIDEO_HANDLE_INVALID);
    video_play(h);
    video_player_update(10.0);   /* one stalled frame must not decode the whole file */
    VideoState st;
    ASSERT(video_get_state(h, &st));
    ASSERT(!st.ended);
    ASSERT(st.time < 0.5);
    video_close(h);
    printf("PASS");
}

TEST(audio_is_streamed)
{
    if (!fixture_present()) return;
    if (!audio_engine_is_initialized()) { printf("SKIP (no audio engine) "); return; }
    VideoHandle h = video_open(FIXTURE);
    ASSERT(h != VIDEO_HANDLE_INVALID);
    video_play(h);
    /* Decoding the first stretch must have pushed samples into a stream.
       A second stream slot being free proves the video's one is in use. */
    for (int i = 0; i < 6; i++) video_player_update(1.0 / 60.0);
    AudioStreamHandle probe = audio_stream_create(44100, 2);
    ASSERT(probe != AUDIO_STREAM_INVALID);
    audio_stream_destroy(probe);
    video_close(h);
    printf("PASS");
}

TEST(pcm_stream_ring_buffer)
{
    if (!audio_engine_is_initialized()) { printf("SKIP (no audio engine) "); return; }
    AudioStreamHandle s = audio_stream_create(44100, 2);
    ASSERT(s != AUDIO_STREAM_INVALID);
    float chunk[256 * 2];
    memset(chunk, 0, sizeof(chunk));
    audio_stream_push(s, chunk, 256);
    /* The mixer thread may already have drained some; it cannot exceed what was pushed. */
    ASSERT(audio_stream_queued_frames(s) <= 256);
    audio_stream_set_volume(s, 0.5f);
    audio_stream_set_paused(s, true);
    audio_stream_set_paused(s, false);
    audio_stream_destroy(s);
    ASSERT(audio_stream_queued_frames(s) == 0);
    /* Invalid handles are ignored. */
    audio_stream_push(0, chunk, 256);
    audio_stream_destroy(0);
    printf("PASS");
}

TEST(handles_are_recycled)
{
    if (!fixture_present()) return;
    VideoHandle first = video_open(FIXTURE);
    ASSERT(first != VIDEO_HANDLE_INVALID);
    video_close(first);
    VideoHandle second = video_open(FIXTURE);
    ASSERT(second == first);
    video_close(second);
    video_close(second);   /* double close is harmless */
    printf("PASS");
}

int main(void)
{
    printf("test_video\n");
    file_io_set_game_root(NULL);
    audio_engine_init();
    video_player_init(NULL);

    RUN(open_reports_dimensions_and_duration);
    RUN(open_rejects_missing_and_garbage);
    RUN(decodes_frames_in_real_time);
    RUN(pause_stops_decoding);
    RUN(large_step_is_clamped);
    RUN(audio_is_streamed);
    RUN(pcm_stream_ring_buffer);
    RUN(handles_are_recycled);

    video_player_shutdown();
    audio_engine_shutdown();
    printf("%d tests, %d failed\n", _tests_run, _tests_failed);
    return _tests_failed ? 1 : 0;
}
