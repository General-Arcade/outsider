/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#include "effects/effekseer_backend.h"
#include "bindings/bind_effekseer.h"
#include "bindings/bind_io.h"
#include "io/file_io.h"
#include "engine/js_engine.h"

#include <quickjs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* Test fixture: dummy .efkefc file */

static const char *TEST_EFFECTS_DIR = "tests/fixtures/effects";
static const char *TEST_EFFECT_PATH = "tests/fixtures/effects/test_effect.efkefc";

static void create_test_fixtures(void)
{
    file_io_mkdir(TEST_EFFECTS_DIR);

    /* Real Effekseer would reject this file; the stub backend only tracks paths. */
    const char dummy_data[] = "EFKEFC_DUMMY_DATA_FOR_TESTING";
    file_io_write_text(TEST_EFFECT_PATH, dummy_data);
}

static void cleanup_test_fixtures(void)
{
    remove(TEST_EFFECT_PATH);
}

/* C-level backend tests */

TEST(test_init_shutdown)
{
    ASSERT(!effekseer_is_initialized());
    ASSERT(effekseer_init(816, 624, 8000));
    ASSERT(effekseer_is_initialized());

    /* Double init should succeed. */
    ASSERT(effekseer_init(816, 624, 8000));

    effekseer_shutdown();
    ASSERT(!effekseer_is_initialized());

    /* Double shutdown is safe. */
    effekseer_shutdown();
}

TEST(test_load_release)
{
    effekseer_init(816, 624, 8000);

    EffectHandle h = effekseer_load(TEST_EFFECT_PATH, 1.0f);
    ASSERT(h != EFFECT_HANDLE_INVALID);
    ASSERT(effekseer_is_loaded(h));
    ASSERT_EQ_INT(effekseer_get_loaded_count(), 1);

    /* Load a second effect. */
    EffectHandle h2 = effekseer_load(TEST_EFFECT_PATH, 2.0f);
    ASSERT(h2 != EFFECT_HANDLE_INVALID);
    ASSERT(h2 != h);
    ASSERT_EQ_INT(effekseer_get_loaded_count(), 2);

    /* Release first. */
    effekseer_release(h);
    ASSERT(!effekseer_is_loaded(h));
    ASSERT_EQ_INT(effekseer_get_loaded_count(), 1);

    /* Release second. */
    effekseer_release(h2);
    ASSERT_EQ_INT(effekseer_get_loaded_count(), 0);

    /* Release invalid handle is safe. */
    effekseer_release(EFFECT_HANDLE_INVALID);
    effekseer_release(999);

    effekseer_shutdown();
}

TEST(test_play_stop)
{
    effekseer_init(816, 624, 8000);

    EffectHandle effect = effekseer_load(TEST_EFFECT_PATH, 1.0f);
    ASSERT(effect != EFFECT_HANDLE_INVALID);

    EffectInstanceHandle inst = effekseer_play(effect, 0.0f, 0.0f, 0.0f);
    ASSERT(inst != EFFECT_INSTANCE_INVALID);
    ASSERT(effekseer_exists(inst));
    ASSERT_EQ_INT(effekseer_get_playing_count(), 1);

    /* Play a second instance. */
    EffectInstanceHandle inst2 = effekseer_play(effect, 10.0f, 20.0f, 0.0f);
    ASSERT(inst2 != EFFECT_INSTANCE_INVALID);
    ASSERT_EQ_INT(effekseer_get_playing_count(), 2);

    /* Stop first instance. */
    effekseer_stop(inst);
    ASSERT(!effekseer_exists(inst));
    ASSERT_EQ_INT(effekseer_get_playing_count(), 1);

    /* Stop all. */
    effekseer_stop_all();
    ASSERT_EQ_INT(effekseer_get_playing_count(), 0);

    effekseer_shutdown();
}

TEST(test_position_rotation_scale_speed)
{
    effekseer_init(816, 624, 8000);

    EffectHandle effect = effekseer_load(TEST_EFFECT_PATH, 1.0f);
    EffectInstanceHandle inst = effekseer_play(effect, 0.0f, 0.0f, 0.0f);
    ASSERT(inst != EFFECT_INSTANCE_INVALID);

    /* These should not crash on valid instances. */
    effekseer_set_position(inst, 100.0f, 200.0f, 0.0f);
    effekseer_set_rotation(inst, 0.1f, 0.2f, 0.3f);
    effekseer_set_scale(inst, 2.0f, 2.0f, 2.0f);
    effekseer_set_speed(inst, 1.5f);

    /* Should not crash on invalid instances. */
    effekseer_set_position(EFFECT_INSTANCE_INVALID, 0, 0, 0);
    effekseer_set_rotation(EFFECT_INSTANCE_INVALID, 0, 0, 0);
    effekseer_set_scale(EFFECT_INSTANCE_INVALID, 1, 1, 1);
    effekseer_set_speed(EFFECT_INSTANCE_INVALID, 1.0f);

    effekseer_shutdown();
}

TEST(test_matrices)
{
    effekseer_init(816, 624, 8000);

    /* Set projection and camera matrices — should not crash. */
    float proj[16] = {
        1, 0, 0, 0,
        0, -1, 0, 0,
        0, 0, 1, -0.5f,
        0, 0, 0, 1
    };
    float camera[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, -10, 1
    };
    effekseer_set_projection_matrix(proj);
    effekseer_set_camera_matrix(camera);

    /* NULL matrix should be safe. */
    effekseer_set_projection_matrix(NULL);
    effekseer_set_camera_matrix(NULL);

    effekseer_shutdown();
}

TEST(test_update_and_expiry)
{
    effekseer_init(816, 624, 8000);

    EffectHandle effect = effekseer_load(TEST_EFFECT_PATH, 1.0f);
    EffectInstanceHandle inst = effekseer_play(effect, 0.0f, 0.0f, 0.0f);
    ASSERT(effekseer_exists(inst));

    /* Update a few frames — instance should still exist. */
    for (int i = 0; i < 10; i++) {
        effekseer_update(1.0f);
    }
    ASSERT(effekseer_exists(inst));

    /* In stub mode, instances expire after 300 frames. Update past that. */
    for (int i = 0; i < 300; i++) {
        effekseer_update(1.0f);
    }
    ASSERT(!effekseer_exists(inst));
    ASSERT_EQ_INT(effekseer_get_playing_count(), 0);

    effekseer_shutdown();
}

TEST(test_draw_cycle)
{
    effekseer_init(816, 624, 8000);

    EffectHandle effect = effekseer_load(TEST_EFFECT_PATH, 1.0f);
    EffectInstanceHandle inst = effekseer_play(effect, 0.0f, 0.0f, 0.0f);

    /* Full draw cycle should not crash. */
    effekseer_begin_draw();
    effekseer_draw_handle(inst);
    effekseer_end_draw();

    /* Draw with invalid handle should be safe. */
    effekseer_begin_draw();
    effekseer_draw_handle(EFFECT_INSTANCE_INVALID);
    effekseer_end_draw();

    effekseer_shutdown();
}

TEST(test_restoration_and_background)
{
    effekseer_init(816, 624, 8000);

    effekseer_set_restoration_of_states_flag(false);
    effekseer_set_restoration_of_states_flag(true);
    effekseer_set_background(123);
    effekseer_reset_background();

    effekseer_shutdown();
}

/* JS-level binding tests */

static JSEngine *js = NULL;

static void init_js(void)
{
    js = js_engine_init();
    bind_io_register(js_engine_get_context(js));
    bind_effekseer_register(js_engine_get_context(js));
}

static void shutdown_js(void)
{
    js_engine_shutdown(js);
    js = NULL;
}

static bool eval_js(const char *code)
{
    return js_engine_eval(js, code, "<test>");
}

/* Caller frees the result. */
static char *eval_js_str(const char *code)
{
    return js_engine_eval_string(js, code, "<test>");
}

TEST(test_js_native_binding_init)
{
    init_js();

    char *r = eval_js_str("typeof __native_effekseer");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "object") == 0);
    js_engine_free_string(r);

    r = eval_js_str("String(__native_effekseer.init(816, 624, 8000))");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "true") == 0);
    js_engine_free_string(r);

    r = eval_js_str("String(__native_effekseer.isInitialized())");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "true") == 0);
    js_engine_free_string(r);

    eval_js("__native_effekseer.shutdown()");

    shutdown_js();
}

TEST(test_js_native_load_play_stop)
{
    init_js();
    eval_js("__native_effekseer.init(816, 624, 8000)");

    char load_script[512];
    snprintf(load_script, sizeof(load_script),
             "var h = __native_effekseer.load('%s', 1.0); String(h)",
             TEST_EFFECT_PATH);
    char *r = eval_js_str(load_script);
    ASSERT(r != NULL);
    int handle = atoi(r);
    ASSERT(handle > 0);
    js_engine_free_string(r);

    r = eval_js_str("var inst = __native_effekseer.play(h, 0, 0, 0); String(inst)");
    ASSERT(r != NULL);
    int inst = atoi(r);
    ASSERT(inst >= 0);
    js_engine_free_string(r);

    r = eval_js_str("String(__native_effekseer.exists(inst))");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "true") == 0);
    js_engine_free_string(r);

    eval_js("__native_effekseer.stop(inst)");
    r = eval_js_str("String(__native_effekseer.exists(inst))");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "false") == 0);
    js_engine_free_string(r);

    eval_js("__native_effekseer.shutdown()");
    shutdown_js();
}

TEST(test_js_effekseer_shim)
{
    init_js();

    /* dom_shim provides setTimeout and globalThis.window for the effekseer shim. */
    ASSERT(js_engine_eval_file(js, "src/shims/dom_shim.js"));
    js_engine_execute_pending_jobs(js);

    ASSERT(js_engine_eval_file(js, "src/shims/effekseer_shim.js"));
    js_engine_execute_pending_jobs(js);

    char *r = eval_js_str("typeof effekseer");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "object") == 0);
    js_engine_free_string(r);

    /* initRuntime should call onLoad (deferred via setTimeout). */
    eval_js("var runtimeLoaded = false;"
            "effekseer.initRuntime('test.wasm', function() { runtimeLoaded = true; }, null);");
    js_engine_execute_pending_jobs(js);

    /* Flush timers so the deferred onLoad callback runs. */
    eval_js("if (typeof __dom_flushTimers === 'function') __dom_flushTimers();");
    js_engine_execute_pending_jobs(js);

    r = eval_js_str("String(runtimeLoaded)");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "true") == 0);
    js_engine_free_string(r);

    r = eval_js_str("var ctx = effekseer.createContext(); typeof ctx");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "object") == 0);
    js_engine_free_string(r);

    /* Init context with a stub GL object. */
    eval_js("ctx.init({ canvas: { width: 816, height: 624 } })");

    eval_js("ctx.setRestorationOfStatesFlag(false)");

    char script[1024];
    snprintf(script, sizeof(script),
             "var effectLoaded = false;"
             "var eff = ctx.loadEffect('%s', 1.0, function() { effectLoaded = true; }, null);",
             TEST_EFFECT_PATH);
    eval_js(script);
    js_engine_execute_pending_jobs(js);

    /* Flush timers for the deferred onload callback. */
    eval_js("if (typeof __dom_flushTimers === 'function') __dom_flushTimers();");
    js_engine_execute_pending_jobs(js);

    r = eval_js_str("String(eff.isLoaded)");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "true") == 0);
    js_engine_free_string(r);

    r = eval_js_str("var handle = ctx.play(eff, 0, 0, 0); handle !== null ? 'ok' : 'null'");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "ok") == 0);
    js_engine_free_string(r);

    r = eval_js_str("String(handle.exists)");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "true") == 0);
    js_engine_free_string(r);

    eval_js("handle.setLocation(100, 200, 0);"
            "handle.setRotation(0.1, 0.2, 0.3);"
            "handle.setScale(2, 2, 2);"
            "handle.setSpeed(1.5);");

    eval_js("ctx.setProjectionMatrix([1,0,0,0, 0,-1,0,0, 0,0,1,-0.5, 0,0,0,1]);"
            "ctx.setCameraMatrix([1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,-10,1]);");

    eval_js("ctx.beginDraw(); ctx.drawHandle(handle); ctx.endDraw();");

    eval_js("ctx.update(1.0)");

    eval_js("handle.stop()");
    r = eval_js_str("String(handle.exists)");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "false") == 0);
    js_engine_free_string(r);

    eval_js("ctx.releaseEffect(eff)");

    eval_js("ctx.stopAll()");

    eval_js("__native_effekseer.shutdown()");
    shutdown_js();
}

TEST(test_js_effekseer_manager_compat)
{
    /* Exercises the shim through the EffectManager / Sprite_Animation call patterns. */
    init_js();

    ASSERT(js_engine_eval_file(js, "src/shims/dom_shim.js"));
    js_engine_execute_pending_jobs(js);
    ASSERT(js_engine_eval_file(js, "src/shims/effekseer_shim.js"));
    js_engine_execute_pending_jobs(js);

    /* Simulate Graphics._createEffekseerContext. */
    eval_js(
        "var Graphics = { _effekseer: null, _app: { renderer: { gl: { canvas: { width: 816, height: 624 } } } } };"
        "Graphics._effekseer = effekseer.createContext();"
        "Graphics._effekseer.init(Graphics._app.renderer.gl);"
        "Graphics._effekseer.setRestorationOfStatesFlag(false);"
        "Object.defineProperty(Graphics, 'effekseer', { get: function() { return this._effekseer; } });"
    );

    /* Simulate EffectManager.load pattern. */
    char script[1024];
    snprintf(script, sizeof(script),
             "var cache = {};"
             "var url = '%s';"
             "var onLoad = function() {};"
             "var onError = function(msg, url) {};"
             "var effect = Graphics.effekseer.loadEffect(url, 1, onLoad, onError);"
             "cache[url] = effect;",
             TEST_EFFECT_PATH);
    eval_js(script);
    js_engine_execute_pending_jobs(js);

    eval_js("if (typeof __dom_flushTimers === 'function') __dom_flushTimers();");
    js_engine_execute_pending_jobs(js);

    /* EffectManager.isReady pattern. */
    char *r = eval_js_str(
        "var ready = true;"
        "for (var k in cache) { if (!cache[k].isLoaded) ready = false; }"
        "String(ready)"
    );
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "true") == 0);
    js_engine_free_string(r);

    /* Sprite_Animation.play pattern. */
    r = eval_js_str(
        "var h = Graphics.effekseer.play(cache[url]);"
        "h !== null ? 'ok' : 'null'"
    );
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "ok") == 0);
    js_engine_free_string(r);

    /* Sprite_Animation._render pattern. */
    eval_js(
        "Graphics.effekseer.setProjectionMatrix([1,0,0,0, 0,-1,0,0, 0,0,1,-0.5, 0,0,0,1]);"
        "Graphics.effekseer.setCameraMatrix([1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,-10,1]);"
        "Graphics.effekseer.beginDraw();"
        "Graphics.effekseer.drawHandle(h);"
        "Graphics.effekseer.endDraw();"
    );

    /* SceneManager.updateEffekseer pattern. */
    eval_js("Graphics.effekseer.update()");

    /* EffectManager.clear pattern. */
    eval_js(
        "for (var k in cache) { Graphics.effekseer.releaseEffect(cache[k]); }"
        "cache = {};"
    );

    /* Graphics.effekseer.stopAll pattern. */
    eval_js("Graphics.effekseer.stopAll()");

    eval_js("__native_effekseer.shutdown()");
    shutdown_js();
}

/* Main */

int main(void)
{
    printf("=== Effekseer Backend + Binding Tests ===\n\n");

    create_test_fixtures();

    printf("--- C-level backend tests ---\n");
    RUN(test_init_shutdown);
    RUN(test_load_release);
    RUN(test_play_stop);
    RUN(test_position_rotation_scale_speed);
    RUN(test_matrices);
    RUN(test_update_and_expiry);
    RUN(test_draw_cycle);
    RUN(test_restoration_and_background);

    printf("\n--- JS binding tests ---\n");
    RUN(test_js_native_binding_init);
    RUN(test_js_native_load_play_stop);
    RUN(test_js_effekseer_shim);
    RUN(test_js_effekseer_manager_compat);

    cleanup_test_fixtures();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
