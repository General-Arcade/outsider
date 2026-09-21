/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#include "engine/js_engine.h"
#include "bindings/bind_io.h"
#include "test_paths.h"
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

/* Test data setup/teardown */

/* Scratch directory under the platform temp directory; filled in by main(). */
static char test_dir[TEST_PATH_MAX];

static void cleanup_test_dir(void)
{
    test_rm_rf(test_dir);
}

static void setup_test_data(void)
{
    cleanup_test_dir();
    file_io_mkdir(test_dir);

    char path[512];
    snprintf(path, sizeof(path), "%s/data", test_dir);
    file_io_mkdir(path);

    file_io_write_text(
        test_tmp_path("rmmz_test_xhr/data/Actors.json"),
        "[null,{\"id\":1,\"name\":\"Harold\",\"classId\":1}]"
    );

    file_io_write_text(
        test_tmp_path("rmmz_test_xhr/data/System.json"),
        "{\"gameTitle\":\"Test Game\",\"locale\":\"en_US\"}"
    );

    uint8_t bin[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    file_io_write_binary(test_tmp_path("rmmz_test_xhr/test.bin"), bin, sizeof(bin));

    file_io_write_text(test_tmp_path("rmmz_test_xhr/hello.txt"), "Hello, World!");
}

/* Read the XHR shim from the project root, or from build/ where tests may run. */
static char *read_shim_file(void)
{
    size_t size = 0;
    char *data = file_io_read_text("src/shims/xhr_shim.js", &size);
    if (!data) {
        data = file_io_read_text("../src/shims/xhr_shim.js", &size);
    }
    return data;
}

/* Helper: create engine with XHR shim loaded */

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

    file_io_set_game_root(test_dir);

    char *shim = read_shim_file();
    if (!shim) {
        fprintf(stderr, "Failed to read xhr_shim.js\n");
        js_engine_shutdown(tc->engine);
        free(tc);
        return NULL;
    }

    bool ok = js_engine_eval(tc->engine, shim, "xhr_shim.js");
    free(shim);
    if (!ok) {
        fprintf(stderr, "Failed to eval xhr_shim.js\n");
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

/* Tests: XMLHttpRequest constructor and constants */

static void test_xhr_constructor(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(xhr_setup); FAIL("engine setup"); return; }

    TEST(xhr_exists);
    char *r = js_engine_eval_string(tc->engine,
        "typeof XMLHttpRequest", "test");
    if (r && strcmp(r, "function") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(xhr_constants);
    r = js_engine_eval_string(tc->engine,
        "XMLHttpRequest.UNSENT + ',' + XMLHttpRequest.OPENED + ',' + "
        "XMLHttpRequest.HEADERS_RECEIVED + ',' + XMLHttpRequest.LOADING + ',' + "
        "XMLHttpRequest.DONE", "test");
    if (r && strcmp(r, "0,1,2,3,4") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(xhr_initial_state);
    r = js_engine_eval_string(tc->engine,
        "var x = new XMLHttpRequest();\n"
        "x.readyState + ',' + x.status + ',' + x.responseText + ',' + x.responseType",
        "test");
    if (r && strcmp(r, "0,0,,") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: open() */

static void test_xhr_open(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(xhr_setup); FAIL("engine setup"); return; }

    TEST(xhr_open_sets_readystate);
    char *r = js_engine_eval_string(tc->engine,
        "var x = new XMLHttpRequest();\n"
        "x.open('GET', 'data/Actors.json');\n"
        "String(x.readyState)", "test");
    if (r && strcmp(r, "1") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(xhr_open_default_async);
    r = js_engine_eval_string(tc->engine,
        "var x = new XMLHttpRequest();\n"
        "x.open('GET', 'test.txt');\n"
        "String(x._async)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(xhr_open_explicit_sync);
    r = js_engine_eval_string(tc->engine,
        "var x = new XMLHttpRequest();\n"
        "x.open('GET', 'test.txt', false);\n"
        "String(x._async)", "test");
    if (r && strcmp(r, "false") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: synchronous send() — text */

static void test_xhr_sync_text(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(xhr_setup); FAIL("engine setup"); return; }

    TEST(xhr_sync_load_text);
    char *r = js_engine_eval_string(tc->engine,
        "var x = new XMLHttpRequest();\n"
        "x.open('GET', 'hello.txt', false);\n"
        "x.send();\n"
        "x.status + '|' + x.responseText + '|' + x.readyState", "test");
    if (r && strcmp(r, "200|Hello, World!|4") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(xhr_sync_load_json);
    r = js_engine_eval_string(tc->engine,
        "var x = new XMLHttpRequest();\n"
        "x.open('GET', 'data/Actors.json', false);\n"
        "x.overrideMimeType('application/json');\n"
        "x.send();\n"
        "var parsed = JSON.parse(x.responseText);\n"
        "parsed[1].name", "test");
    if (r && strcmp(r, "Harold") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(xhr_sync_json_responsetype);
    r = js_engine_eval_string(tc->engine,
        "var x = new XMLHttpRequest();\n"
        "x.open('GET', 'data/System.json', false);\n"
        "x.responseType = 'json';\n"
        "x.send();\n"
        "x.response.gameTitle", "test");
    if (r && strcmp(r, "Test Game") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(xhr_sync_404);
    r = js_engine_eval_string(tc->engine,
        "var x = new XMLHttpRequest();\n"
        "var loadFired = false;\n"
        "var errorFired = false;\n"
        "x.onload = function() { loadFired = true; };\n"
        "x.onerror = function() { errorFired = true; };\n"
        "x.open('GET', 'nonexistent.json', false);\n"
        "x.send();\n"
        "x.status + '|' + loadFired + '|' + errorFired + '|' + x.readyState", "test");
    /* 404 fires load (not error) per XHR spec; caller checks status */
    if (r && strcmp(r, "404|true|false|4") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: synchronous send() — binary */

static void test_xhr_sync_binary(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(xhr_setup); FAIL("engine setup"); return; }

    TEST(xhr_sync_load_binary);
    char *r = js_engine_eval_string(tc->engine,
        "var x = new XMLHttpRequest();\n"
        "x.open('GET', 'test.bin', false);\n"
        "x.responseType = 'arraybuffer';\n"
        "x.send();\n"
        "var buf = x.response;\n"
        "var view = new Uint8Array(buf);\n"
        "view.length + '|' + view[0].toString(16) + '|' + view[1].toString(16)", "test");
    if (r && strcmp(r, "8|89|50") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(xhr_sync_binary_response_is_arraybuffer);
    r = js_engine_eval_string(tc->engine,
        "var x = new XMLHttpRequest();\n"
        "x.open('GET', 'test.bin', false);\n"
        "x.responseType = 'arraybuffer';\n"
        "x.send();\n"
        "x.response instanceof ArrayBuffer ? 'yes' : 'no'", "test");
    if (r && strcmp(r, "yes") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: asynchronous send() */

static void test_xhr_async(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(xhr_setup); FAIL("engine setup"); return; }

    /* onload fires only after pending jobs flush. */
    TEST(xhr_async_onload);
    js_engine_eval(tc->engine,
        "var asyncResult = 'pending';\n"
        "var asyncStatus = -1;\n"
        "var x = new XMLHttpRequest();\n"
        "x.open('GET', 'data/Actors.json');\n"
        "x.overrideMimeType('application/json');\n"
        "x.onload = function() {\n"
        "    asyncStatus = x.status;\n"
        "    asyncResult = JSON.parse(x.responseText)[1].name;\n"
        "};\n"
        "x.send();\n", "test");

    char *r = js_engine_eval_string(tc->engine, "asyncResult", "test");
    if (r && strcmp(r, "pending") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(xhr_async_flush);
    js_engine_execute_pending_jobs(tc->engine);

    r = js_engine_eval_string(tc->engine,
        "asyncStatus + '|' + asyncResult", "test");
    if (r && strcmp(r, "200|Harold") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    /* Async 404: fires load (not error) per XHR spec. */
    TEST(xhr_async_404);
    js_engine_eval(tc->engine,
        "var async404Status = -1;\n"
        "var async404Error = false;\n"
        "var async404Load = false;\n"
        "var x2 = new XMLHttpRequest();\n"
        "x2.open('GET', 'nonexistent.json');\n"
        "x2.onerror = function() { async404Error = true; };\n"
        "x2.onload = function() { async404Load = true; async404Status = x2.status; };\n"
        "x2.send();\n", "test");
    js_engine_execute_pending_jobs(tc->engine);

    r = js_engine_eval_string(tc->engine,
        "async404Error + '|' + async404Load + '|' + async404Status", "test");
    if (r && strcmp(r, "false|true|404") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(xhr_async_binary);
    js_engine_eval(tc->engine,
        "var asyncBinResult = null;\n"
        "var x3 = new XMLHttpRequest();\n"
        "x3.open('GET', 'test.bin');\n"
        "x3.responseType = 'arraybuffer';\n"
        "x3.onload = function() {\n"
        "    var view = new Uint8Array(x3.response);\n"
        "    asyncBinResult = view.length + '|' + view[0].toString(16);\n"
        "};\n"
        "x3.send();\n", "test");
    js_engine_execute_pending_jobs(tc->engine);

    r = js_engine_eval_string(tc->engine, "asyncBinResult", "test");
    if (r && strcmp(r, "8|89") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: overrideMimeType and getResponseHeader */

static void test_xhr_mime(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(xhr_setup); FAIL("engine setup"); return; }

    TEST(xhr_overrideMimeType);
    char *r = js_engine_eval_string(tc->engine,
        "var x = new XMLHttpRequest();\n"
        "x.open('GET', 'data/Actors.json', false);\n"
        "x.overrideMimeType('application/json');\n"
        "x.send();\n"
        "x.getResponseHeader('Content-Type')", "test");
    if (r && strcmp(r, "application/json") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(xhr_getResponseHeader_before_send);
    r = js_engine_eval_string(tc->engine,
        "var x = new XMLHttpRequest();\n"
        "x.open('GET', 'data/Actors.json', false);\n"
        "var h = x.getResponseHeader('Content-Type');\n"
        "String(h)", "test");
    if (r && strcmp(r, "null") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: readyStateChange events */

static void test_xhr_readystate(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(xhr_setup); FAIL("engine setup"); return; }

    TEST(xhr_readystate_changes);
    char *r = js_engine_eval_string(tc->engine,
        "var states = [];\n"
        "var x = new XMLHttpRequest();\n"
        "x.onreadystatechange = function() { states.push(x.readyState); };\n"
        "x.open('GET', 'hello.txt', false);\n"
        "x.send();\n"
        "states.join(',')", "test");
    /* open fires 1, then send fires 2,3,4 */
    if (r && strcmp(r, "1,2,3,4") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(xhr_readystate_changes_404);
    r = js_engine_eval_string(tc->engine,
        "var states404 = [];\n"
        "var x2 = new XMLHttpRequest();\n"
        "x2.onreadystatechange = function() { states404.push(x2.readyState); };\n"
        "x2.open('GET', 'nonexistent.txt', false);\n"
        "x2.send();\n"
        "states404.join(',')", "test");
    /* 404 goes through the same readyState sequence. */
    if (r && strcmp(r, "1,2,3,4") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: onloadend event */

static void test_xhr_loadend(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(xhr_setup); FAIL("engine setup"); return; }

    TEST(xhr_loadend_on_success);
    char *r = js_engine_eval_string(tc->engine,
        "var loadendFired = false;\n"
        "var x = new XMLHttpRequest();\n"
        "x.onloadend = function() { loadendFired = true; };\n"
        "x.open('GET', 'hello.txt', false);\n"
        "x.send();\n"
        "String(loadendFired)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(xhr_loadend_on_error);
    r = js_engine_eval_string(tc->engine,
        "var loadendFired2 = false;\n"
        "var x = new XMLHttpRequest();\n"
        "x.onloadend = function() { loadendFired2 = true; };\n"
        "x.open('GET', 'nonexistent.txt', false);\n"
        "x.send();\n"
        "String(loadendFired2)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: abort() */

static void test_xhr_abort(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(xhr_setup); FAIL("engine setup"); return; }

    TEST(xhr_abort_prevents_callback);
    js_engine_eval(tc->engine,
        "var abortResult = 'not_called';\n"
        "var xa = new XMLHttpRequest();\n"
        "xa.open('GET', 'hello.txt');\n"
        "xa.onload = function() { abortResult = 'called'; };\n"
        "xa.send();\n"
        "xa.abort();\n", "test");
    js_engine_execute_pending_jobs(tc->engine);

    char *r = js_engine_eval_string(tc->engine, "abortResult", "test");
    if (r && strcmp(r, "not_called") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: RPG Maker MZ-style DataManager.loadDataFile pattern */

static void test_xhr_rmmz_pattern(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(xhr_setup); FAIL("engine setup"); return; }

    TEST(xhr_rmmz_datamanager_pattern);
    js_engine_eval(tc->engine,
        "var $dataActors = null;\n"
        "var loadComplete = false;\n"
        "function loadDataFile(name, src) {\n"
        "    var xhr = new XMLHttpRequest();\n"
        "    var url = 'data/' + src;\n"
        "    xhr.open('GET', url);\n"
        "    xhr.overrideMimeType('application/json');\n"
        "    xhr.onload = function() {\n"
        "        if (xhr.status < 400) {\n"
        "            globalThis[name] = JSON.parse(xhr.responseText);\n"
        "        }\n"
        "        loadComplete = true;\n"
        "    };\n"
        "    xhr.onerror = function() {\n"
        "        loadComplete = true;\n"
        "    };\n"
        "    xhr.send();\n"
        "}\n"
        "loadDataFile('$dataActors', 'Actors.json');\n", "test");

    js_engine_execute_pending_jobs(tc->engine);

    char *r = js_engine_eval_string(tc->engine,
        "loadComplete + '|' + $dataActors[1].name + '|' + $dataActors[1].classId",
        "test");
    if (r && strcmp(r, "true|Harold|1") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    /* Multiple concurrent loads, as in the RPG Maker MZ boot sequence. */
    TEST(xhr_rmmz_multiple_loads);
    js_engine_eval(tc->engine,
        "var $dataSystem = null;\n"
        "var allLoaded = 0;\n"
        "function loadData2(name, src) {\n"
        "    var xhr = new XMLHttpRequest();\n"
        "    var url = 'data/' + src;\n"
        "    xhr.open('GET', url);\n"
        "    xhr.overrideMimeType('application/json');\n"
        "    xhr.onload = function() {\n"
        "        if (xhr.status < 400) {\n"
        "            globalThis[name] = JSON.parse(xhr.responseText);\n"
        "        }\n"
        "        allLoaded++;\n"
        "    };\n"
        "    xhr.send();\n"
        "}\n"
        "loadData2('$dataActors', 'Actors.json');\n"
        "loadData2('$dataSystem', 'System.json');\n", "test");

    js_engine_execute_pending_jobs(tc->engine);

    r = js_engine_eval_string(tc->engine,
        "allLoaded + '|' + $dataActors[1].name + '|' + $dataSystem.gameTitle",
        "test");
    if (r && strcmp(r, "2|Harold|Test Game") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

/* Tests: addEventListener/removeEventListener */

static void test_xhr_event_listeners(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(xhr_setup); FAIL("engine setup"); return; }

    TEST(xhr_addEventListener);
    char *r = js_engine_eval_string(tc->engine,
        "var evtResult = '';\n"
        "var x = new XMLHttpRequest();\n"
        "x.addEventListener('load', function() { evtResult = 'loaded'; });\n"
        "x.open('GET', 'hello.txt', false);\n"
        "x.send();\n"
        "evtResult", "test");
    if (r && strcmp(r, "loaded") == 0) { PASS(); } else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

int main(void)
{
    snprintf(test_dir, sizeof(test_dir), "%s/rmmz_test_xhr", test_tmp_root());

    printf("=== XMLHttpRequest Shim Tests ===\n");

    setup_test_data();

    printf("\n-- Constructor and Constants --\n");
    test_xhr_constructor();

    printf("\n-- open() --\n");
    test_xhr_open();

    printf("\n-- Sync Text Loading --\n");
    test_xhr_sync_text();

    printf("\n-- Sync Binary Loading --\n");
    test_xhr_sync_binary();

    printf("\n-- Async Loading --\n");
    test_xhr_async();

    printf("\n-- MIME Type --\n");
    test_xhr_mime();

    printf("\n-- ReadyState Changes --\n");
    test_xhr_readystate();

    printf("\n-- Loadend Event --\n");
    test_xhr_loadend();

    printf("\n-- Abort --\n");
    test_xhr_abort();

    printf("\n-- RPG Maker MZ Pattern --\n");
    test_xhr_rmmz_pattern();

    printf("\n-- Event Listeners --\n");
    test_xhr_event_listeners();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);

    cleanup_test_dir();

    return (tests_passed == tests_run) ? 0 : 1;
}
