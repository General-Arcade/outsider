/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#include "engine/js_engine.h"
#include "bindings/bind_io.h"
#include "io/file_io.h"

#include <quickjs.h>
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

/* Helper: read a shim file into a string */

static char *read_shim(const char *relpath)
{
    size_t size = 0;
    char *data = file_io_read_text(relpath, &size);
    if (!data) {
        /* Fall back to running from the build directory. */
        char altpath[512];
        snprintf(altpath, sizeof(altpath), "../%s", relpath);
        data = file_io_read_text(altpath, &size);
    }
    return data;
}

/* Helper: create engine with DOM + navigator shims loaded */

typedef struct {
    char level[64][16];
    char message[64][4096];
    int count;
} ConsoleCapture;

static void console_capture_cb(const char *level, const char *message, void *userdata)
{
    ConsoleCapture *cap = userdata;
    if (cap->count < 64) {
        strncpy(cap->level[cap->count], level, 15);
        cap->level[cap->count][15] = '\0';
        strncpy(cap->message[cap->count], message, 4095);
        cap->message[cap->count][4095] = '\0';
        cap->count++;
    }
}

typedef struct {
    JSEngine *engine;
    ConsoleCapture cap;
} TestCtx;

static TestCtx *create_test_ctx(void)
{
    TestCtx *tc = calloc(1, sizeof(TestCtx));
    tc->engine = js_engine_init();
    if (!tc->engine) {
        free(tc);
        return NULL;
    }

    js_engine_set_console_callback(tc->engine, console_capture_cb, &tc->cap);
    bind_io_register(js_engine_get_context(tc->engine));

    char *dom_shim = read_shim("src/shims/dom_shim.js");
    if (!dom_shim) {
        fprintf(stderr, "Failed to read dom_shim.js\n");
        js_engine_shutdown(tc->engine);
        free(tc);
        return NULL;
    }
    bool ok = js_engine_eval(tc->engine, dom_shim, "dom_shim.js");
    free(dom_shim);
    if (!ok) {
        fprintf(stderr, "Failed to eval dom_shim.js\n");
        js_engine_shutdown(tc->engine);
        free(tc);
        return NULL;
    }

    char *nav_shim = read_shim("src/shims/navigator_shim.js");
    if (!nav_shim) {
        fprintf(stderr, "Failed to read navigator_shim.js\n");
        js_engine_shutdown(tc->engine);
        free(tc);
        return NULL;
    }
    ok = js_engine_eval(tc->engine, nav_shim, "navigator_shim.js");
    free(nav_shim);
    if (!ok) {
        fprintf(stderr, "Failed to eval navigator_shim.js\n");
        js_engine_shutdown(tc->engine);
        free(tc);
        return NULL;
    }

    return tc;
}

static void destroy_test_ctx(TestCtx *tc)
{
    if (!tc) return;
    js_engine_shutdown(tc->engine);
    free(tc);
}

/* Tests: document.createElement */

static void test_create_element(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(dom_setup); FAIL("engine setup"); return; }

    TEST(createElement_canvas);
    char *r = js_engine_eval_string(tc->engine,
        "var c = document.createElement('canvas');\n"
        "c.tagName + '|' + (c instanceof HTMLCanvasElement)", "test");
    if (r && strcmp(r, "CANVAS|true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(createElement_img);
    r = js_engine_eval_string(tc->engine,
        "var img = document.createElement('img');\n"
        "img.tagName + '|' + (img instanceof HTMLImageElement)", "test");
    if (r && strcmp(r, "IMG|true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(createElement_div);
    r = js_engine_eval_string(tc->engine,
        "var d = document.createElement('div');\n"
        "d.tagName", "test");
    if (r && strcmp(r, "DIV") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(createElement_video);
    r = js_engine_eval_string(tc->engine,
        "var v = document.createElement('video');\n"
        "v.tagName + '|' + (v instanceof HTMLVideoElement)", "test");
    if (r && strcmp(r, "VIDEO|true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(Image_constructor);
    r = js_engine_eval_string(tc->engine,
        "var img = new Image();\n"
        "img.tagName + '|' + (img instanceof HTMLImageElement)", "test");
    if (r && strcmp(r, "IMG|true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: HTMLCanvasElement */

static void test_canvas_element(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(dom_setup); FAIL("engine setup"); return; }

    TEST(canvas_default_size);
    char *r = js_engine_eval_string(tc->engine,
        "var c = document.createElement('canvas');\n"
        "c.width + 'x' + c.height", "test");
    if (r && strcmp(r, "300x150") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(canvas_set_size);
    r = js_engine_eval_string(tc->engine,
        "var c = document.createElement('canvas');\n"
        "c.width = 816; c.height = 624;\n"
        "c.width + 'x' + c.height", "test");
    if (r && strcmp(r, "816x624") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(canvas_getContext_2d);
    r = js_engine_eval_string(tc->engine,
        "var c = document.createElement('canvas');\n"
        "var ctx = c.getContext('2d');\n"
        "(ctx !== null) + '|' + (ctx.canvas === c)", "test");
    if (r && strcmp(r, "true|true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(canvas_getContext_2d_same_instance);
    r = js_engine_eval_string(tc->engine,
        "var c = document.createElement('canvas');\n"
        "var ctx1 = c.getContext('2d');\n"
        "var ctx2 = c.getContext('2d');\n"
        "String(ctx1 === ctx2)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(canvas_getContext_webgl);
    r = js_engine_eval_string(tc->engine,
        "var c = document.createElement('canvas');\n"
        "var gl = c.getContext('webgl');\n"
        "String(gl !== null)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(canvas_getContext_null);
    r = js_engine_eval_string(tc->engine,
        "var c = document.createElement('canvas');\n"
        "String(c.getContext('unknown') === null)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(canvas_style);
    r = js_engine_eval_string(tc->engine,
        "var c = document.createElement('canvas');\n"
        "c.style.cursor = 'pointer';\n"
        "c.style.cursor", "test");
    if (r && strcmp(r, "pointer") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(canvas_id);
    r = js_engine_eval_string(tc->engine,
        "var c = document.createElement('canvas');\n"
        "c.id = 'gameCanvas';\n"
        "c.id", "test");
    if (r && strcmp(r, "gameCanvas") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: Canvas2D stub context methods */

static void test_canvas2d_context(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(dom_setup); FAIL("engine setup"); return; }

    TEST(ctx2d_default_properties);
    char *r = js_engine_eval_string(tc->engine,
        "var c = document.createElement('canvas');\n"
        "var ctx = c.getContext('2d');\n"
        "ctx.fillStyle + '|' + ctx.globalAlpha + '|' + ctx.font", "test");
    if (r && strcmp(r, "#000000|1|10px sans-serif") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(ctx2d_save_restore);
    r = js_engine_eval_string(tc->engine,
        "var c = document.createElement('canvas');\n"
        "var ctx = c.getContext('2d');\n"
        "ctx.fillStyle = '#ff0000';\n"
        "ctx.save();\n"
        "ctx.fillStyle = '#00ff00';\n"
        "var before = ctx.fillStyle;\n"
        "ctx.restore();\n"
        "before + '|' + ctx.fillStyle", "test");
    if (r && strcmp(r, "#00ff00|#ff0000") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(ctx2d_measureText);
    r = js_engine_eval_string(tc->engine,
        "var c = document.createElement('canvas');\n"
        "var ctx = c.getContext('2d');\n"
        "var m = ctx.measureText('Hello');\n"
        "String(m.width > 0)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(ctx2d_getImageData);
    r = js_engine_eval_string(tc->engine,
        "var c = document.createElement('canvas');\n"
        "var ctx = c.getContext('2d');\n"
        "var id = ctx.getImageData(0, 0, 10, 10);\n"
        "id.width + '|' + id.height + '|' + id.data.length", "test");
    if (r && strcmp(r, "10|10|400") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(ctx2d_createImageData);
    r = js_engine_eval_string(tc->engine,
        "var c = document.createElement('canvas');\n"
        "var ctx = c.getContext('2d');\n"
        "var id = ctx.createImageData(5, 5);\n"
        "id.width + '|' + id.height + '|' + id.data.length", "test");
    if (r && strcmp(r, "5|5|100") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(ctx2d_methods_callable);
    r = js_engine_eval_string(tc->engine,
        "var c = document.createElement('canvas');\n"
        "var ctx = c.getContext('2d');\n"
        "ctx.fillRect(0,0,10,10);\n"
        "ctx.clearRect(0,0,10,10);\n"
        "ctx.strokeRect(0,0,10,10);\n"
        "ctx.fillText('hello', 0, 0);\n"
        "ctx.strokeText('hello', 0, 0);\n"
        "ctx.beginPath();\n"
        "ctx.moveTo(0,0);\n"
        "ctx.lineTo(10,10);\n"
        "ctx.closePath();\n"
        "ctx.fill();\n"
        "ctx.stroke();\n"
        "ctx.scale(1,1);\n"
        "ctx.rotate(0);\n"
        "ctx.translate(0,0);\n"
        "ctx.setTransform(1,0,0,1,0,0);\n"
        "'ok'", "test");
    if (r && strcmp(r, "ok") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: HTMLImageElement */

static void test_image_element(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(dom_setup); FAIL("engine setup"); return; }

    TEST(image_initial_state);
    char *r = js_engine_eval_string(tc->engine,
        "var img = new Image();\n"
        "img.src + '|' + img.complete + '|' + img.width + '|' + img.height", "test");
    if (r && strcmp(r, "|false|0|0") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(image_data_url_onload);
    js_engine_eval(tc->engine,
        "var imgResult = 'pending';\n"
        "var img = new Image();\n"
        "img.onload = function() { imgResult = 'loaded'; };\n"
        "img.src = 'data:image/png;base64,abc';\n", "test");
    js_engine_execute_pending_jobs(tc->engine);
    r = js_engine_eval_string(tc->engine, "imgResult", "test");
    if (r && strcmp(r, "loaded") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(image_complete_after_load);
    r = js_engine_eval_string(tc->engine,
        "var img = new Image();\n"
        "String(img.complete)", "test");
    if (r && strcmp(r, "false") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(image_crossOrigin);
    r = js_engine_eval_string(tc->engine,
        "var img = new Image();\n"
        "img.crossOrigin = 'anonymous';\n"
        "img.crossOrigin", "test");
    if (r && strcmp(r, "anonymous") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: document event dispatch */

static void test_event_dispatch(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(dom_setup); FAIL("engine setup"); return; }

    TEST(document_addEventListener);
    char *r = js_engine_eval_string(tc->engine,
        "var evtResult = '';\n"
        "document.addEventListener('keydown', function(e) {\n"
        "    evtResult = e.type;\n"
        "});\n"
        "document.dispatchEvent(new Event('keydown'));\n"
        "evtResult", "test");
    if (r && strcmp(r, "keydown") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(document_removeEventListener);
    r = js_engine_eval_string(tc->engine,
        "var evtCount = 0;\n"
        "var handler = function() { evtCount++; };\n"
        "document.addEventListener('test', handler);\n"
        "document.dispatchEvent(new Event('test'));\n"
        "document.removeEventListener('test', handler);\n"
        "document.dispatchEvent(new Event('test'));\n"
        "String(evtCount)", "test");
    if (r && strcmp(r, "1") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(element_addEventListener);
    r = js_engine_eval_string(tc->engine,
        "var el = document.createElement('div');\n"
        "var elEvt = '';\n"
        "el.addEventListener('click', function(e) { elEvt = e.type; });\n"
        "el.dispatchEvent(new Event('click'));\n"
        "elEvt", "test");
    if (r && strcmp(r, "click") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(event_properties);
    r = js_engine_eval_string(tc->engine,
        "var e = new Event('test', { bubbles: true, cancelable: true });\n"
        "e.type + '|' + e.bubbles + '|' + e.cancelable + '|' + e.defaultPrevented", "test");
    if (r && strcmp(r, "test|true|true|false") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(event_preventDefault);
    r = js_engine_eval_string(tc->engine,
        "var e = new Event('test', { cancelable: true });\n"
        "e.preventDefault();\n"
        "String(e.defaultPrevented)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(multiple_listeners_same_event);
    r = js_engine_eval_string(tc->engine,
        "var results = [];\n"
        "var el = document.createElement('div');\n"
        "el.addEventListener('test', function() { results.push('a'); });\n"
        "el.addEventListener('test', function() { results.push('b'); });\n"
        "el.dispatchEvent(new Event('test'));\n"
        "results.join(',')", "test");
    if (r && strcmp(r, "a,b") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(no_duplicate_listeners);
    r = js_engine_eval_string(tc->engine,
        "var count = 0;\n"
        "var el = document.createElement('div');\n"
        "var fn = function() { count++; };\n"
        "el.addEventListener('test', fn);\n"
        "el.addEventListener('test', fn);\n"
        "el.dispatchEvent(new Event('test'));\n"
        "String(count)", "test");
    if (r && strcmp(r, "1") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: Timer functions (setTimeout / setInterval / clear) */

static void test_timers(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(dom_setup); FAIL("engine setup"); return; }

    TEST(setTimeout_returns_id);
    char *r = js_engine_eval_string(tc->engine,
        "var id = setTimeout(function() {}, 0);\n"
        "String(typeof id === 'number' && id > 0)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(setTimeout_fires_on_flush);
    r = js_engine_eval_string(tc->engine,
        "var timerFired = false;\n"
        "setTimeout(function() { timerFired = true; }, 0);\n"
        "__dom_flushTimers();\n"
        "String(timerFired)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(setTimeout_fires_only_once);
    r = js_engine_eval_string(tc->engine,
        "var timerCount = 0;\n"
        "setTimeout(function() { timerCount++; }, 0);\n"
        "__dom_flushTimers();\n"
        "__dom_flushTimers();\n"
        "__dom_flushTimers();\n"
        "String(timerCount)", "test");
    if (r && strcmp(r, "1") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(clearTimeout_prevents_fire);
    r = js_engine_eval_string(tc->engine,
        "var cleared = false;\n"
        "var tid = setTimeout(function() { cleared = true; }, 0);\n"
        "clearTimeout(tid);\n"
        "__dom_flushTimers();\n"
        "String(cleared)", "test");
    if (r && strcmp(r, "false") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(setInterval_fires_multiple);
    r = js_engine_eval_string(tc->engine,
        "var intervalCount = 0;\n"
        "var iid = setInterval(function() { intervalCount++; }, 0);\n"
        "__dom_flushTimers();\n"
        "__dom_flushTimers();\n"
        "__dom_flushTimers();\n"
        "clearInterval(iid);\n"
        "String(intervalCount >= 2)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(clearInterval_stops);
    r = js_engine_eval_string(tc->engine,
        "var icount = 0;\n"
        "var iid2 = setInterval(function() { icount++; }, 0);\n"
        "__dom_flushTimers();\n"
        "clearInterval(iid2);\n"
        "var countAfterClear = icount;\n"
        "__dom_flushTimers();\n"
        "__dom_flushTimers();\n"
        "String(icount === countAfterClear)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(setTimeout_on_window);
    r = js_engine_eval_string(tc->engine,
        "String(window.setTimeout === setTimeout)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: requestAnimationFrame / cancelAnimationFrame */

static void test_raf(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(dom_setup); FAIL("engine setup"); return; }

    TEST(raf_returns_id);
    char *r = js_engine_eval_string(tc->engine,
        "var id = requestAnimationFrame(function() {});\n"
        "String(typeof id === 'number' && id > 0)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(raf_fires_on_flush);
    r = js_engine_eval_string(tc->engine,
        "var rafFired = false;\n"
        "var rafTimestamp = -1;\n"
        "requestAnimationFrame(function(ts) {\n"
        "    rafFired = true;\n"
        "    rafTimestamp = ts;\n"
        "});\n"
        "__dom_flushAnimationFrames(16.67);\n"
        "rafFired + '|' + (rafTimestamp === 16.67)", "test");
    if (r && strcmp(r, "true|true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(raf_fires_only_once_per_schedule);
    r = js_engine_eval_string(tc->engine,
        "var rafCount = 0;\n"
        "requestAnimationFrame(function() { rafCount++; });\n"
        "__dom_flushAnimationFrames(16.67);\n"
        "__dom_flushAnimationFrames(33.34);\n"
        "String(rafCount)", "test");
    if (r && strcmp(r, "1") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(cancelAnimationFrame_prevents_fire);
    r = js_engine_eval_string(tc->engine,
        "var rafCancelled = false;\n"
        "var rid = requestAnimationFrame(function() { rafCancelled = true; });\n"
        "cancelAnimationFrame(rid);\n"
        "__dom_flushAnimationFrames(16.67);\n"
        "String(rafCancelled)", "test");
    if (r && strcmp(r, "false") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(raf_recursive);
    r = js_engine_eval_string(tc->engine,
        "var rafSeq = [];\n"
        "function loop(ts) {\n"
        "    rafSeq.push(Math.round(ts));\n"
        "    if (rafSeq.length < 3) requestAnimationFrame(loop);\n"
        "}\n"
        "requestAnimationFrame(loop);\n"
        "__dom_flushAnimationFrames(100);\n"
        "__dom_flushAnimationFrames(200);\n"
        "__dom_flushAnimationFrames(300);\n"
        "rafSeq.join(',')", "test");
    if (r && strcmp(r, "100,200,300") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(raf_on_window);
    r = js_engine_eval_string(tc->engine,
        "String(window.requestAnimationFrame === requestAnimationFrame)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: window object */

static void test_window(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(dom_setup); FAIL("engine setup"); return; }

    TEST(window_exists);
    char *r = js_engine_eval_string(tc->engine,
        "String(typeof window === 'object' && window !== null)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(window_is_globalThis);
    r = js_engine_eval_string(tc->engine,
        "String(window === globalThis)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(window_dimensions);
    r = js_engine_eval_string(tc->engine,
        "window.innerWidth + 'x' + window.innerHeight", "test");
    if (r && strcmp(r, "816x624") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(window_devicePixelRatio);
    r = js_engine_eval_string(tc->engine,
        "String(window.devicePixelRatio)", "test");
    if (r && strcmp(r, "1") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(window_location);
    r = js_engine_eval_string(tc->engine,
        "window.location.protocol + '|' + (typeof window.location.href === 'string')", "test");
    if (r && strcmp(r, "file:|true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(window_screen);
    r = js_engine_eval_string(tc->engine,
        "window.screen.width + 'x' + window.screen.height + '|' + window.screen.colorDepth", "test");
    if (r && strcmp(r, "816x624|32") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(window_performance_now);
    r = js_engine_eval_string(tc->engine,
        "String(typeof window.performance.now() === 'number')", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(window_matchMedia);
    r = js_engine_eval_string(tc->engine,
        "var mq = window.matchMedia('(min-width: 800px)');\n"
        "mq.media + '|' + (typeof mq.matches === 'boolean')", "test");
    if (r && strcmp(r, "(min-width: 800px)|true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(window_getComputedStyle);
    r = js_engine_eval_string(tc->engine,
        "var el = document.createElement('div');\n"
        "el.style.cursor = 'pointer';\n"
        "var s = window.getComputedStyle(el);\n"
        "s.cursor", "test");
    if (r && strcmp(r, "pointer") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(window_self);
    r = js_engine_eval_string(tc->engine,
        "String(self === window)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: <script> elements inserted at run time */

static void test_inserted_scripts(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(script_setup); FAIL("engine setup"); return; }

    TEST(inline_script_runs_when_inserted);
    char *r = js_engine_eval_string(tc->engine,
        "var s = document.createElement('script');\n"
        "s.innerHTML = 'globalThis.__inlineRan = (globalThis.__inlineRan || 0) + 1;';\n"
        "document.body.appendChild(s);\n"
        "String(globalThis.__inlineRan)", "test");
    if (r && strcmp(r, "1") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(detached_script_does_not_run);
    r = js_engine_eval_string(tc->engine,
        "var d = document.createElement('div');\n"
        "var s2 = document.createElement('script');\n"
        "s2.innerHTML = 'globalThis.__detachedRan = true;';\n"
        "d.appendChild(s2);\n"
        "String(globalThis.__detachedRan === undefined)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(script_src_reads_the_game_file);
    r = js_engine_eval_string(tc->engine,
        "var s3 = document.createElement('script');\n"
        "s3.src = 'tests/fixtures/inserted_script.js';\n"
        "document.body.appendChild(s3);\n"
        "String(globalThis.__fromFile)", "test");
    if (r && strcmp(r, "42") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(script_already_loaded_is_not_run_again);
    r = js_engine_eval_string(tc->engine,
        /* The script loader announces every game script it evaluated; a
           <script src> for one of those must not run the file a second
           time (loader plugins re-request the core scripts). */
        "globalThis.__fromFile = 0;\n"
        "__dom_noteScriptLoaded('tests/fixtures/inserted_script.js');\n"
        "var s4 = document.createElement('script');\n"
        "s4.src = './tests/fixtures/inserted_script.js';\n"
        "document.body.appendChild(s4);\n"
        "String(globalThis.__fromFile)", "test");
    if (r && strcmp(r, "0") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(insertAdjacentElement_positions);
    r = js_engine_eval_string(tc->engine,
        "var host = document.createElement('div');\n"
        "var a = document.createElement('span');\n"
        "var b = document.createElement('span');\n"
        "document.body.appendChild(host);\n"
        "host.insertAdjacentElement('beforebegin', a);\n"
        "host.insertAdjacentElement('afterend', b);\n"
        "var kids = document.body.children;\n"
        "var i = kids.indexOf(host);\n"
        "String(kids[i - 1] === a && kids[i + 1] === b)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(style_has_standard_properties);
    r = js_engine_eval_string(tc->engine,
        /* Feature probes ("transform" in el.style) must see the property. */
        "var el = document.createElement('div');\n"
        "String(('transform' in el.style) && ('opacity' in el.style) && ('filter' in el.style))", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(window_name_defined);
    r = js_engine_eval_string(tc->engine, "typeof name", "test");
    if (r && strcmp(r, "string") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: document object */

static void test_document(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(dom_setup); FAIL("engine setup"); return; }

    TEST(document_exists);
    char *r = js_engine_eval_string(tc->engine,
        "String(typeof document === 'object' && document !== null)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(document_body);
    r = js_engine_eval_string(tc->engine,
        "String(document.body !== null && document.body.tagName === 'BODY')", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(document_documentElement);
    r = js_engine_eval_string(tc->engine,
        "String(document.documentElement !== null && document.documentElement.tagName === 'HTML')", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(document_readyState);
    r = js_engine_eval_string(tc->engine,
        "document.readyState", "test");
    if (r && strcmp(r, "complete") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(document_title);
    r = js_engine_eval_string(tc->engine,
        "document.title = 'Test Game';\n"
        "document.title", "test");
    if (r && strcmp(r, "Test Game") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(document_getElementById);
    r = js_engine_eval_string(tc->engine,
        "var el = document.createElement('div');\n"
        "el.id = 'testEl';\n"
        "var found = document.getElementById('testEl');\n"
        "String(found === el)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(document_fonts_ready);
    r = js_engine_eval_string(tc->engine,
        "String(document.fonts.ready instanceof Promise)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(document_querySelector);
    r = js_engine_eval_string(tc->engine,
        "String(document.querySelector('body') === document.body)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(document_body_appendChild);
    r = js_engine_eval_string(tc->engine,
        "var child = document.createElement('div');\n"
        "document.body.appendChild(child);\n"
        "String(document.body.children.length > 0 && child.parentNode === document.body)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: navigator object */

static void test_navigator(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(dom_setup); FAIL("engine setup"); return; }

    TEST(navigator_exists);
    char *r = js_engine_eval_string(tc->engine,
        "String(typeof navigator === 'object' && navigator !== null)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(navigator_on_window);
    r = js_engine_eval_string(tc->engine,
        "String(window.navigator === navigator)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(navigator_userAgent_contains_Chrome);
    r = js_engine_eval_string(tc->engine,
        "String(navigator.userAgent.indexOf('Chrome') >= 0)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(navigator_userAgent_contains_Outsider);
    r = js_engine_eval_string(tc->engine,
        "String(navigator.userAgent.indexOf('Outsider') >= 0)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(navigator_standalone);
    r = js_engine_eval_string(tc->engine,
        "String(navigator.standalone)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(navigator_getGamepads);
    r = js_engine_eval_string(tc->engine,
        "var pads = navigator.getGamepads();\n"
        "Array.isArray(pads) + '|' + pads.length", "test");
    if (r && strcmp(r, "true|4") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(navigator_getGamepads_all_null);
    r = js_engine_eval_string(tc->engine,
        "var pads = navigator.getGamepads();\n"
        "String(pads[0] === null && pads[1] === null && pads[2] === null && pads[3] === null)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(navigator_platform);
    r = js_engine_eval_string(tc->engine,
        "navigator.platform", "test");
    if (r && strcmp(r, "Win32") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(navigator_language);
    r = js_engine_eval_string(tc->engine,
        "navigator.language", "test");
    if (r && strcmp(r, "en-US") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: HTMLElement DOM manipulation */

static void test_dom_manipulation(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(dom_setup); FAIL("engine setup"); return; }

    TEST(appendChild_removeChild);
    char *r = js_engine_eval_string(tc->engine,
        "var parent = document.createElement('div');\n"
        "var child = document.createElement('span');\n"
        "parent.appendChild(child);\n"
        "var len1 = parent.children.length;\n"
        "parent.removeChild(child);\n"
        "var len2 = parent.children.length;\n"
        "len1 + '|' + len2 + '|' + String(child.parentNode === null)", "test");
    if (r && strcmp(r, "1|0|true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(insertBefore);
    r = js_engine_eval_string(tc->engine,
        "var p = document.createElement('div');\n"
        "var c1 = document.createElement('span');\n"
        "var c2 = document.createElement('span');\n"
        "p.appendChild(c2);\n"
        "p.insertBefore(c1, c2);\n"
        "String(p.children[0] === c1 && p.children[1] === c2)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(setAttribute_getAttribute);
    r = js_engine_eval_string(tc->engine,
        "var el = document.createElement('div');\n"
        "el.setAttribute('data-value', '42');\n"
        "el.getAttribute('data-value')", "test");
    if (r && strcmp(r, "42") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(classList);
    r = js_engine_eval_string(tc->engine,
        "var el = document.createElement('div');\n"
        "el.classList.add('foo');\n"
        "el.classList.add('bar');\n"
        "var has = el.classList.contains('foo');\n"
        "el.classList.remove('foo');\n"
        "var hasAfter = el.classList.contains('foo');\n"
        "has + '|' + hasAfter", "test");
    if (r && strcmp(r, "true|false") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(element_style);
    r = js_engine_eval_string(tc->engine,
        "var el = document.createElement('div');\n"
        "el.style.display = 'none';\n"
        "el.style.setProperty('background-color', 'red');\n"
        "el.style.display + '|' + el.style.backgroundColor", "test");
    if (r && strcmp(r, "none|red") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(firstChild_lastChild);
    r = js_engine_eval_string(tc->engine,
        "var p = document.createElement('div');\n"
        "var c1 = document.createElement('span');\n"
        "var c2 = document.createElement('span');\n"
        "p.appendChild(c1);\n"
        "p.appendChild(c2);\n"
        "String(p.firstChild === c1) + '|' + String(p.lastChild === c2)", "test");
    if (r && strcmp(r, "true|true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(element_remove);
    r = js_engine_eval_string(tc->engine,
        "var p = document.createElement('div');\n"
        "var c = document.createElement('span');\n"
        "p.appendChild(c);\n"
        "c.remove();\n"
        "p.children.length + '|' + String(c.parentNode === null)", "test");
    if (r && strcmp(r, "0|true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: Blob and URL */

static void test_blob_url(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(dom_setup); FAIL("engine setup"); return; }

    TEST(blob_constructor);
    char *r = js_engine_eval_string(tc->engine,
        "var b = new Blob(['hello'], { type: 'text/plain' });\n"
        "b.type + '|' + b.size", "test");
    if (r && strcmp(r, "text/plain|5") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(url_createObjectURL);
    r = js_engine_eval_string(tc->engine,
        "var b = new Blob(['test']);\n"
        "var url = URL.createObjectURL(b);\n"
        "String(url.indexOf('blob:') === 0)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(url_revokeObjectURL);
    r = js_engine_eval_string(tc->engine,
        "var b = new Blob(['test']);\n"
        "var url = URL.createObjectURL(b);\n"
        "URL.revokeObjectURL(url);\n"
        "'ok'", "test");
    if (r && strcmp(r, "ok") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: _allCreatedElements registry cleanup */

static void test_element_registry(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(dom_setup); FAIL("engine setup"); return; }

    TEST(registry_grows_on_createElement);
    char *r = js_engine_eval_string(tc->engine,
        "var regLen0 = __dom_getElementRegistryLength();\n"
        "var el1 = document.createElement('div');\n"
        "var el2 = document.createElement('canvas');\n"
        "var regLen1 = __dom_getElementRegistryLength();\n"
        "String(regLen1 - regLen0)", "test");
    if (r && strcmp(r, "2") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(registry_shrinks_on_removeChild);
    r = js_engine_eval_string(tc->engine,
        "var parent = document.createElement('div');\n"
        "var child = document.createElement('span');\n"
        "parent.appendChild(child);\n"
        "var lenBefore = __dom_getElementRegistryLength();\n"
        "parent.removeChild(child);\n"
        "var lenAfter = __dom_getElementRegistryLength();\n"
        "String(lenBefore - lenAfter)", "test");
    if (r && strcmp(r, "1") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(registry_shrinks_on_element_remove);
    r = js_engine_eval_string(tc->engine,
        "var p = document.createElement('div');\n"
        "var c = document.createElement('span');\n"
        "p.appendChild(c);\n"
        "var lb = __dom_getElementRegistryLength();\n"
        "c.remove();\n"
        "var la = __dom_getElementRegistryLength();\n"
        "String(lb - la)", "test");
    if (r && strcmp(r, "1") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(registry_stable_on_reparent);
    r = js_engine_eval_string(tc->engine,
        "var p1 = document.createElement('div');\n"
        "var p2 = document.createElement('div');\n"
        "var ch = document.createElement('span');\n"
        "p1.appendChild(ch);\n"
        "var lenA = __dom_getElementRegistryLength();\n"
        "p2.appendChild(ch);\n"
        "var lenB = __dom_getElementRegistryLength();\n"
        "String(lenA === lenB) + '|' + String(ch.parentNode === p2)", "test");
    if (r && strcmp(r, "true|true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(registry_stable_on_insertBefore_reparent);
    r = js_engine_eval_string(tc->engine,
        "var pa = document.createElement('div');\n"
        "var pb = document.createElement('div');\n"
        "var ref = document.createElement('span');\n"
        "var mov = document.createElement('span');\n"
        "pa.appendChild(mov);\n"
        "pb.appendChild(ref);\n"
        "var lx = __dom_getElementRegistryLength();\n"
        "pb.insertBefore(mov, ref);\n"
        "var ly = __dom_getElementRegistryLength();\n"
        "String(lx === ly) + '|' + String(mov.parentNode === pb)", "test");
    if (r && strcmp(r, "true|true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(registry_manual_removeFromElementRegistry);
    r = js_engine_eval_string(tc->engine,
        "var orphan = document.createElement('div');\n"
        "var lBefore = __dom_getElementRegistryLength();\n"
        "__dom_removeFromElementRegistry(orphan);\n"
        "var lAfter = __dom_getElementRegistryLength();\n"
        "String(lBefore - lAfter)", "test");
    if (r && strcmp(r, "1") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: window resize API */

static void test_window_resize(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(dom_setup); FAIL("engine setup"); return; }

    TEST(dom_setWindowSize);
    /* Only inner/outer dimensions change; screen.width/height are monitor size. */
    char *r = js_engine_eval_string(tc->engine,
        "__dom_setWindowSize(1920, 1080);\n"
        "window.innerWidth + 'x' + window.innerHeight + '|' + "
        "window.outerWidth + 'x' + window.outerHeight", "test");
    if (r && strcmp(r, "1920x1080|1920x1080") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: btoa / atob */

static void test_base64(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(dom_setup); FAIL("engine setup"); return; }

    TEST(btoa);
    char *r = js_engine_eval_string(tc->engine,
        "btoa('Hello')", "test");
    if (r && strcmp(r, "SGVsbG8=") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(atob);
    r = js_engine_eval_string(tc->engine,
        "atob('SGVsbG8=')", "test");
    if (r && strcmp(r, "Hello") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(btoa_atob_roundtrip);
    r = js_engine_eval_string(tc->engine,
        "atob(btoa('Native Runtime'))", "test");
    if (r && strcmp(r, "Native Runtime") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Main */

int main(void)
{
    printf("=== DOM Shim Tests ===\n");

    printf("\n-- createElement --\n");
    test_create_element();

    printf("\n-- Canvas Element --\n");
    test_canvas_element();

    printf("\n-- Canvas2D Context --\n");
    test_canvas2d_context();

    printf("\n-- Image Element --\n");
    test_image_element();

    printf("\n-- Event Dispatch --\n");
    test_event_dispatch();

    printf("\n-- Timers --\n");
    test_timers();

    printf("\n-- requestAnimationFrame --\n");
    test_raf();

    printf("\n-- window --\n");
    test_window();

    printf("\n-- document --\n");
    test_inserted_scripts();
    test_document();

    printf("\n-- navigator --\n");
    test_navigator();

    printf("\n-- DOM Manipulation --\n");
    test_dom_manipulation();

    printf("\n-- Blob / URL --\n");
    test_blob_url();

    printf("\n-- Element Registry --\n");
    test_element_registry();

    printf("\n-- Window Resize --\n");
    test_window_resize();

    printf("\n-- Base64 --\n");
    test_base64();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
