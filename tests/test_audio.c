/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#include "audio/audio_engine.h"
#include "bindings/bind_audio.h"
#include "bindings/bind_io.h"
#include "io/file_io.h"
#include "engine/js_engine.h"

#include <quickjs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Minimal test framework */

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    tests_run++; \
    printf("  [%d] %s ... ", tests_run, #name); \
    name(); \
    printf("PASS\n"); \
    tests_passed++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    assertion failed: %s\n    at %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ_INT(a, b) do { \
    int _a = (a), _b = (b); \
    if (_a != _b) { \
        printf("FAIL\n    expected %d == %d\n    at %s:%d\n", \
               _a, _b, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* WAV file generator for test fixtures */

/* Generate a minimal 16-bit mono WAV file with a sine wave. Caller frees. */
static uint8_t *generate_wav(float freq_hz, float duration_sec,
                              int sample_rate, size_t *out_size)
{
    int num_samples = (int)(sample_rate * duration_sec);
    int data_size = num_samples * 2; /* 16-bit = 2 bytes per sample */
    int file_size = 44 + data_size;  /* WAV header is 44 bytes */

    uint8_t *buf = (uint8_t *)calloc(1, file_size);
    if (!buf) return NULL;

    /* RIFF header */
    memcpy(buf, "RIFF", 4);
    int chunk_size = file_size - 8;
    buf[4] = chunk_size & 0xFF;
    buf[5] = (chunk_size >> 8) & 0xFF;
    buf[6] = (chunk_size >> 16) & 0xFF;
    buf[7] = (chunk_size >> 24) & 0xFF;
    memcpy(buf + 8, "WAVE", 4);

    /* fmt sub-chunk */
    memcpy(buf + 12, "fmt ", 4);
    buf[16] = 16; buf[17] = 0; buf[18] = 0; buf[19] = 0; /* sub-chunk size = 16 */
    buf[20] = 1; buf[21] = 0; /* PCM format */
    buf[22] = 1; buf[23] = 0; /* 1 channel (mono) */
    buf[24] = sample_rate & 0xFF;
    buf[25] = (sample_rate >> 8) & 0xFF;
    buf[26] = (sample_rate >> 16) & 0xFF;
    buf[27] = (sample_rate >> 24) & 0xFF;
    int byte_rate = sample_rate * 2; /* mono 16-bit */
    buf[28] = byte_rate & 0xFF;
    buf[29] = (byte_rate >> 8) & 0xFF;
    buf[30] = (byte_rate >> 16) & 0xFF;
    buf[31] = (byte_rate >> 24) & 0xFF;
    buf[32] = 2; buf[33] = 0; /* block align = 2 */
    buf[34] = 16; buf[35] = 0; /* bits per sample = 16 */

    /* data sub-chunk */
    memcpy(buf + 36, "data", 4);
    buf[40] = data_size & 0xFF;
    buf[41] = (data_size >> 8) & 0xFF;
    buf[42] = (data_size >> 16) & 0xFF;
    buf[43] = (data_size >> 24) & 0xFF;

    /* Generate sine wave samples */
    int16_t *samples = (int16_t *)(buf + 44);
    for (int i = 0; i < num_samples; i++) {
        double t = (double)i / sample_rate;
        double val = sin(2.0 * 3.14159265358979 * freq_hz * t);
        samples[i] = (int16_t)(val * 32000.0);
    }

    *out_size = file_size;
    return buf;
}

/* Write a WAV test fixture to disk. */
static int write_wav_fixture(const char *path, float freq_hz, float duration_sec)
{
    size_t size;
    uint8_t *buf = generate_wav(freq_hz, duration_sec, 44100, &size);
    if (!buf) return 0;

    FILE *f = fopen(path, "wb");
    if (!f) { free(buf); return 0; }
    size_t written = fwrite(buf, 1, size, f);
    fclose(f);
    free(buf);
    return written == size;
}

/* Test fixture paths */

static const char *TEST_WAV_SHORT = "tests/fixtures/test_audio_short.wav";
static const char *TEST_WAV_LONG  = "tests/fixtures/test_audio_long.wav";

static void setup_fixtures(void)
{
    file_io_mkdir("tests/fixtures");

    /* Generate test WAV files if they don't exist. */
    FILE *f = fopen(TEST_WAV_SHORT, "rb");
    if (!f) {
        write_wav_fixture(TEST_WAV_SHORT, 440.0f, 0.1f);  /* 100ms A4 tone */
    } else {
        fclose(f);
    }

    f = fopen(TEST_WAV_LONG, "rb");
    if (!f) {
        write_wav_fixture(TEST_WAV_LONG, 261.63f, 1.0f);  /* 1 second C4 tone */
    } else {
        fclose(f);
    }
}

/* C-level audio engine tests */

TEST(init_shutdown)
{
    ASSERT(audio_engine_init());
    ASSERT(audio_engine_is_initialized());
    audio_engine_shutdown();
    ASSERT(!audio_engine_is_initialized());
}

TEST(double_init)
{
    ASSERT(audio_engine_init());
    ASSERT(audio_engine_init()); /* idempotent */
    audio_engine_shutdown();
}

TEST(shutdown_without_init)
{
    audio_engine_shutdown();
    ASSERT(!audio_engine_is_initialized());
}

TEST(load_wav)
{
    audio_engine_init();
    AudioSourceHandle h = audio_load(TEST_WAV_SHORT);
    ASSERT(h != AUDIO_SOURCE_INVALID);
    audio_source_free(h);
    audio_engine_shutdown();
}

TEST(load_wav_long)
{
    audio_engine_init();
    AudioSourceHandle h = audio_load(TEST_WAV_LONG);
    ASSERT(h != AUDIO_SOURCE_INVALID);

    /* Duration should be roughly 1 second. */
    double dur = audio_get_duration(h);
    ASSERT(dur > 0.9 && dur < 1.1);

    audio_source_free(h);
    audio_engine_shutdown();
}

TEST(load_stream)
{
    audio_engine_init();
    AudioSourceHandle h = audio_load_stream(TEST_WAV_LONG);
    ASSERT(h != AUDIO_SOURCE_INVALID);
    audio_source_free(h);
    audio_engine_shutdown();
}

TEST(load_from_memory)
{
    audio_engine_init();

    FILE *f = fopen(TEST_WAV_SHORT, "rb");
    ASSERT(f != NULL);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = (uint8_t *)malloc(sz);
    fread(data, 1, sz, f);
    fclose(f);

    AudioSourceHandle h = audio_load_from_memory(data, (uint32_t)sz);
    ASSERT(h != AUDIO_SOURCE_INVALID);

    free(data);
    audio_source_free(h);
    audio_engine_shutdown();
}

TEST(load_nonexistent)
{
    audio_engine_init();
    AudioSourceHandle h = audio_load("tests/fixtures/nonexistent.wav");
    ASSERT(h == AUDIO_SOURCE_INVALID);
    audio_engine_shutdown();
}

TEST(load_null_path)
{
    audio_engine_init();
    AudioSourceHandle h = audio_load(NULL);
    ASSERT(h == AUDIO_SOURCE_INVALID);
    audio_engine_shutdown();
}

TEST(play_stop)
{
    audio_engine_init();
    AudioSourceHandle src = audio_load(TEST_WAV_SHORT);
    ASSERT(src != AUDIO_SOURCE_INVALID);

    AudioVoice v = audio_play(src, AUDIO_BUS_SE, 1.0f, 1.0f, 0.0f, false);
    ASSERT(v != AUDIO_VOICE_INVALID);

    ASSERT(audio_is_playing(v));

    audio_stop(v);
    ASSERT(!audio_is_playing(v));

    audio_source_free(src);
    audio_engine_shutdown();
}

TEST(play_on_each_bus)
{
    audio_engine_init();
    AudioSourceHandle src = audio_load(TEST_WAV_SHORT);
    ASSERT(src != AUDIO_SOURCE_INVALID);

    AudioVoice v_bgm = audio_play(src, AUDIO_BUS_BGM, 0.5f, 1.0f, 0.0f, false);
    AudioVoice v_bgs = audio_play(src, AUDIO_BUS_BGS, 0.5f, 1.0f, 0.0f, false);
    AudioVoice v_me  = audio_play(src, AUDIO_BUS_ME,  0.5f, 1.0f, 0.0f, false);
    AudioVoice v_se  = audio_play(src, AUDIO_BUS_SE,  0.5f, 1.0f, 0.0f, false);

    ASSERT(v_bgm != AUDIO_VOICE_INVALID);
    ASSERT(v_bgs != AUDIO_VOICE_INVALID);
    ASSERT(v_me  != AUDIO_VOICE_INVALID);
    ASSERT(v_se  != AUDIO_VOICE_INVALID);

    ASSERT(audio_is_playing(v_bgm));
    ASSERT(audio_is_playing(v_bgs));
    ASSERT(audio_is_playing(v_me));
    ASSERT(audio_is_playing(v_se));

    audio_stop_all();
    audio_source_free(src);
    audio_engine_shutdown();
}

TEST(pause_resume)
{
    audio_engine_init();
    AudioSourceHandle src = audio_load(TEST_WAV_LONG);
    ASSERT(src != AUDIO_SOURCE_INVALID);

    AudioVoice v = audio_play(src, AUDIO_BUS_BGM, 1.0f, 1.0f, 0.0f, true);
    ASSERT(v != AUDIO_VOICE_INVALID);
    ASSERT(audio_is_playing(v));
    ASSERT(!audio_is_paused(v));

    audio_pause(v);
    ASSERT(audio_is_paused(v));
    ASSERT(!audio_is_playing(v)); /* is_playing returns false when paused */

    audio_resume(v);
    ASSERT(!audio_is_paused(v));
    ASSERT(audio_is_playing(v));

    audio_stop(v);
    audio_source_free(src);
    audio_engine_shutdown();
}

TEST(volume_change)
{
    audio_engine_init();
    AudioSourceHandle src = audio_load(TEST_WAV_LONG);
    ASSERT(src != AUDIO_SOURCE_INVALID);

    AudioVoice v = audio_play(src, AUDIO_BUS_SE, 0.8f, 1.0f, 0.0f, true);
    ASSERT(v != AUDIO_VOICE_INVALID);

    float vol = audio_get_volume(v);
    ASSERT(vol > 0.79f && vol < 0.81f);

    audio_set_volume(v, 0.3f);
    vol = audio_get_volume(v);
    ASSERT(vol > 0.29f && vol < 0.31f);

    audio_stop(v);
    audio_source_free(src);
    audio_engine_shutdown();
}

TEST(pitch_change)
{
    audio_engine_init();
    AudioSourceHandle src = audio_load(TEST_WAV_LONG);
    ASSERT(src != AUDIO_SOURCE_INVALID);

    AudioVoice v = audio_play(src, AUDIO_BUS_SE, 1.0f, 2.0f, 0.0f, false);
    ASSERT(v != AUDIO_VOICE_INVALID);

    audio_set_pitch(v, 0.5f);

    audio_stop(v);
    audio_source_free(src);
    audio_engine_shutdown();
}

TEST(pan_change)
{
    audio_engine_init();
    AudioSourceHandle src = audio_load(TEST_WAV_SHORT);
    ASSERT(src != AUDIO_SOURCE_INVALID);

    AudioVoice v = audio_play(src, AUDIO_BUS_SE, 1.0f, 1.0f, -1.0f, false);
    ASSERT(v != AUDIO_VOICE_INVALID);

    audio_set_pan(v, 1.0f);
    audio_set_pan(v, 0.0f);

    audio_stop(v);
    audio_source_free(src);
    audio_engine_shutdown();
}

TEST(bus_volume)
{
    audio_engine_init();

    audio_set_bus_volume(AUDIO_BUS_BGM, 0.7f);
    audio_set_bus_volume(AUDIO_BUS_BGS, 0.5f);
    audio_set_bus_volume(AUDIO_BUS_ME,  0.8f);
    audio_set_bus_volume(AUDIO_BUS_SE,  1.0f);


    audio_engine_shutdown();
}

TEST(fade_volume)
{
    audio_engine_init();
    AudioSourceHandle src = audio_load(TEST_WAV_LONG);
    ASSERT(src != AUDIO_SOURCE_INVALID);

    AudioVoice v = audio_play(src, AUDIO_BUS_BGM, 1.0f, 1.0f, 0.0f, true);
    ASSERT(v != AUDIO_VOICE_INVALID);

    audio_fade_volume(v, 0.0f, 0.5f);

    audio_stop(v);
    audio_source_free(src);
    audio_engine_shutdown();
}

TEST(fade_bus_volume)
{
    audio_engine_init();

    audio_fade_bus_volume(AUDIO_BUS_BGM, 0.0f, 1.0f);

    audio_engine_shutdown();
}

TEST(stop_bus)
{
    audio_engine_init();
    AudioSourceHandle src = audio_load(TEST_WAV_LONG);
    ASSERT(src != AUDIO_SOURCE_INVALID);

    AudioVoice v1 = audio_play(src, AUDIO_BUS_SE, 1.0f, 1.0f, 0.0f, true);
    AudioVoice v2 = audio_play(src, AUDIO_BUS_SE, 1.0f, 1.0f, 0.0f, true);
    ASSERT(v1 != AUDIO_VOICE_INVALID);
    ASSERT(v2 != AUDIO_VOICE_INVALID);

    audio_stop_bus(AUDIO_BUS_SE);
    ASSERT(!audio_is_playing(v1));
    ASSERT(!audio_is_playing(v2));

    audio_source_free(src);
    audio_engine_shutdown();
}

TEST(pause_resume_bus)
{
    audio_engine_init();
    AudioSourceHandle src = audio_load(TEST_WAV_LONG);
    ASSERT(src != AUDIO_SOURCE_INVALID);

    AudioVoice v = audio_play(src, AUDIO_BUS_BGM, 1.0f, 1.0f, 0.0f, true);
    ASSERT(v != AUDIO_VOICE_INVALID);

    audio_pause_bus(AUDIO_BUS_BGM);
    /* Pausing the bus pauses the bus voice handle, not individual voices. */

    audio_resume_bus(AUDIO_BUS_BGM);

    audio_stop(v);
    audio_source_free(src);
    audio_engine_shutdown();
}

TEST(active_voice_count)
{
    audio_engine_init();
    AudioSourceHandle src = audio_load(TEST_WAV_LONG);
    ASSERT(src != AUDIO_SOURCE_INVALID);

    int initial = audio_get_active_voice_count();

    AudioVoice v = audio_play(src, AUDIO_BUS_SE, 1.0f, 1.0f, 0.0f, true);
    ASSERT(v != AUDIO_VOICE_INVALID);

    int after_play = audio_get_active_voice_count();
    ASSERT(after_play > initial);

    audio_stop(v);
    audio_source_free(src);
    audio_engine_shutdown();
}

TEST(play_invalid_source)
{
    audio_engine_init();
    AudioVoice v = audio_play(AUDIO_SOURCE_INVALID, AUDIO_BUS_SE, 1.0f, 1.0f, 0.0f, false);
    ASSERT(v == AUDIO_VOICE_INVALID);
    audio_engine_shutdown();
}

TEST(operations_on_invalid_voice)
{
    audio_engine_init();
    /* All of these should be no-ops. */
    audio_stop(AUDIO_VOICE_INVALID);
    audio_pause(AUDIO_VOICE_INVALID);
    audio_resume(AUDIO_VOICE_INVALID);
    audio_set_volume(AUDIO_VOICE_INVALID, 0.5f);
    audio_set_pitch(AUDIO_VOICE_INVALID, 1.0f);
    audio_set_pan(AUDIO_VOICE_INVALID, 0.0f);
    audio_fade_volume(AUDIO_VOICE_INVALID, 0.0f, 1.0f);
    ASSERT(!audio_is_playing(AUDIO_VOICE_INVALID));
    ASSERT(!audio_is_paused(AUDIO_VOICE_INVALID));
    ASSERT(audio_get_volume(AUDIO_VOICE_INVALID) == 0.0f);
    audio_engine_shutdown();
}

TEST(source_free_and_reuse)
{
    audio_engine_init();
    AudioSourceHandle h1 = audio_load(TEST_WAV_SHORT);
    ASSERT(h1 != AUDIO_SOURCE_INVALID);
    audio_source_free(h1);

    AudioSourceHandle h2 = audio_load(TEST_WAV_SHORT);
    ASSERT(h2 != AUDIO_SOURCE_INVALID);
    audio_source_free(h2);
    audio_engine_shutdown();
}

TEST(get_duration)
{
    audio_engine_init();
    AudioSourceHandle h = audio_load(TEST_WAV_SHORT);
    ASSERT(h != AUDIO_SOURCE_INVALID);

    double dur = audio_get_duration(h);
    ASSERT(dur > 0.05 && dur < 0.15);

    audio_source_free(h);
    audio_engine_shutdown();
}

TEST(update_no_crash)
{
    audio_engine_init();
    audio_update();
    audio_update();
    audio_update();
    audio_engine_shutdown();
}

/* JS-level binding tests */

static JSEngine *s_js = NULL;

static void js_setup(void)
{
    s_js = js_engine_init();
    bind_io_register(js_engine_get_context(s_js));
    bind_audio_register(js_engine_get_context(s_js));
    file_io_set_game_root(".");
}

static void js_teardown(void)
{
    js_engine_shutdown(s_js);
    s_js = NULL;
}

/* Evaluate JS and check it returns "true". */
static int js_check(const char *script)
{
    char *result = js_engine_eval_string(s_js, script, "<test>");
    if (!result) return 0;
    int ok = (strcmp(result, "true") == 0);
    js_engine_free_string(result);
    return ok;
}

TEST(js_init_shutdown)
{
    js_setup();

    ASSERT(js_check("__native_audio.init()"));
    ASSERT(js_check("__native_audio.isInitialized()"));
    js_engine_eval(s_js, "__native_audio.shutdown()", "<test>");
    ASSERT(js_check("!__native_audio.isInitialized()"));

    js_teardown();
}

TEST(js_bus_constants)
{
    js_setup();

    ASSERT(js_check("__native_audio.BUS_BGM === 0"));
    ASSERT(js_check("__native_audio.BUS_BGS === 1"));
    ASSERT(js_check("__native_audio.BUS_ME === 2"));
    ASSERT(js_check("__native_audio.BUS_SE === 3"));

    js_teardown();
}

TEST(js_load_play_stop)
{
    js_setup();
    js_engine_eval(s_js, "__native_audio.init()", "<test>");

    ASSERT(js_check(
        "var src = __native_audio.load('tests/fixtures/test_audio_short.wav');"
        "src > 0"
    ));

    ASSERT(js_check(
        "var voice = __native_audio.play(src, __native_audio.BUS_SE, 1.0, 1.0, 0.0, false);"
        "voice > 0"
    ));

    ASSERT(js_check("__native_audio.isPlaying(voice)"));

    js_engine_eval(s_js, "__native_audio.stop(voice)", "<test>");
    ASSERT(js_check("!__native_audio.isPlaying(voice)"));

    js_engine_eval(s_js, "__native_audio.freeSource(src)", "<test>");

    js_engine_eval(s_js, "__native_audio.shutdown()", "<test>");
    js_teardown();
}

TEST(js_volume_control)
{
    js_setup();
    js_engine_eval(s_js, "__native_audio.init()", "<test>");

    js_engine_eval(s_js,
        "var src = __native_audio.load('tests/fixtures/test_audio_long.wav');"
        "var voice = __native_audio.play(src, __native_audio.BUS_BGM, 0.8, 1.0, 0.0, true);",
        "<test>");

    ASSERT(js_check(
        "var vol = __native_audio.getVolume(voice);"
        "vol > 0.79 && vol < 0.81"
    ));

    js_engine_eval(s_js, "__native_audio.setVolume(voice, 0.4)", "<test>");
    ASSERT(js_check(
        "var vol2 = __native_audio.getVolume(voice);"
        "vol2 > 0.39 && vol2 < 0.41"
    ));

    js_engine_eval(s_js,
        "__native_audio.stop(voice);"
        "__native_audio.freeSource(src);"
        "__native_audio.shutdown();",
        "<test>");
    js_teardown();
}

TEST(js_pause_resume)
{
    js_setup();
    js_engine_eval(s_js, "__native_audio.init()", "<test>");

    js_engine_eval(s_js,
        "var src = __native_audio.load('tests/fixtures/test_audio_long.wav');"
        "var voice = __native_audio.play(src, __native_audio.BUS_SE, 1.0, 1.0, 0.0, true);",
        "<test>");

    ASSERT(js_check("__native_audio.isPlaying(voice)"));
    ASSERT(js_check("!__native_audio.isPaused(voice)"));

    js_engine_eval(s_js, "__native_audio.pause(voice)", "<test>");
    ASSERT(js_check("__native_audio.isPaused(voice)"));

    js_engine_eval(s_js, "__native_audio.resume(voice)", "<test>");
    ASSERT(js_check("!__native_audio.isPaused(voice)"));

    js_engine_eval(s_js,
        "__native_audio.stop(voice);"
        "__native_audio.freeSource(src);"
        "__native_audio.shutdown();",
        "<test>");
    js_teardown();
}

TEST(js_load_nonexistent)
{
    js_setup();
    js_engine_eval(s_js, "__native_audio.init()", "<test>");

    ASSERT(js_check(
        "var src = __native_audio.load('nonexistent_file.ogg');"
        "src === 0"
    ));

    js_engine_eval(s_js, "__native_audio.shutdown()", "<test>");
    js_teardown();
}

TEST(js_get_duration)
{
    js_setup();
    js_engine_eval(s_js, "__native_audio.init()", "<test>");

    ASSERT(js_check(
        "var src = __native_audio.load('tests/fixtures/test_audio_long.wav');"
        "var dur = __native_audio.getDuration(src);"
        "dur > 0.9 && dur < 1.1"
    ));

    js_engine_eval(s_js,
        "__native_audio.freeSource(src);"
        "__native_audio.shutdown();",
        "<test>");
    js_teardown();
}

TEST(js_fade_volume)
{
    js_setup();
    js_engine_eval(s_js, "__native_audio.init()", "<test>");

    js_engine_eval(s_js,
        "var src = __native_audio.load('tests/fixtures/test_audio_long.wav');"
        "var voice = __native_audio.play(src, __native_audio.BUS_BGM, 1.0, 1.0, 0.0, true);"
        "__native_audio.fadeVolume(voice, 0.0, 0.5);",
        "<test>");

    js_engine_eval(s_js,
        "__native_audio.stop(voice);"
        "__native_audio.freeSource(src);"
        "__native_audio.shutdown();",
        "<test>");
    js_teardown();
}

TEST(js_bus_operations)
{
    js_setup();
    js_engine_eval(s_js, "__native_audio.init()", "<test>");

    js_engine_eval(s_js,
        "__native_audio.setBusVolume(__native_audio.BUS_BGM, 0.7);"
        "__native_audio.fadeBusVolume(__native_audio.BUS_BGS, 0.5, 1.0);"
        "__native_audio.stopBus(__native_audio.BUS_SE);"
        "__native_audio.pauseBus(__native_audio.BUS_ME);"
        "__native_audio.resumeBus(__native_audio.BUS_ME);",
        "<test>");

    js_engine_eval(s_js, "__native_audio.shutdown()", "<test>");
    js_teardown();
}

/* Main */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    printf("=== Audio Engine Tests ===\n");

    setup_fixtures();

    printf("\n--- C-level tests ---\n");
    RUN(init_shutdown);
    RUN(double_init);
    RUN(shutdown_without_init);
    RUN(load_wav);
    RUN(load_wav_long);
    RUN(load_stream);
    RUN(load_from_memory);
    RUN(load_nonexistent);
    RUN(load_null_path);
    RUN(play_stop);
    RUN(play_on_each_bus);
    RUN(pause_resume);
    RUN(volume_change);
    RUN(pitch_change);
    RUN(pan_change);
    RUN(bus_volume);
    RUN(fade_volume);
    RUN(fade_bus_volume);
    RUN(stop_bus);
    RUN(pause_resume_bus);
    RUN(active_voice_count);
    RUN(play_invalid_source);
    RUN(operations_on_invalid_voice);
    RUN(source_free_and_reuse);
    RUN(get_duration);
    RUN(update_no_crash);

    printf("\n--- JS binding tests ---\n");
    RUN(js_init_shutdown);
    RUN(js_bus_constants);
    RUN(js_load_play_stop);
    RUN(js_volume_control);
    RUN(js_pause_resume);
    RUN(js_load_nonexistent);
    RUN(js_get_duration);
    RUN(js_fade_volume);
    RUN(js_bus_operations);

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
