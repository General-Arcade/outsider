/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#include "engine/js_engine.h"
#include "test_paths.h"

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

/* Console capture infrastructure */

#define CONSOLE_BUF_MAX 4096
#define CONSOLE_MSG_MAX 64

typedef struct {
    char level[CONSOLE_MSG_MAX][16];
    char message[CONSOLE_MSG_MAX][CONSOLE_BUF_MAX];
    int count;
} ConsoleCapture;

static void console_capture_cb(const char *level, const char *message, void *userdata)
{
    ConsoleCapture *cap = userdata;
    if (cap->count < CONSOLE_MSG_MAX) {
        strncpy(cap->level[cap->count], level, 15);
        cap->level[cap->count][15] = '\0';
        strncpy(cap->message[cap->count], message, CONSOLE_BUF_MAX - 1);
        cap->message[cap->count][CONSOLE_BUF_MAX - 1] = '\0';
        cap->count++;
    }
}

static void console_capture_reset(ConsoleCapture *cap)
{
    cap->count = 0;
}

static void test_engine_init_shutdown(void)
{
    TEST(engine_init_returns_non_null);
    JSEngine *engine = js_engine_init();
    if (!engine) {
        FAIL("js_engine_init returned NULL");
        return;
    }
    PASS();

    TEST(engine_shutdown);
    js_engine_shutdown(engine);
    PASS();

    TEST(engine_shutdown_null);
    js_engine_shutdown(NULL); /* Should not crash. */
    PASS();
}

static void test_eval_simple_expressions(void)
{
    JSEngine *engine = js_engine_init();
    if (!engine) {
        TEST(eval_requires_engine);
        FAIL("engine init failed");
        return;
    }

    TEST(eval_arithmetic);
    {
        char *result = js_engine_eval_string(engine, "1 + 2", "test");
        if (!result) {
            FAIL("eval returned NULL");
        } else if (strcmp(result, "3") != 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "expected '3', got '%s'", result);
            FAIL(buf);
        } else {
            PASS();
        }
        js_engine_free_string(result);
    }

    TEST(eval_string_concat);
    {
        char *result = js_engine_eval_string(engine, "'hello' + ' ' + 'world'", "test");
        if (!result) {
            FAIL("eval returned NULL");
        } else if (strcmp(result, "hello world") != 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "expected 'hello world', got '%s'", result);
            FAIL(buf);
        } else {
            PASS();
        }
        js_engine_free_string(result);
    }

    TEST(eval_boolean);
    {
        char *result = js_engine_eval_string(engine, "true && false", "test");
        if (!result) {
            FAIL("eval returned NULL");
        } else if (strcmp(result, "false") != 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "expected 'false', got '%s'", result);
            FAIL(buf);
        } else {
            PASS();
        }
        js_engine_free_string(result);
    }

    TEST(eval_undefined);
    {
        char *result = js_engine_eval_string(engine, "undefined", "test");
        if (!result) {
            FAIL("eval returned NULL");
        } else if (strcmp(result, "undefined") != 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "expected 'undefined', got '%s'", result);
            FAIL(buf);
        } else {
            PASS();
        }
        js_engine_free_string(result);
    }

    TEST(eval_multiline_script);
    {
        bool ok = js_engine_eval(engine,
            "var x = 10;\n"
            "var y = 20;\n"
            "var z = x + y;\n",
            "test");
        if (!ok) {
            FAIL("eval returned false");
        } else {
            char *result = js_engine_eval_string(engine, "z", "test");
            if (!result || strcmp(result, "30") != 0) {
                FAIL("variable not persisted across evals");
            } else {
                PASS();
            }
            js_engine_free_string(result);
        }
    }

    TEST(eval_function_definition_and_call);
    {
        bool ok = js_engine_eval(engine,
            "function add(a, b) { return a + b; }", "test");
        if (!ok) {
            FAIL("function definition failed");
        } else {
            char *result = js_engine_eval_string(engine, "add(3, 4)", "test");
            if (!result || strcmp(result, "7") != 0) {
                FAIL("function call returned wrong result");
            } else {
                PASS();
            }
            js_engine_free_string(result);
        }
    }

    js_engine_shutdown(engine);
}

static void test_console_output(void)
{
    JSEngine *engine = js_engine_init();
    if (!engine) {
        TEST(console_requires_engine);
        FAIL("engine init failed");
        return;
    }

    ConsoleCapture cap;
    console_capture_reset(&cap);
    js_engine_set_console_callback(engine, console_capture_cb, &cap);

    TEST(console_log);
    {
        console_capture_reset(&cap);
        js_engine_eval(engine, "console.log('hello')", "test");
        if (cap.count != 1) {
            char buf[64];
            snprintf(buf, sizeof(buf), "expected 1 message, got %d", cap.count);
            FAIL(buf);
        } else if (strcmp(cap.level[0], "log") != 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "expected level 'log', got '%s'", cap.level[0]);
            FAIL(buf);
        } else if (strcmp(cap.message[0], "hello") != 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "expected 'hello', got '%s'", cap.message[0]);
            FAIL(buf);
        } else {
            PASS();
        }
    }

    TEST(console_warn);
    {
        console_capture_reset(&cap);
        js_engine_eval(engine, "console.warn('careful')", "test");
        if (cap.count != 1 || strcmp(cap.level[0], "warn") != 0 ||
            strcmp(cap.message[0], "careful") != 0) {
            FAIL("warn output mismatch");
        } else {
            PASS();
        }
    }

    TEST(console_error);
    {
        console_capture_reset(&cap);
        js_engine_eval(engine, "console.error('oops')", "test");
        if (cap.count != 1 || strcmp(cap.level[0], "error") != 0 ||
            strcmp(cap.message[0], "oops") != 0) {
            FAIL("error output mismatch");
        } else {
            PASS();
        }
    }

    TEST(console_log_multiple_args);
    {
        console_capture_reset(&cap);
        js_engine_eval(engine, "console.log('a', 'b', 'c')", "test");
        if (cap.count != 1 || strcmp(cap.message[0], "a b c") != 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "expected 'a b c', got '%s'",
                     cap.count > 0 ? cap.message[0] : "(none)");
            FAIL(buf);
        } else {
            PASS();
        }
    }

    TEST(console_log_mixed_types);
    {
        console_capture_reset(&cap);
        js_engine_eval(engine, "console.log('count:', 42, true)", "test");
        if (cap.count != 1 || strcmp(cap.message[0], "count: 42 true") != 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "expected 'count: 42 true', got '%s'",
                     cap.count > 0 ? cap.message[0] : "(none)");
            FAIL(buf);
        } else {
            PASS();
        }
    }

    TEST(console_log_no_args);
    {
        console_capture_reset(&cap);
        js_engine_eval(engine, "console.log()", "test");
        if (cap.count != 1 || strcmp(cap.message[0], "") != 0) {
            FAIL("no-arg log should produce empty message");
        } else {
            PASS();
        }
    }

    js_engine_shutdown(engine);
}

static void test_error_handling(void)
{
    JSEngine *engine = js_engine_init();
    if (!engine) {
        TEST(error_requires_engine);
        FAIL("engine init failed");
        return;
    }

    ConsoleCapture cap;
    console_capture_reset(&cap);
    js_engine_set_console_callback(engine, console_capture_cb, &cap);

    TEST(syntax_error_returns_false);
    {
        console_capture_reset(&cap);
        bool ok = js_engine_eval(engine, "function {{{ bad", "test_syntax.js");
        if (ok) {
            FAIL("expected eval to fail on syntax error");
        } else {
            PASS();
        }
    }

    TEST(syntax_error_produces_message);
    {
        /* cap should have captured the error from the previous test */
        if (cap.count < 1) {
            FAIL("no error message captured");
        } else if (strcmp(cap.level[0], "error") != 0) {
            FAIL("expected error level");
        } else {
            PASS();
        }
    }

    TEST(reference_error);
    {
        console_capture_reset(&cap);
        bool ok = js_engine_eval(engine, "nonexistentVariable", "test_ref.js");
        if (ok) {
            FAIL("expected eval to fail on reference error");
        } else if (cap.count < 1) {
            FAIL("no error message captured");
        } else {
            PASS();
        }
    }

    TEST(throw_error);
    {
        console_capture_reset(&cap);
        bool ok = js_engine_eval(engine, "throw new Error('test error')", "test_throw.js");
        if (ok) {
            FAIL("expected eval to fail on throw");
        } else if (cap.count < 1) {
            FAIL("no error message captured");
        } else {
            /* Check that the error message contains our text. */
            if (strstr(cap.message[0], "test error") == NULL) {
                char buf[256];
                snprintf(buf, sizeof(buf), "error message '%s' doesn't contain 'test error'",
                         cap.message[0]);
                FAIL(buf);
            } else {
                PASS();
            }
        }
    }

    TEST(throw_error_has_stack_trace);
    {
        console_capture_reset(&cap);
        js_engine_eval(engine, "throw new Error('stack test')", "test_stack.js");
        /* Expect at least 2 messages: the error message and the stack trace. */
        if (cap.count < 2) {
            char buf[64];
            snprintf(buf, sizeof(buf), "expected >= 2 messages (error+stack), got %d", cap.count);
            FAIL(buf);
        } else if (strstr(cap.message[1], "test_stack.js") == NULL) {
            char buf[256];
            snprintf(buf, sizeof(buf), "stack trace '%s' doesn't mention filename",
                     cap.message[1]);
            FAIL(buf);
        } else {
            PASS();
        }
    }

    TEST(eval_string_returns_null_on_error);
    {
        char *result = js_engine_eval_string(engine, "throw 'fail'", "test");
        if (result != NULL) {
            FAIL("expected NULL on error");
            js_engine_free_string(result);
        } else {
            PASS();
        }
    }

    TEST(eval_null_script);
    {
        bool ok = js_engine_eval(engine, NULL, "test");
        if (ok) {
            FAIL("expected false for NULL script");
        } else {
            PASS();
        }
    }

    TEST(eval_null_engine);
    {
        bool ok = js_engine_eval(NULL, "1+1", "test");
        if (ok) {
            FAIL("expected false for NULL engine");
        } else {
            PASS();
        }
    }

    js_engine_shutdown(engine);
}

static void test_eval_file(void)
{
    /* Create a temporary JS file for testing. */
    const char *tmp_path = test_tmp_path("rmmz_test_script.js");
    FILE *f = fopen(tmp_path, "w");
    if (!f) {
        TEST(eval_file_setup);
        FAIL("could not create temp file");
        return;
    }
    fprintf(f, "var testFileVar = 'loaded from file';\n");
    fprintf(f, "console.log('file script executed');\n");
    fclose(f);

    JSEngine *engine = js_engine_init();
    if (!engine) {
        TEST(eval_file_requires_engine);
        FAIL("engine init failed");
        return;
    }

    ConsoleCapture cap;
    console_capture_reset(&cap);
    js_engine_set_console_callback(engine, console_capture_cb, &cap);

    TEST(eval_file_success);
    {
        bool ok = js_engine_eval_file(engine, tmp_path);
        if (!ok) {
            FAIL("eval_file returned false");
        } else {
            PASS();
        }
    }

    TEST(eval_file_variable_accessible);
    {
        char *result = js_engine_eval_string(engine, "testFileVar", "test");
        if (!result || strcmp(result, "loaded from file") != 0) {
            FAIL("variable from file not accessible");
        } else {
            PASS();
        }
        js_engine_free_string(result);
    }

    TEST(eval_file_console_output);
    {
        if (cap.count < 1 || strcmp(cap.message[0], "file script executed") != 0) {
            FAIL("console.log from file not captured");
        } else {
            PASS();
        }
    }

    TEST(eval_file_nonexistent);
    {
        bool ok = js_engine_eval_file(engine, test_tmp_path("nonexistent_rmmz_test_12345.js"));
        if (ok) {
            FAIL("expected false for nonexistent file");
        } else {
            PASS();
        }
    }

    TEST(eval_file_null_path);
    {
        bool ok = js_engine_eval_file(engine, NULL);
        if (ok) {
            FAIL("expected false for NULL path");
        } else {
            PASS();
        }
    }

    js_engine_shutdown(engine);
    remove(tmp_path);
}

static void test_pending_jobs(void)
{
    JSEngine *engine = js_engine_init();
    if (!engine) {
        TEST(pending_jobs_requires_engine);
        FAIL("engine init failed");
        return;
    }

    ConsoleCapture cap;
    console_capture_reset(&cap);
    js_engine_set_console_callback(engine, console_capture_cb, &cap);

    TEST(execute_pending_jobs_no_jobs);
    {
        int count = js_engine_execute_pending_jobs(engine);
        if (count != 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "expected 0 jobs, got %d", count);
            FAIL(buf);
        } else {
            PASS();
        }
    }

    TEST(execute_pending_jobs_with_promise);
    {
        console_capture_reset(&cap);
        js_engine_eval(engine,
            "Promise.resolve('ok').then(function(v) { console.log('promise:', v); });",
            "test");
        int count = js_engine_execute_pending_jobs(engine);
        if (count < 1) {
            char buf[64];
            snprintf(buf, sizeof(buf), "expected >= 1 job, got %d", count);
            FAIL(buf);
        } else if (cap.count < 1 || strcmp(cap.message[0], "promise: ok") != 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "expected 'promise: ok', got '%s'",
                     cap.count > 0 ? cap.message[0] : "(none)");
            FAIL(buf);
        } else {
            PASS();
        }
    }

    TEST(execute_pending_jobs_null_engine);
    {
        int count = js_engine_execute_pending_jobs(NULL);
        if (count != -1) {
            FAIL("expected -1 for NULL engine");
        } else {
            PASS();
        }
    }

    js_engine_shutdown(engine);
}

static void test_state_persistence(void)
{
    JSEngine *engine = js_engine_init();
    if (!engine) {
        TEST(state_requires_engine);
        FAIL("engine init failed");
        return;
    }

    TEST(global_state_persists_across_evals);
    {
        js_engine_eval(engine, "var globalCounter = 0;", "test");
        js_engine_eval(engine, "globalCounter += 5;", "test");
        js_engine_eval(engine, "globalCounter += 10;", "test");
        char *result = js_engine_eval_string(engine, "globalCounter", "test");
        if (!result || strcmp(result, "15") != 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "expected '15', got '%s'",
                     result ? result : "(null)");
            FAIL(buf);
        } else {
            PASS();
        }
        js_engine_free_string(result);
    }

    TEST(object_creation);
    {
        js_engine_eval(engine,
            "var obj = { name: 'test', value: 42 };", "test");
        char *result = js_engine_eval_string(engine, "obj.name + ':' + obj.value", "test");
        if (!result || strcmp(result, "test:42") != 0) {
            FAIL("object property access failed");
        } else {
            PASS();
        }
        js_engine_free_string(result);
    }

    TEST(array_operations);
    {
        js_engine_eval(engine, "var arr = [1, 2, 3];", "test");
        char *result = js_engine_eval_string(engine, "arr.length", "test");
        if (!result || strcmp(result, "3") != 0) {
            FAIL("array length wrong");
        } else {
            PASS();
        }
        js_engine_free_string(result);
    }

    js_engine_shutdown(engine);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("=== JS Engine Tests ===\n");
    test_engine_init_shutdown();
    test_eval_simple_expressions();
    test_console_output();
    test_error_handling();
    test_eval_file();
    test_pending_jobs();
    test_state_persistence();

    printf("\nResults: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
