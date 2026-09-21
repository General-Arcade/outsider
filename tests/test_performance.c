/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

/*
 * tests/test_performance.c — Performance benchmarks: sprite batch culling,
 * audio source allocation, JS bytecode caching, game loop stats.
 */

#include "rendering/sprite_batch.h"
#include "rendering/renderer.h"
#include "audio/audio_engine.h"
#include "engine/js_engine.h"
#include "engine/game_loop.h"
#include "io/file_io.h"
#include "bindings/bind_io.h"

#include <quickjs.h>
#include <SDL3/SDL.h>
#include "test_paths.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#ifdef _WIN32
#include <sys/utime.h>
#else
#include <utime.h>
#endif

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
        printf("FAIL\n    expected %d == %d (%s == %s)\n    at %s:%d\n", \
               _a, _b, #a, #b, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* Sprite batch tests — indexed rendering and frustum culling */

TEST(test_sprite_batch_create_with_index_buffer)
{
    /* Creation should succeed even without a GL context. */
    SpriteBatch *sb = sprite_batch_create(256);
    ASSERT(sb != NULL);
    ASSERT_EQ_INT(sprite_batch_get_draw_calls(sb), 0);
    ASSERT_EQ_INT(sprite_batch_get_quad_count(sb), 0);
    ASSERT_EQ_INT(sprite_batch_get_culled_count(sb), 0);
    sprite_batch_destroy(sb);
}

TEST(test_sprite_batch_frustum_culling_offscreen_right)
{
    SpriteBatch *sb = sprite_batch_create(256);
    ASSERT(sb != NULL);
    sprite_batch_set_projection(sb, 816.0f, 624.0f);
    sprite_batch_begin(sb);

    /* Draw a quad entirely off-screen to the right */
    sprite_batch_draw(sb, 1, 900.0f, 0.0f, 50.0f, 50.0f,
                      0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, 1.0f);

    sprite_batch_end(sb);

    ASSERT_EQ_INT(sprite_batch_get_quad_count(sb), 0);
    ASSERT_EQ_INT(sprite_batch_get_culled_count(sb), 1);
    sprite_batch_destroy(sb);
}

TEST(test_sprite_batch_frustum_culling_offscreen_left)
{
    SpriteBatch *sb = sprite_batch_create(256);
    ASSERT(sb != NULL);
    sprite_batch_set_projection(sb, 816.0f, 624.0f);
    sprite_batch_begin(sb);

    /* Draw a quad entirely off-screen to the left */
    sprite_batch_draw(sb, 1, -100.0f, 0.0f, 50.0f, 50.0f,
                      0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, 1.0f);

    sprite_batch_end(sb);

    ASSERT_EQ_INT(sprite_batch_get_quad_count(sb), 0);
    ASSERT_EQ_INT(sprite_batch_get_culled_count(sb), 1);
    sprite_batch_destroy(sb);
}

TEST(test_sprite_batch_frustum_culling_offscreen_below)
{
    SpriteBatch *sb = sprite_batch_create(256);
    ASSERT(sb != NULL);
    sprite_batch_set_projection(sb, 816.0f, 624.0f);
    sprite_batch_begin(sb);

    /* Draw a quad entirely off-screen below */
    sprite_batch_draw(sb, 1, 0.0f, 700.0f, 50.0f, 50.0f,
                      0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, 1.0f);

    sprite_batch_end(sb);

    ASSERT_EQ_INT(sprite_batch_get_quad_count(sb), 0);
    ASSERT_EQ_INT(sprite_batch_get_culled_count(sb), 1);
    sprite_batch_destroy(sb);
}

TEST(test_sprite_batch_frustum_culling_offscreen_above)
{
    SpriteBatch *sb = sprite_batch_create(256);
    ASSERT(sb != NULL);
    sprite_batch_set_projection(sb, 816.0f, 624.0f);
    sprite_batch_begin(sb);

    /* Draw a quad entirely off-screen above */
    sprite_batch_draw(sb, 1, 0.0f, -100.0f, 50.0f, 50.0f,
                      0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, 1.0f);

    sprite_batch_end(sb);

    ASSERT_EQ_INT(sprite_batch_get_quad_count(sb), 0);
    ASSERT_EQ_INT(sprite_batch_get_culled_count(sb), 1);
    sprite_batch_destroy(sb);
}

TEST(test_sprite_batch_frustum_culling_visible)
{
    SpriteBatch *sb = sprite_batch_create(256);
    ASSERT(sb != NULL);
    sprite_batch_set_projection(sb, 816.0f, 624.0f);
    sprite_batch_begin(sb);

    /* Draw a quad that is on-screen */
    sprite_batch_draw(sb, 1, 100.0f, 100.0f, 50.0f, 50.0f,
                      0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, 1.0f);

    sprite_batch_end(sb);

    ASSERT_EQ_INT(sprite_batch_get_quad_count(sb), 1);
    ASSERT_EQ_INT(sprite_batch_get_culled_count(sb), 0);
    sprite_batch_destroy(sb);
}

TEST(test_sprite_batch_frustum_culling_partially_visible)
{
    SpriteBatch *sb = sprite_batch_create(256);
    ASSERT(sb != NULL);
    sprite_batch_set_projection(sb, 816.0f, 624.0f);
    sprite_batch_begin(sb);

    /* Draw a quad partially off-screen — should NOT be culled */
    sprite_batch_draw(sb, 1, -25.0f, -25.0f, 50.0f, 50.0f,
                      0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, 1.0f);

    sprite_batch_end(sb);

    ASSERT_EQ_INT(sprite_batch_get_quad_count(sb), 1);
    ASSERT_EQ_INT(sprite_batch_get_culled_count(sb), 0);
    sprite_batch_destroy(sb);
}

TEST(test_sprite_batch_cull_transparent)
{
    SpriteBatch *sb = sprite_batch_create(256);
    ASSERT(sb != NULL);
    sprite_batch_set_projection(sb, 816.0f, 624.0f);
    sprite_batch_begin(sb);

    /* Draw a fully transparent quad — should be culled */
    sprite_batch_draw(sb, 1, 100.0f, 100.0f, 50.0f, 50.0f,
                      0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, 0.0f);

    sprite_batch_end(sb);

    ASSERT_EQ_INT(sprite_batch_get_quad_count(sb), 0);
    ASSERT_EQ_INT(sprite_batch_get_culled_count(sb), 1);
    sprite_batch_destroy(sb);
}

TEST(test_sprite_batch_mixed_visible_culled)
{
    SpriteBatch *sb = sprite_batch_create(256);
    ASSERT(sb != NULL);
    sprite_batch_set_projection(sb, 816.0f, 624.0f);
    sprite_batch_begin(sb);

    /* 3 visible quads */
    sprite_batch_draw(sb, 1, 0.0f, 0.0f, 50.0f, 50.0f,
                      0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, 1.0f);
    sprite_batch_draw(sb, 1, 100.0f, 100.0f, 50.0f, 50.0f,
                      0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, 1.0f);
    sprite_batch_draw(sb, 1, 400.0f, 300.0f, 50.0f, 50.0f,
                      0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, 1.0f);

    /* 2 culled quads (off-screen) */
    sprite_batch_draw(sb, 1, -200.0f, 0.0f, 50.0f, 50.0f,
                      0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, 1.0f);
    sprite_batch_draw(sb, 1, 0.0f, 700.0f, 50.0f, 50.0f,
                      0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, 1.0f);

    sprite_batch_end(sb);

    ASSERT_EQ_INT(sprite_batch_get_quad_count(sb), 3);
    ASSERT_EQ_INT(sprite_batch_get_culled_count(sb), 2);
    sprite_batch_destroy(sb);
}

TEST(test_sprite_batch_viewport_update_affects_culling)
{
    SpriteBatch *sb = sprite_batch_create(256);
    ASSERT(sb != NULL);

    /* Set a small viewport */
    sprite_batch_set_projection(sb, 100.0f, 100.0f);
    sprite_batch_begin(sb);

    /* This quad is outside the small viewport */
    sprite_batch_draw(sb, 1, 150.0f, 150.0f, 50.0f, 50.0f,
                      0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, 1.0f);

    sprite_batch_end(sb);
    ASSERT_EQ_INT(sprite_batch_get_culled_count(sb), 1);

    /* Expand viewport — same quad should now be visible */
    sprite_batch_set_projection(sb, 816.0f, 624.0f);
    sprite_batch_begin(sb);

    sprite_batch_draw(sb, 1, 150.0f, 150.0f, 50.0f, 50.0f,
                      0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, 1.0f);

    sprite_batch_end(sb);
    ASSERT_EQ_INT(sprite_batch_get_culled_count(sb), 0);
    ASSERT_EQ_INT(sprite_batch_get_quad_count(sb), 1);

    sprite_batch_destroy(sb);
}

/* Rendering benchmark — sprite batch throughput */

TEST(test_benchmark_sprite_batch_throughput)
{
    SpriteBatch *sb = sprite_batch_create(4096);
    ASSERT(sb != NULL);
    sprite_batch_set_projection(sb, 816.0f, 624.0f);

    /* Benchmark: queue and flush 4000 quads */
    clock_t start = clock();
    int iterations = 100;

    for (int iter = 0; iter < iterations; iter++) {
        sprite_batch_begin(sb);
        for (int i = 0; i < 4000; i++) {
            float x = (float)(i % 80) * 10.0f;
            float y = (float)(i / 80) * 12.0f;
            sprite_batch_draw(sb, 1, x, y, 10.0f, 12.0f,
                              0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, 1.0f);
        }
        sprite_batch_end(sb);
    }

    clock_t end = clock();
    double elapsed_ms = (double)(end - start) * 1000.0 / CLOCKS_PER_SEC;
    double quads_per_sec = (4000.0 * iterations) / (elapsed_ms / 1000.0);

    printf("(%.1fms for %d frames, %.0f quads/sec) ", elapsed_ms, iterations, quads_per_sec);

    ASSERT_EQ_INT(sprite_batch_get_quad_count(sb), 4000);
    ASSERT(quads_per_sec > 100000.0);

    sprite_batch_destroy(sb);
}

/* Audio engine profiling tests */

TEST(test_audio_free_list_allocation)
{
    ASSERT(audio_engine_init());
    ASSERT_EQ_INT(audio_get_loaded_source_count(), 0);

    /* Generate a minimal WAV for testing */
    int sample_rate = 44100;
    int num_samples = sample_rate / 10; /* 100ms */
    int data_size = num_samples * 2;
    int file_size = 44 + data_size;
    uint8_t *wav = calloc(1, (size_t)file_size);
    ASSERT(wav != NULL);

    /* WAV header */
    memcpy(wav, "RIFF", 4);
    int chunk_size = file_size - 8;
    memcpy(wav + 4, &chunk_size, 4);
    memcpy(wav + 8, "WAVEfmt ", 8);
    int fmt_size = 16;
    memcpy(wav + 16, &fmt_size, 4);
    short audio_format = 1;
    memcpy(wav + 20, &audio_format, 2);
    short channels = 1;
    memcpy(wav + 22, &channels, 2);
    memcpy(wav + 24, &sample_rate, 4);
    int byte_rate = sample_rate * 2;
    memcpy(wav + 28, &byte_rate, 4);
    short block_align = 2;
    memcpy(wav + 32, &block_align, 2);
    short bits_per_sample = 16;
    memcpy(wav + 34, &bits_per_sample, 2);
    memcpy(wav + 36, "data", 4);
    memcpy(wav + 40, &data_size, 4);

    /* Load a few sources and verify count */
    AudioSourceHandle h1 = audio_load_from_memory(wav, (uint32_t)file_size);
    ASSERT(h1 != AUDIO_SOURCE_INVALID);
    ASSERT_EQ_INT(audio_get_loaded_source_count(), 1);

    AudioSourceHandle h2 = audio_load_from_memory(wav, (uint32_t)file_size);
    ASSERT(h2 != AUDIO_SOURCE_INVALID);
    ASSERT_EQ_INT(audio_get_loaded_source_count(), 2);

    /* Free one and verify count decreases */
    audio_source_free(h1);
    ASSERT_EQ_INT(audio_get_loaded_source_count(), 1);

    /* Reallocate — should reuse the freed slot */
    AudioSourceHandle h3 = audio_load_from_memory(wav, (uint32_t)file_size);
    ASSERT(h3 != AUDIO_SOURCE_INVALID);
    ASSERT_EQ_INT(audio_get_loaded_source_count(), 2);

    audio_source_free(h2);
    audio_source_free(h3);
    ASSERT_EQ_INT(audio_get_loaded_source_count(), 0);

    free(wav);
    audio_engine_shutdown();
}

TEST(test_audio_peak_voice_tracking)
{
    ASSERT(audio_engine_init());

    ASSERT_EQ_INT(audio_get_peak_voice_count(), 0);

    audio_update();
    /* No voices playing, peak should still be 0 */
    ASSERT_EQ_INT(audio_get_peak_voice_count(), 0);

    audio_reset_peak_voice_count();
    ASSERT_EQ_INT(audio_get_peak_voice_count(), 0);

    audio_engine_shutdown();
}

/* JS engine bytecode caching tests */

TEST(test_bytecode_caching_basic)
{
    JSEngine *engine = js_engine_init();
    ASSERT(engine != NULL);

    const char *test_js = "var _bc_test_result = 42;";
    FILE *f = fopen(test_tmp_path("test_bc.js"), "w");
    ASSERT(f != NULL);
    fputs(test_js, f);
    fclose(f);

    remove(test_tmp_path("test_bc.jsc"));

    /* First eval — should compile and cache */
    bool ok = js_engine_eval_file_cached(engine, test_tmp_path("test_bc.js"));
    ASSERT(ok);

    char *result = js_engine_eval_string(engine, "_bc_test_result.toString()", "<test>");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "42") == 0);
    js_engine_free_string(result);

    /* Verify cache file was created */
    f = fopen(test_tmp_path("test_bc.jsc"), "rb");
    ASSERT(f != NULL);
    fclose(f);

    js_engine_shutdown(engine);

    remove(test_tmp_path("test_bc.js"));
    remove(test_tmp_path("test_bc.jsc"));
}

TEST(test_bytecode_caching_loads_from_cache)
{
    const char *test_js = "var _bc_cached_val = 123;";
    FILE *f = fopen(test_tmp_path("test_bc2.js"), "w");
    ASSERT(f != NULL);
    fputs(test_js, f);
    fclose(f);
    remove(test_tmp_path("test_bc2.jsc"));

    /* First pass: compile and cache */
    JSEngine *engine1 = js_engine_init();
    ASSERT(engine1 != NULL);
    bool ok = js_engine_eval_file_cached(engine1, test_tmp_path("test_bc2.js"));
    ASSERT(ok);
    js_engine_shutdown(engine1);

    /* Verify cache exists */
    f = fopen(test_tmp_path("test_bc2.jsc"), "rb");
    ASSERT(f != NULL);
    fclose(f);

    /* Second pass: should load from cache */
    JSEngine *engine2 = js_engine_init();
    ASSERT(engine2 != NULL);
    ok = js_engine_eval_file_cached(engine2, test_tmp_path("test_bc2.js"));
    ASSERT(ok);

    char *result = js_engine_eval_string(engine2, "_bc_cached_val.toString()", "<test>");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "123") == 0);
    js_engine_free_string(result);

    js_engine_shutdown(engine2);

    remove(test_tmp_path("test_bc2.js"));
    remove(test_tmp_path("test_bc2.jsc"));
}

TEST(test_bytecode_caching_invalidates_on_change)
{
    const char *test_js_v1 = "var _bc_version = 1;";
    FILE *f = fopen(test_tmp_path("test_bc3.js"), "w");
    ASSERT(f != NULL);
    fputs(test_js_v1, f);
    fclose(f);
    remove(test_tmp_path("test_bc3.jsc"));

    /* First pass: compile and cache */
    JSEngine *engine1 = js_engine_init();
    ASSERT(engine1 != NULL);
    bool ok = js_engine_eval_file_cached(engine1, test_tmp_path("test_bc3.js"));
    ASSERT(ok);
    char *r1 = js_engine_eval_string(engine1, "_bc_version.toString()", "<test>");
    ASSERT(r1 != NULL);
    ASSERT(strcmp(r1, "1") == 0);
    js_engine_free_string(r1);
    js_engine_shutdown(engine1);

    /* Modify the source file and ensure mtime changes (cache uses mtime). */
    const char *test_js_v2 = "var _bc_version = 2; /* updated content with different size */";
    f = fopen(test_tmp_path("test_bc3.js"), "w");
    ASSERT(f != NULL);
    fputs(test_js_v2, f);
    fclose(f);
    /* Force mtime to be different from when cache was written. */
    struct utimbuf times;
    times.actime = time(NULL) + 2;
    times.modtime = time(NULL) + 2;
    utime(test_tmp_path("test_bc3.js"), &times);

    /* Second pass: should recompile because mtime changed */
    JSEngine *engine2 = js_engine_init();
    ASSERT(engine2 != NULL);
    ok = js_engine_eval_file_cached(engine2, test_tmp_path("test_bc3.js"));
    ASSERT(ok);
    char *r2 = js_engine_eval_string(engine2, "_bc_version.toString()", "<test>");
    ASSERT(r2 != NULL);
    ASSERT(strcmp(r2, "2") == 0);
    js_engine_free_string(r2);
    js_engine_shutdown(engine2);

    remove(test_tmp_path("test_bc3.js"));
    remove(test_tmp_path("test_bc3.jsc"));
}

/* JS eval benchmark */

TEST(test_benchmark_js_eval)
{
    JSEngine *engine = js_engine_init();
    ASSERT(engine != NULL);

    /* Benchmark: evaluate many small expressions */
    clock_t start = clock();
    int iterations = 10000;

    for (int i = 0; i < iterations; i++) {
        bool ok = js_engine_eval(engine, "var _bench_x = 1 + 2 + 3;", "<bench>");
        ASSERT(ok);
    }

    clock_t end = clock();
    double elapsed_ms = (double)(end - start) * 1000.0 / CLOCKS_PER_SEC;
    double evals_per_sec = (double)iterations / (elapsed_ms / 1000.0);

    printf("(%.1fms for %d evals, %.0f evals/sec) ", elapsed_ms, iterations, evals_per_sec);

    ASSERT(evals_per_sec > 10000.0);

    js_engine_shutdown(engine);
}

TEST(test_benchmark_bytecode_vs_source)
{
    JSEngine *engine = js_engine_init();
    ASSERT(engine != NULL);

    /* Write a moderately complex JS file */
    const char *test_js =
        "var _bench_sum = 0;\n"
        "for (var _bench_i = 0; _bench_i < 100; _bench_i++) {\n"
        "    _bench_sum += _bench_i * _bench_i;\n"
        "}\n";

    FILE *f = fopen(test_tmp_path("test_bench.js"), "w");
    ASSERT(f != NULL);
    fputs(test_js, f);
    fclose(f);
    remove(test_tmp_path("test_bench.jsc"));

    /* Benchmark source eval */
    clock_t start = clock();
    int iterations = 500;
    for (int i = 0; i < iterations; i++) {
        bool ok = js_engine_eval_file(engine, test_tmp_path("test_bench.js"));
        ASSERT(ok);
    }
    clock_t mid = clock();

    /* Prime the cache */
    js_engine_eval_file_cached(engine, test_tmp_path("test_bench.js"));

    /* Benchmark cached eval */
    clock_t start2 = clock();
    for (int i = 0; i < iterations; i++) {
        bool ok = js_engine_eval_file_cached(engine, test_tmp_path("test_bench.js"));
        ASSERT(ok);
    }
    clock_t end = clock();

    double source_ms = (double)(mid - start) * 1000.0 / CLOCKS_PER_SEC;
    double cached_ms = (double)(end - start2) * 1000.0 / CLOCKS_PER_SEC;

    printf("(source: %.1fms, cached: %.1fms, ratio: %.2fx) ",
           source_ms, cached_ms, source_ms / (cached_ms > 0.001 ? cached_ms : 0.001));

    /* Generous bound: reading bytecode from disk can be slower than parsing tiny files. */
    ASSERT(cached_ms < source_ms * 5.0);

    js_engine_shutdown(engine);

    remove(test_tmp_path("test_bench.js"));
    remove(test_tmp_path("test_bench.jsc"));
}

/* Game loop stats tests */

TEST(test_game_loop_stats_struct)
{
    GameLoopStats stats = {0};
    stats.events_ms = 1.5;
    stats.timers_ms = 0.5;
    stats.jobs_ms = 0.1;
    stats.raf_ms = 5.0;
    stats.swap_ms = 2.0;
    stats.total_ms = 9.1;
    stats.draw_calls = 15;
    stats.quad_count = 500;

    ASSERT(stats.events_ms > 0.0);
    ASSERT(stats.total_ms > 0.0);
    ASSERT_EQ_INT(stats.draw_calls, 15);
    ASSERT_EQ_INT(stats.quad_count, 500);
}

TEST(test_game_loop_null_stats)
{
    /* game_loop_get_stats with NULL should return zeroed stats */
    GameLoopStats stats = game_loop_get_stats(NULL);
    ASSERT(stats.events_ms == 0.0);
    ASSERT(stats.total_ms == 0.0);
    ASSERT_EQ_INT(stats.draw_calls, 0);
    ASSERT_EQ_INT(stats.quad_count, 0);
}

/* Renderer tests — culled count exposed through renderer */

TEST(test_renderer_batch_culling)
{
    Renderer *r = renderer_create(816, 624);
    ASSERT(r != NULL);

    SpriteBatch *sb = renderer_get_batch(r);
    ASSERT(sb != NULL);

    sprite_batch_begin(sb);

    /* 2 visible */
    sprite_batch_draw(sb, 1, 10.0f, 10.0f, 50.0f, 50.0f,
                      0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, 1.0f);
    sprite_batch_draw(sb, 1, 200.0f, 200.0f, 50.0f, 50.0f,
                      0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, 1.0f);

    /* 1 culled */
    sprite_batch_draw(sb, 1, 900.0f, 0.0f, 50.0f, 50.0f,
                      0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF, 1.0f);

    sprite_batch_end(sb);

    ASSERT_EQ_INT(sprite_batch_get_quad_count(sb), 2);
    ASSERT_EQ_INT(sprite_batch_get_culled_count(sb), 1);

    renderer_destroy(r);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    printf("=== Performance Optimization Tests ===\n\n");

    printf("--- Sprite Batch (indexed + culling) ---\n");
    RUN(test_sprite_batch_create_with_index_buffer);
    RUN(test_sprite_batch_frustum_culling_offscreen_right);
    RUN(test_sprite_batch_frustum_culling_offscreen_left);
    RUN(test_sprite_batch_frustum_culling_offscreen_below);
    RUN(test_sprite_batch_frustum_culling_offscreen_above);
    RUN(test_sprite_batch_frustum_culling_visible);
    RUN(test_sprite_batch_frustum_culling_partially_visible);
    RUN(test_sprite_batch_cull_transparent);
    RUN(test_sprite_batch_mixed_visible_culled);
    RUN(test_sprite_batch_viewport_update_affects_culling);

    printf("\n--- Rendering Benchmark ---\n");
    RUN(test_benchmark_sprite_batch_throughput);

    printf("\n--- Audio Profiling ---\n");
    RUN(test_audio_free_list_allocation);
    RUN(test_audio_peak_voice_tracking);

    printf("\n--- Bytecode Caching ---\n");
    RUN(test_bytecode_caching_basic);
    RUN(test_bytecode_caching_loads_from_cache);
    RUN(test_bytecode_caching_invalidates_on_change);

    printf("\n--- JS Eval Benchmark ---\n");
    RUN(test_benchmark_js_eval);
    RUN(test_benchmark_bytecode_vs_source);

    printf("\n--- Game Loop Stats ---\n");
    RUN(test_game_loop_stats_struct);
    RUN(test_game_loop_null_stats);
    RUN(test_renderer_batch_culling);

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("FAILURES: %d\n", tests_failed);
    }

    return tests_failed > 0 ? 1 : 0;
}
