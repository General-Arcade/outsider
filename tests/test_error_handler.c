/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#include "engine/error_handler.h"
#include "engine/js_engine.h"

#include <quickjs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_paths.h"

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

/* Log capture infrastructure */

#define LOG_BUF_MAX 4096
#define LOG_MSG_MAX 64

typedef struct {
    LogLevel levels[LOG_MSG_MAX];
    char messages[LOG_MSG_MAX][LOG_BUF_MAX];
    int count;
} LogCapture;

static void log_capture_cb(LogLevel level, const char *message, void *userdata)
{
    LogCapture *cap = userdata;
    if (cap->count < LOG_MSG_MAX) {
        cap->levels[cap->count] = level;
        strncpy(cap->messages[cap->count], message, LOG_BUF_MAX - 1);
        cap->messages[cap->count][LOG_BUF_MAX - 1] = '\0';
        cap->count++;
    }
}

static void log_capture_reset(LogCapture *cap)
{
    cap->count = 0;
}

/* Tests: Structured logging */

static void test_logging_basics(void)
{
    LogCapture cap;
    log_capture_reset(&cap);

    error_handler_init(NULL, LOG_DEBUG);
    error_handler_set_callback(log_capture_cb, &cap);

    TEST(log_debug_message);
    {
        log_capture_reset(&cap);
        error_handler_log(LOG_DEBUG, "debug test %d", 42);
        if (cap.count != 1) {
            char buf[64];
            snprintf(buf, sizeof(buf), "expected 1 message, got %d", cap.count);
            FAIL(buf);
        } else if (cap.levels[0] != LOG_DEBUG) {
            FAIL("expected DEBUG level");
        } else if (strstr(cap.messages[0], "debug test 42") == NULL) {
            FAIL("message content mismatch");
        } else {
            PASS();
        }
    }

    TEST(log_info_message);
    {
        log_capture_reset(&cap);
        error_handler_log(LOG_INFO, "info message");
        if (cap.count != 1 || cap.levels[0] != LOG_INFO) {
            FAIL("info log mismatch");
        } else {
            PASS();
        }
    }

    TEST(log_warn_message);
    {
        log_capture_reset(&cap);
        error_handler_log(LOG_WARN, "warning: %s", "test");
        if (cap.count != 1 || cap.levels[0] != LOG_WARN) {
            FAIL("warn log mismatch");
        } else if (strstr(cap.messages[0], "warning: test") == NULL) {
            FAIL("warn message content mismatch");
        } else {
            PASS();
        }
    }

    TEST(log_error_message);
    {
        log_capture_reset(&cap);
        error_handler_log(LOG_ERROR, "error occurred");
        if (cap.count != 1 || cap.levels[0] != LOG_ERROR) {
            FAIL("error log mismatch");
        } else {
            PASS();
        }
    }

    error_handler_shutdown();
}

static void test_log_level_filtering(void)
{
    LogCapture cap;
    log_capture_reset(&cap);

    error_handler_init(NULL, LOG_WARN);
    error_handler_set_callback(log_capture_cb, &cap);

    TEST(filter_below_min_level);
    {
        log_capture_reset(&cap);
        error_handler_log(LOG_DEBUG, "should not appear");
        error_handler_log(LOG_INFO, "should not appear either");
        if (cap.count != 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "expected 0 messages, got %d", cap.count);
            FAIL(buf);
        } else {
            PASS();
        }
    }

    TEST(pass_at_min_level);
    {
        log_capture_reset(&cap);
        error_handler_log(LOG_WARN, "should appear");
        error_handler_log(LOG_ERROR, "also should appear");
        if (cap.count != 2) {
            char buf[64];
            snprintf(buf, sizeof(buf), "expected 2 messages, got %d", cap.count);
            FAIL(buf);
        } else {
            PASS();
        }
    }

    TEST(change_level_at_runtime);
    {
        error_handler_set_level(LOG_DEBUG);
        if (error_handler_get_level() != LOG_DEBUG) {
            FAIL("level not set");
        } else {
            log_capture_reset(&cap);
            error_handler_log(LOG_DEBUG, "now visible");
            if (cap.count != 1) {
                FAIL("debug message should be visible after level change");
            } else {
                PASS();
            }
        }
    }

    error_handler_shutdown();
}

static void test_log_level_names(void)
{
    TEST(level_name_debug);
    if (strcmp(error_handler_level_name(LOG_DEBUG), "DEBUG") != 0) {
        FAIL("expected DEBUG");
    } else {
        PASS();
    }

    TEST(level_name_info);
    if (strcmp(error_handler_level_name(LOG_INFO), "INFO") != 0) {
        FAIL("expected INFO");
    } else {
        PASS();
    }

    TEST(level_name_warn);
    if (strcmp(error_handler_level_name(LOG_WARN), "WARN") != 0) {
        FAIL("expected WARN");
    } else {
        PASS();
    }

    TEST(level_name_error);
    if (strcmp(error_handler_level_name(LOG_ERROR), "ERROR") != 0) {
        FAIL("expected ERROR");
    } else {
        PASS();
    }
}

static void test_log_file_output(void)
{
    const char *log_path = test_tmp_path("rmmz_test_error_handler.log");
    remove(log_path);

    error_handler_init(log_path, LOG_DEBUG);

    TEST(log_file_created);
    {
        error_handler_log(LOG_INFO, "file test message");
        error_handler_shutdown();

        FILE *f = fopen(log_path, "r");
        if (!f) {
            FAIL("log file not created");
        } else {
            char buf[4096];
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            buf[n] = '\0';
            fclose(f);

            if (strstr(buf, "file test message") == NULL) {
                FAIL("message not found in log file");
            } else if (strstr(buf, "[INFO]") == NULL) {
                FAIL("[INFO] level not found in log file");
            } else {
                PASS();
            }
        }
    }

    TEST(log_file_contains_timestamp);
    {
        FILE *f = fopen(log_path, "r");
        if (!f) {
            FAIL("log file not found");
        } else {
            char buf[4096];
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            buf[n] = '\0';
            fclose(f);

            /* Timestamp format: [YYYY-MM-DD HH:MM:SS] */
            if (strstr(buf, "202") == NULL) {
                FAIL("timestamp not found in log file");
            } else {
                PASS();
            }
        }
    }

    remove(log_path);
}

/* Tests: JS exception handling */

static void test_js_exception_reporting(void)
{
    LogCapture cap;
    log_capture_reset(&cap);

    error_handler_init(NULL, LOG_DEBUG);
    error_handler_set_callback(log_capture_cb, &cap);

    JSEngine *engine = js_engine_init();
    if (!engine) {
        TEST(js_exception_requires_engine);
        FAIL("engine init failed");
        error_handler_shutdown();
        return;
    }

    /* DOM shim provides window.onerror. */
    js_engine_eval_file(engine, "src/shims/dom_shim.js");

    TEST(report_js_exception_logs_message);
    {
        log_capture_reset(&cap);

        JSContext *ctx = js_engine_get_context(engine);
        JSValue result = JS_Eval(ctx, "throw new Error('test exception')",
                                 strlen("throw new Error('test exception')"),
                                 "test.js", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result)) {
            error_handler_report_js_exception(engine, false);
        }
        JS_FreeValue(ctx, result);

        if (cap.count < 1) {
            FAIL("no log messages captured");
        } else {
            bool found = false;
            for (int i = 0; i < cap.count; i++) {
                if (strstr(cap.messages[i], "test exception") != NULL) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                char buf[256];
                snprintf(buf, sizeof(buf), "exception message not found; got: '%s'",
                         cap.messages[0]);
                FAIL(buf);
            } else {
                PASS();
            }
        }
    }

    TEST(report_js_exception_logs_stack);
    {
        log_capture_reset(&cap);

        JSContext *ctx = js_engine_get_context(engine);
        JSValue result = JS_Eval(ctx, "throw new Error('stack test')",
                                 strlen("throw new Error('stack test')"),
                                 "stack_test.js", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result)) {
            error_handler_report_js_exception(engine, false);
        }
        JS_FreeValue(ctx, result);

        bool found_stack = false;
        for (int i = 0; i < cap.count; i++) {
            if (strstr(cap.messages[i], "Stack trace") != NULL) {
                found_stack = true;
                break;
            }
        }
        if (!found_stack) {
            FAIL("stack trace not found in log output");
        } else {
            PASS();
        }
    }

    js_engine_shutdown(engine);
    error_handler_shutdown();
}

/* Tests: window.onerror dispatch */

static void test_window_onerror_dispatch(void)
{
    LogCapture cap;
    log_capture_reset(&cap);

    error_handler_init(NULL, LOG_DEBUG);
    error_handler_set_callback(log_capture_cb, &cap);

    JSEngine *engine = js_engine_init();
    if (!engine) {
        TEST(onerror_requires_engine);
        FAIL("engine init failed");
        error_handler_shutdown();
        return;
    }

    js_engine_eval_file(engine, "src/shims/dom_shim.js");

    TEST(window_onerror_called_on_exception);
    {
        js_engine_eval(engine,
            "var _lastError = null;\n"
            "window.onerror = function(msg, src, line, col, err) {\n"
            "    _lastError = msg;\n"
            "    return true;\n"
            "};\n",
            "test_setup.js");

        JSContext *ctx = js_engine_get_context(engine);
        JSValue result = JS_Eval(ctx, "throw new Error('onerror test')",
                                 strlen("throw new Error('onerror test')"),
                                 "test.js", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result)) {
            error_handler_report_js_exception(engine, false);
        }
        JS_FreeValue(ctx, result);

        char *last_err = js_engine_eval_string(engine, "_lastError", "test");
        if (!last_err) {
            FAIL("_lastError is null");
        } else if (strstr(last_err, "onerror test") == NULL) {
            char buf[256];
            snprintf(buf, sizeof(buf), "expected 'onerror test', got '%s'", last_err);
            FAIL(buf);
        } else {
            PASS();
        }
        js_engine_free_string(last_err);
    }

    TEST(window_error_event_listener_called);
    {
        js_engine_eval(engine,
            "var _errorEventFired = false;\n"
            "window.onerror = null;\n"  /* clear onerror so event listener runs */
            "window.addEventListener('error', function(e) {\n"
            "    _errorEventFired = true;\n"
            "});\n",
            "test_setup2.js");

        JSContext *ctx = js_engine_get_context(engine);
        JSValue result = JS_Eval(ctx, "throw new Error('event listener test')",
                                 strlen("throw new Error('event listener test')"),
                                 "test.js", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result)) {
            error_handler_report_js_exception(engine, false);
        }
        JS_FreeValue(ctx, result);

        char *fired = js_engine_eval_string(engine, "_errorEventFired ? 'true' : 'false'", "test");
        if (!fired || strcmp(fired, "true") != 0) {
            FAIL("error event listener not called");
        } else {
            PASS();
        }
        js_engine_free_string(fired);
    }

    js_engine_shutdown(engine);
    error_handler_shutdown();
}

/* Tests: window.onunhandledrejection */

static void test_unhandled_rejection(void)
{
    JSEngine *engine = js_engine_init();
    if (!engine) {
        TEST(rejection_requires_engine);
        FAIL("engine init failed");
        return;
    }

    js_engine_eval_file(engine, "src/shims/dom_shim.js");

    TEST(unhandledrejection_handler_exists);
    {
        char *result = js_engine_eval_string(engine,
            "typeof window.onunhandledrejection === 'object' ? 'null' : "
            "typeof window.onunhandledrejection",
            "test");
        if (!result) {
            FAIL("eval failed");
        } else if (strcmp(result, "null") != 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "expected 'null', got '%s'", result);
            FAIL(buf);
        } else {
            PASS();
        }
        js_engine_free_string(result);
    }

    TEST(onunhandledrejection_callable);
    {
        js_engine_eval(engine,
            "var _rejectionReason = null;\n"
            "window.onunhandledrejection = function(e) {\n"
            "    _rejectionReason = e.reason;\n"
            "};\n",
            "test_setup.js");

        js_engine_eval(engine,
            "__error_handler_dispatchRejection('test rejection');",
            "test.js");

        char *reason = js_engine_eval_string(engine, "_rejectionReason", "test");
        if (!reason || strcmp(reason, "test rejection") != 0) {
            FAIL("rejection reason mismatch");
        } else {
            PASS();
        }
        js_engine_free_string(reason);
    }

    TEST(unhandledrejection_event_listener);
    {
        js_engine_eval(engine,
            "window.onunhandledrejection = null;\n"
            "var _rejectionEvtFired = false;\n"
            "window.addEventListener('unhandledrejection', function(e) {\n"
            "    _rejectionEvtFired = true;\n"
            "});\n",
            "test_setup2.js");

        js_engine_eval(engine,
            "__error_handler_dispatchRejection('test via event');",
            "test.js");

        char *fired = js_engine_eval_string(engine,
            "_rejectionEvtFired ? 'true' : 'false'", "test");
        if (!fired || strcmp(fired, "true") != 0) {
            FAIL("unhandledrejection event not fired");
        } else {
            PASS();
        }
        js_engine_free_string(fired);
    }

    js_engine_shutdown(engine);
}

/* Tests: Error event constructors */

static void test_error_event_constructors(void)
{
    JSEngine *engine = js_engine_init();
    if (!engine) {
        TEST(constructors_require_engine);
        FAIL("engine init failed");
        return;
    }

    js_engine_eval_file(engine, "src/shims/dom_shim.js");

    TEST(ErrorEvent_constructor);
    {
        char *result = js_engine_eval_string(engine,
            "var e = new ErrorEvent('error', { message: 'test', lineno: 42 });\n"
            "e.type + ':' + e.message + ':' + e.lineno",
            "test");
        if (!result || strcmp(result, "error:test:42") != 0) {
            char buf[256];
            snprintf(buf, sizeof(buf), "expected 'error:test:42', got '%s'",
                     result ? result : "(null)");
            FAIL(buf);
        } else {
            PASS();
        }
        js_engine_free_string(result);
    }

    TEST(PromiseRejectionEvent_constructor);
    {
        char *result = js_engine_eval_string(engine,
            "var e = new PromiseRejectionEvent('unhandledrejection', { reason: 'fail' });\n"
            "e.type + ':' + e.reason",
            "test");
        if (!result || strcmp(result, "unhandledrejection:fail") != 0) {
            char buf[256];
            snprintf(buf, sizeof(buf), "expected 'unhandledrejection:fail', got '%s'",
                     result ? result : "(null)");
            FAIL(buf);
        } else {
            PASS();
        }
        js_engine_free_string(result);
    }

    js_engine_shutdown(engine);
}

/* Tests: Auto-save crash recovery */

static void test_auto_save(void)
{
    LogCapture cap;
    log_capture_reset(&cap);

    error_handler_init(NULL, LOG_DEBUG);
    error_handler_set_callback(log_capture_cb, &cap);

    JSEngine *engine = js_engine_init();
    if (!engine) {
        TEST(autosave_requires_engine);
        FAIL("engine init failed");
        error_handler_shutdown();
        return;
    }

    js_engine_eval_file(engine, "src/shims/dom_shim.js");

    TEST(auto_save_when_no_datamanager);
    {
        log_capture_reset(&cap);
        bool saved = error_handler_attempt_auto_save(engine);
        if (saved) {
            FAIL("should not succeed without DataManager");
        } else {
            PASS();
        }
    }

    TEST(auto_save_with_mock_datamanager);
    {
        js_engine_eval(engine,
            "var _autoSaveCalled = false;\n"
            "var DataManager = {\n"
            "    saveGame: function(slot) { _autoSaveCalled = true; }\n"
            "};\n"
            "var $gameSystem = {};\n",
            "test_setup.js");

        bool saved = error_handler_attempt_auto_save(engine);
        if (!saved) {
            FAIL("auto-save should have succeeded");
        } else {
            char *called = js_engine_eval_string(engine,
                "_autoSaveCalled ? 'true' : 'false'", "test");
            if (!called || strcmp(called, "true") != 0) {
                FAIL("DataManager.saveGame not called");
            } else {
                PASS();
            }
            js_engine_free_string(called);
        }
    }

    TEST(auto_save_null_engine);
    {
        bool saved = error_handler_attempt_auto_save(NULL);
        if (saved) {
            FAIL("should fail with NULL engine");
        } else {
            PASS();
        }
    }

    js_engine_shutdown(engine);
    error_handler_shutdown();
}

/* Tests: RPG Maker MZ compatibility */

static void test_rmmz_compatibility(void)
{
    JSEngine *engine = js_engine_init();
    if (!engine) {
        TEST(rmmz_requires_engine);
        FAIL("engine init failed");
        return;
    }

    js_engine_eval_file(engine, "src/shims/dom_shim.js");

    TEST(graphics_printerror_receives_onerror);
    {
        /* Graphics.printError hooked into window.onerror, as RPG Maker MZ does. */
        js_engine_eval(engine,
            "var _printErrorCalled = false;\n"
            "var Graphics = {\n"
            "    printError: function(name, message) {\n"
            "        _printErrorCalled = true;\n"
            "    }\n"
            "};\n"
            "window.onerror = function(msg) {\n"
            "    Graphics.printError('Error', msg);\n"
            "    return true;\n"
            "};\n",
            "test_setup.js");

        js_engine_eval(engine,
            "__error_handler_dispatch('Test error message', null);",
            "test.js");

        char *called = js_engine_eval_string(engine,
            "_printErrorCalled ? 'true' : 'false'", "test");
        if (!called || strcmp(called, "true") != 0) {
            FAIL("Graphics.printError not called via onerror");
        } else {
            PASS();
        }
        js_engine_free_string(called);
    }

    TEST(scenemanager_catchexception_pattern);
    {
        /* SceneManager.catchException calls window.onerror internally. */
        js_engine_eval(engine,
            "var _caughtMessage = null;\n"
            "window.onerror = function(msg) {\n"
            "    _caughtMessage = msg;\n"
            "    return true;\n"
            "};\n"
            "var SceneManager = {\n"
            "    catchException: function(e) {\n"
            "        window.onerror(e.message || String(e));\n"
            "    }\n"
            "};\n"
            "SceneManager.catchException(new Error('scene error'));\n",
            "test.js");

        char *msg = js_engine_eval_string(engine, "_caughtMessage", "test");
        if (!msg || strcmp(msg, "scene error") != 0) {
            char buf[256];
            snprintf(buf, sizeof(buf), "expected 'scene error', got '%s'",
                     msg ? msg : "(null)");
            FAIL(buf);
        } else {
            PASS();
        }
        js_engine_free_string(msg);
    }

    js_engine_shutdown(engine);
}

/* Main */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("=== Error Handler Tests ===\n");

    printf("\n--- Structured Logging ---\n");
    test_logging_basics();
    test_log_level_filtering();
    test_log_level_names();
    test_log_file_output();

    printf("\n--- JS Exception Handling ---\n");
    test_js_exception_reporting();

    printf("\n--- window.onerror Dispatch ---\n");
    test_window_onerror_dispatch();

    printf("\n--- Unhandled Rejection ---\n");
    test_unhandled_rejection();

    printf("\n--- Error Event Constructors ---\n");
    test_error_event_constructors();

    printf("\n--- Auto-Save / Crash Recovery ---\n");
    test_auto_save();

    printf("\n--- RPG Maker MZ Compatibility ---\n");
    test_rmmz_compatibility();

    printf("\nResults: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
