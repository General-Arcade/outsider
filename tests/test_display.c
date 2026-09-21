/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

/* test_display.c — Tests for fullscreen, window resize, and display scaling.
   Covers both C-level platform functions and JS-level shims. */

#include "platform/platform.h"
#include "engine/js_engine.h"
#include "bindings/bind_platform.h"
#include "bindings/bind_io.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  TEST %s ... ", #name); \
    } while (0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("PASS\n"); \
    } while (0)

#define FAIL(msg) \
    do { \
        printf("FAIL: %s\n", msg); \
    } while (0)

/* C-level platform tests */

static Platform *g_platform = NULL;

static void test_fullscreen_toggle(void)
{
    TEST(fullscreen_initially_off);
    if (platform_is_fullscreen(g_platform)) {
        FAIL("expected not fullscreen initially");
    } else {
        PASS();
    }

    TEST(set_fullscreen_on);
    platform_set_fullscreen(g_platform, true);
    /* Give SDL a moment to process the change. Wayland only completes it
       once the window commits another frame, as a running game always does. */
    platform_swap_buffers(g_platform);
    SDL_Delay(50);
    SDL_PumpEvents();
    if (!platform_is_fullscreen(g_platform)) {
        FAIL("expected fullscreen after setFullscreen(true)");
    } else {
        PASS();
    }

    TEST(set_fullscreen_off);
    platform_set_fullscreen(g_platform, false);
    platform_swap_buffers(g_platform);
    SDL_Delay(50);
    SDL_PumpEvents();
    if (platform_is_fullscreen(g_platform)) {
        FAIL("expected windowed after setFullscreen(false)");
    } else {
        PASS();
    }
}

static void test_display_info(void)
{
    TEST(display_size_positive);
    int dw = 0, dh = 0;
    platform_get_display_size(g_platform, &dw, &dh);
    if (dw <= 0 || dh <= 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "display size %dx%d is not positive", dw, dh);
        FAIL(buf);
    } else {
        printf("PASS (%dx%d)\n", dw, dh);
        tests_passed++;
    }

    TEST(display_scale_positive);
    float scale = platform_get_display_scale(g_platform);
    if (scale < 1.0f) {
        char buf[64];
        snprintf(buf, sizeof(buf), "display scale %f < 1.0", (double)scale);
        FAIL(buf);
    } else {
        printf("PASS (%.2f)\n", (double)scale);
        tests_passed++;
    }
}

static void test_resize_detection(void)
{
    TEST(no_pending_resize_initially);
    int rw, rh;
    if (platform_check_resize(g_platform, &rw, &rh)) {
        FAIL("unexpected pending resize");
    } else {
        PASS();
    }

    TEST(resize_after_set_window_size);
    int w0, h0;
    platform_get_window_size(g_platform, &w0, &h0);

    /* Push a fake resize event to test the detection mechanism. */
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_EVENT_WINDOW_RESIZED;
    ev.window.data1 = 1024;
    ev.window.data2 = 768;
    SDL_PushEvent(&ev);

    /* Poll to process the event. */
    platform_poll_events(g_platform);

    if (!platform_check_resize(g_platform, &rw, &rh)) {
        FAIL("expected pending resize after window event");
    } else if (rw != 1024 || rh != 768) {
        char buf[128];
        snprintf(buf, sizeof(buf), "expected 1024x768, got %dx%d", rw, rh);
        FAIL(buf);
    } else {
        PASS();
    }

    TEST(resize_consumed_after_check);
    if (platform_check_resize(g_platform, &rw, &rh)) {
        FAIL("resize should be consumed after check");
    } else {
        PASS();
    }
}

static void test_null_safety(void)
{
    TEST(fullscreen_null_platform);
    platform_set_fullscreen(NULL, true);
    if (platform_is_fullscreen(NULL)) {
        FAIL("NULL platform should not be fullscreen");
    } else {
        PASS();
    }

    TEST(display_size_null_platform);
    int w = 0, h = 0;
    platform_get_display_size(NULL, &w, &h);
    /* Should return fallback values without crashing. */
    if (w <= 0 || h <= 0) {
        FAIL("expected positive fallback dimensions");
    } else {
        PASS();
    }

    TEST(check_resize_null_platform);
    if (platform_check_resize(NULL, &w, &h)) {
        FAIL("NULL platform should not have pending resize");
    } else {
        PASS();
    }
}

/* JS-level shim tests */

static JSEngine *g_js = NULL;

static bool eval_bool(const char *script)
{
    char *result = js_engine_eval_string(g_js, script, "<test>");
    if (!result) return false;
    bool val = (strcmp(result, "true") == 0);
    js_engine_free_string(result);
    return val;
}

static int eval_int(const char *script)
{
    char *result = js_engine_eval_string(g_js, script, "<test>");
    if (!result) return -1;
    int val = atoi(result);
    js_engine_free_string(result);
    return val;
}

static char *eval_str(const char *script)
{
    return js_engine_eval_string(g_js, script, "<test>");
}

static void test_js_fullscreen_api(void)
{
    TEST(js_fullscreen_element_initially_null);
    if (!eval_bool("document.fullscreenElement === null")) {
        FAIL("fullscreenElement should be null initially");
    } else {
        PASS();
    }

    TEST(js_requestFullscreen_sets_element);
    js_engine_eval(g_js,
        "var _testCanvas = document.createElement('canvas');\n"
        "_testCanvas.requestFullscreen();\n",
        "<test>");
    if (!eval_bool("document.fullscreenElement === _testCanvas")) {
        FAIL("fullscreenElement should be the canvas after requestFullscreen");
    } else {
        PASS();
    }

    TEST(js_exitFullscreen_clears_element);
    js_engine_eval(g_js, "document.exitFullscreen();", "<test>");
    if (!eval_bool("document.fullscreenElement === null")) {
        FAIL("fullscreenElement should be null after exitFullscreen");
    } else {
        PASS();
    }

    TEST(js_webkitRequestFullscreen_exists);
    if (!eval_bool("typeof document.createElement('canvas').webkitRequestFullscreen === 'function'")) {
        FAIL("webkitRequestFullscreen should be a function");
    } else {
        PASS();
    }

    TEST(js_webkitExitFullscreen_exists);
    if (!eval_bool("typeof document.webkitExitFullscreen === 'function'")) {
        FAIL("webkitExitFullscreen should be a function");
    } else {
        PASS();
    }
}

static void test_js_fullscreen_event(void)
{
    TEST(js_fullscreenchange_event_fires);
    js_engine_eval(g_js,
        "var _fsChangeCount = 0;\n"
        "document.addEventListener('fullscreenchange', function() {\n"
        "    _fsChangeCount++;\n"
        "});\n"
        "var _fsCanvas = document.createElement('canvas');\n"
        "_fsCanvas.requestFullscreen();\n",
        "<test>");
    int count = eval_int("String(_fsChangeCount)");
    if (count < 1) {
        FAIL("fullscreenchange event should have fired");
    } else {
        PASS();
    }

    TEST(js_fullscreenchange_on_exit);
    js_engine_eval(g_js, "document.exitFullscreen();", "<test>");
    count = eval_int("String(_fsChangeCount)");
    if (count < 2) {
        FAIL("fullscreenchange event should fire on exit too");
    } else {
        PASS();
    }
}

static void test_js_resize_event(void)
{
    TEST(js_resize_event_fires);
    js_engine_eval(g_js,
        "var _resizeCount = 0;\n"
        "var _lastResizeW = 0;\n"
        "var _lastResizeH = 0;\n"
        "window.addEventListener('resize', function() {\n"
        "    _resizeCount++;\n"
        "    _lastResizeW = window.innerWidth;\n"
        "    _lastResizeH = window.innerHeight;\n"
        "});\n"
        "__dom_setWindowSize(1280, 720);\n",
        "<test>");
    int count = eval_int("String(_resizeCount)");
    if (count < 1) {
        FAIL("resize event should have fired");
    } else {
        PASS();
    }

    TEST(js_resize_updates_inner_dimensions);
    int w = eval_int("String(window.innerWidth)");
    int h = eval_int("String(window.innerHeight)");
    if (w != 1280 || h != 720) {
        char buf[128];
        snprintf(buf, sizeof(buf), "expected 1280x720, got %dx%d", w, h);
        FAIL(buf);
    } else {
        PASS();
    }

    TEST(js_resize_updates_outer_dimensions);
    w = eval_int("String(window.outerWidth)");
    h = eval_int("String(window.outerHeight)");
    if (w != 1280 || h != 720) {
        char buf[128];
        snprintf(buf, sizeof(buf), "expected 1280x720, got %dx%d", w, h);
        FAIL(buf);
    } else {
        PASS();
    }

    TEST(js_resize_updates_documentElement);
    if (!eval_bool("document.documentElement.style.width === '1280px'")) {
        FAIL("documentElement width should be '1280px'");
    } else {
        PASS();
    }
}

static void test_js_display_info(void)
{
    TEST(js_screen_width_positive);
    int sw = eval_int("String(window.screen.width)");
    if (sw <= 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "screen.width = %d, expected > 0", sw);
        FAIL(buf);
    } else {
        printf("PASS (%d)\n", sw);
        tests_passed++;
    }

    TEST(js_screen_height_positive);
    int sh = eval_int("String(window.screen.height)");
    if (sh <= 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "screen.height = %d, expected > 0", sh);
        FAIL(buf);
    } else {
        printf("PASS (%d)\n", sh);
        tests_passed++;
    }

    TEST(js_device_pixel_ratio_positive);
    char *dpr_str = eval_str("String(window.devicePixelRatio)");
    double dpr = dpr_str ? atof(dpr_str) : 0.0;
    if (dpr_str) js_engine_free_string(dpr_str);
    if (dpr < 1.0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "devicePixelRatio = %.2f, expected >= 1.0", dpr);
        FAIL(buf);
    } else {
        printf("PASS (%.2f)\n", dpr);
        tests_passed++;
    }
}

static void test_js_native_platform(void)
{
    TEST(js_native_platform_exists);
    if (!eval_bool("typeof __native_platform === 'object'")) {
        FAIL("__native_platform should be an object");
    } else {
        PASS();
    }

    TEST(js_native_platform_setFullscreen);
    if (!eval_bool("typeof __native_platform.setFullscreen === 'function'")) {
        FAIL("setFullscreen should be a function");
    } else {
        PASS();
    }

    TEST(js_native_platform_isFullscreen);
    if (!eval_bool("typeof __native_platform.isFullscreen === 'function'")) {
        FAIL("isFullscreen should be a function");
    } else {
        PASS();
    }

    TEST(js_native_platform_getDisplaySize);
    if (!eval_bool("typeof __native_platform.getDisplaySize === 'function'")) {
        FAIL("getDisplaySize should be a function");
    } else {
        PASS();
    }

    TEST(js_native_platform_getDisplayScale);
    if (!eval_bool("typeof __native_platform.getDisplayScale === 'function'")) {
        FAIL("getDisplayScale should be a function");
    } else {
        PASS();
    }

    TEST(js_native_platform_getWindowSize);
    if (!eval_bool("typeof __native_platform.getWindowSize === 'function'")) {
        FAIL("getWindowSize should be a function");
    } else {
        PASS();
    }

    TEST(js_getDisplaySize_returns_object);
    if (!eval_bool("(function() { var s = __native_platform.getDisplaySize(); return s && s.width > 0 && s.height > 0; })()")) {
        FAIL("getDisplaySize should return {width, height} with positive values");
    } else {
        PASS();
    }

    TEST(js_native_platform_setWindowTitle);
    if (!eval_bool("typeof __native_platform.setWindowTitle === 'function'")) {
        FAIL("setWindowTitle should be a function");
    } else {
        PASS();
    }

    TEST(js_setWindowTitle_accepts_string);
    if (!eval_bool("(function() { __native_platform.setWindowTitle('Test Game'); return true; })()")) {
        FAIL("setWindowTitle should accept a string");
    } else {
        PASS();
    }

    TEST(js_getWindowSize_returns_object);
    if (!eval_bool("(function() { var s = __native_platform.getWindowSize(); return s && s.width > 0 && s.height > 0; })()")) {
        FAIL("getWindowSize should return {width, height} with positive values");
    } else {
        PASS();
    }
}

static void test_js_scaling_calculation(void)
{
    /* Mirrors the letterbox scaling in Graphics._updateRealScale. */
    TEST(js_letterbox_scaling_wider_window);
    /* 816x624 game in a 1920x1080 window: fit-by-height wins (1080/624 = 1.7307). */
    js_engine_eval(g_js,
        "var _testScale = (function() {\n"
        "    var gameW = 816, gameH = 624;\n"
        "    var winW = 1920, winH = 1080;\n"
        "    var scaleX = winW / gameW;\n"
        "    var scaleY = winH / gameH;\n"
        "    return Math.min(scaleX, scaleY);\n"
        "})();\n",
        "<test>");
    char *s = eval_str("String(Math.round(_testScale * 10000))");
    int scaled = s ? atoi(s) : 0;
    if (s) js_engine_free_string(s);
    if (scaled != 17307 && scaled != 17308) {
        char buf[128];
        snprintf(buf, sizeof(buf), "expected ~17307, got %d", scaled);
        FAIL(buf);
    } else {
        PASS();
    }

    TEST(js_letterbox_scaling_taller_window);
    /* 816x1200 window: fit-by-width wins at scale 1.0. */
    js_engine_eval(g_js,
        "var _testScale2 = (function() {\n"
        "    var gameW = 816, gameH = 624;\n"
        "    var winW = 816, winH = 1200;\n"
        "    var scaleX = winW / gameW;\n"
        "    var scaleY = winH / gameH;\n"
        "    return Math.min(scaleX, scaleY);\n"
        "})();\n",
        "<test>");
    s = eval_str("String(Math.round(_testScale2 * 10000))");
    scaled = s ? atoi(s) : 0;
    if (s) js_engine_free_string(s);
    if (scaled != 10000) {
        char buf[128];
        snprintf(buf, sizeof(buf), "expected 10000, got %d", scaled);
        FAIL(buf);
    } else {
        PASS();
    }
}

/* Main */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    printf("=== Display Tests ===\n");

    /* Initialize platform for C-level tests. */
    g_platform = platform_init("Display Test", 816, 624);
    if (!g_platform) {
        fprintf(stderr, "SKIP: Could not initialize platform (no display?)\n");
        return 0;
    }

    /* Wayland maps a window only once it has content, so put one frame on
       screen before asking anything of the window. */
    platform_swap_buffers(g_platform);
    SDL_PumpEvents();

    printf("\n--- C-level platform tests ---\n");
    test_fullscreen_toggle();
    test_display_info();
    test_resize_detection();
    test_null_safety();

    /* Initialize JS engine for JS-level tests. */
    g_js = js_engine_init();
    if (!g_js) {
        fprintf(stderr, "FAIL: Could not initialize JS engine\n");
        platform_shutdown(g_platform);
        return 1;
    }

    /* Register bindings. */
    bind_io_register(js_engine_get_context(g_js));
    bind_platform_register(js_engine_get_context(g_js), g_platform);

    /* Load DOM shim. */
    if (!js_engine_eval_file(g_js, "src/shims/dom_shim.js")) {
        fprintf(stderr, "FAIL: Could not load dom_shim.js\n");
        js_engine_shutdown(g_js);
        platform_shutdown(g_platform);
        return 1;
    }

    /* Initialize display info. */
    js_engine_eval(g_js, "__dom_initDisplayInfo && __dom_initDisplayInfo();",
                   "<display-init>");

    printf("\n--- JS-level shim tests ---\n");
    test_js_native_platform();
    test_js_fullscreen_api();
    test_js_fullscreen_event();
    test_js_resize_event();
    test_js_display_info();
    test_js_scaling_calculation();

    /* Cleanup. */
    js_engine_shutdown(g_js);
    platform_shutdown(g_platform);

    printf("\nResults: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
