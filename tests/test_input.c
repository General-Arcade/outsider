/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#include "engine/js_engine.h"
#include "bindings/bind_io.h"
#include "bindings/bind_input.h"
#include "input/input_manager.h"
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

/* Helper: read a shim file into a string. */

static char *read_shim(const char *relpath)
{
    size_t size = 0;
    char *data = file_io_read_text(relpath, &size);
    if (!data) {
        char altpath[512];
        snprintf(altpath, sizeof(altpath), "../%s", relpath);
        data = file_io_read_text(altpath, &size);
    }
    return data;
}

/* Helper: create engine with DOM + navigator shims + input bindings. */

typedef struct {
    JSEngine *engine;
} TestCtx;

static TestCtx *create_test_ctx(void)
{
    TestCtx *tc = calloc(1, sizeof(TestCtx));
    tc->engine = js_engine_init();
    if (!tc->engine) {
        free(tc);
        return NULL;
    }

    input_manager_init();
    bind_io_register(js_engine_get_context(tc->engine));
    bind_input_register(js_engine_get_context(tc->engine));

    /* Load DOM shim. */
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

    /* Load navigator shim. */
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
    input_manager_shutdown();
    free(tc);
}

/* Tests: input_manager C-level */

static void test_input_manager_lifecycle(void)
{
    TEST(input_manager_init_shutdown);
    input_manager_init();

    /* Push event and poll it back. */
    InputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_KEY_DOWN;
    ev.keyCode = 65;
    strncpy(ev.key, "a", sizeof(ev.key) - 1);
    strncpy(ev.code, "KeyA", sizeof(ev.code) - 1);
    input_manager_push_event(&ev);

    InputEvent out[8];
    int count = input_manager_poll_events(out, 8);
    if (count == 1 && out[0].type == INPUT_KEY_DOWN && out[0].keyCode == 65
        && strcmp(out[0].key, "a") == 0 && strcmp(out[0].code, "KeyA") == 0) {
        PASS();
    } else {
        FAIL("event round-trip mismatch");
    }

    input_manager_shutdown();
}

static void test_input_manager_poll_clears(void)
{
    TEST(poll_clears_queue);
    input_manager_init();

    InputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_KEY_DOWN;
    ev.keyCode = 13;
    input_manager_push_event(&ev);

    InputEvent out[8];
    int count1 = input_manager_poll_events(out, 8);
    int count2 = input_manager_poll_events(out, 8);

    if (count1 == 1 && count2 == 0) {
        PASS();
    } else {
        FAIL("queue not cleared after poll");
    }

    input_manager_shutdown();
}

static void test_input_manager_multiple_events(void)
{
    TEST(multiple_events);
    input_manager_init();

    InputEvent ev;
    memset(&ev, 0, sizeof(ev));

    ev.type = INPUT_KEY_DOWN;
    ev.keyCode = 37;
    input_manager_push_event(&ev);

    ev.type = INPUT_KEY_UP;
    ev.keyCode = 37;
    input_manager_push_event(&ev);

    ev.type = INPUT_MOUSE_DOWN;
    ev.mouseX = 100;
    ev.mouseY = 200;
    ev.button = 0;
    input_manager_push_event(&ev);

    InputEvent out[8];
    int count = input_manager_poll_events(out, 8);

    if (count == 3 && out[0].type == INPUT_KEY_DOWN && out[1].type == INPUT_KEY_UP
        && out[2].type == INPUT_MOUSE_DOWN && out[2].mouseX == 100) {
        PASS();
    } else {
        FAIL("multiple event mismatch");
    }

    input_manager_shutdown();
}

static void test_input_manager_noop_uninitialized(void)
{
    TEST(noop_uninitialized);
    input_manager_shutdown(); /* Ensure uninitialized. */

    InputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_KEY_DOWN;
    input_manager_push_event(&ev); /* Should be no-op. */

    InputEvent out[8];
    int count = input_manager_poll_events(out, 8);

    if (count == 0) {
        PASS();
    } else {
        FAIL("expected 0 events when uninitialized");
    }
}

static void test_input_manager_gamepad(void)
{
    TEST(gamepad_state);
    input_manager_init();

    GamepadState gs;
    memset(&gs, 0, sizeof(gs));
    gs.connected = true;
    gs.index = 0;
    strncpy(gs.id, "Test Controller", sizeof(gs.id) - 1);
    gs.buttons[0].pressed = true;
    gs.buttons[0].value = 1.0;
    gs.axes[0] = 0.5;
    gs.axes[1] = -0.25;

    input_manager_set_gamepad(0, &gs);

    const GamepadState *gp = input_manager_get_gamepad(0);
    if (gp && gp->connected && strcmp(gp->id, "Test Controller") == 0
        && gp->buttons[0].pressed && gp->axes[0] == 0.5 && gp->axes[1] == -0.25) {
        PASS();
    } else {
        FAIL("gamepad state mismatch");
    }

    TEST(gamepad_clear);
    input_manager_clear_gamepad(0);
    gp = input_manager_get_gamepad(0);
    if (gp && !gp->connected) {
        PASS();
    } else {
        FAIL("gamepad should be disconnected after clear");
    }

    TEST(gamepad_null_out_of_range);
    gp = input_manager_get_gamepad(5);
    if (!gp) {
        PASS();
    } else {
        FAIL("should return NULL for out-of-range index");
    }

    input_manager_shutdown();
}

/* Tests: JS-level input event dispatch */

static void test_js_keyboard_event_class(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(setup); FAIL("engine setup"); return; }

    TEST(KeyboardEvent_class);
    char *r = js_engine_eval_string(tc->engine,
        "var e = new KeyboardEvent('keydown', {\n"
        "    key: 'a', code: 'KeyA', keyCode: 65,\n"
        "    shiftKey: false, ctrlKey: true\n"
        "});\n"
        "e.type + '|' + e.key + '|' + e.code + '|' + e.keyCode + '|' + e.which + '|'"
        " + e.ctrlKey + '|' + e.shiftKey", "test");
    if (r && strcmp(r, "keydown|a|KeyA|65|65|true|false") == 0) { PASS(); }
    else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(KeyboardEvent_getModifierState);
    r = js_engine_eval_string(tc->engine,
        "var e = new KeyboardEvent('keydown', { shiftKey: true });\n"
        "e.getModifierState('Shift') + '|' + e.getModifierState('Control')", "test");
    if (r && strcmp(r, "true|false") == 0) { PASS(); }
    else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(KeyboardEvent_instanceof);
    r = js_engine_eval_string(tc->engine,
        "var e = new KeyboardEvent('keydown', {});\n"
        "String(e instanceof Event) + '|' + String(e instanceof KeyboardEvent)", "test");
    if (r && strcmp(r, "true|true") == 0) { PASS(); }
    else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

static void test_js_mouse_event_class(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(setup); FAIL("engine setup"); return; }

    TEST(MouseEvent_class);
    char *r = js_engine_eval_string(tc->engine,
        "var e = new MouseEvent('mousedown', {\n"
        "    clientX: 100, clientY: 200, button: 2\n"
        "});\n"
        "e.type + '|' + e.clientX + '|' + e.clientY + '|' + e.pageX + '|'"
        " + e.pageY + '|' + e.button", "test");
    if (r && strcmp(r, "mousedown|100|200|100|200|2") == 0) { PASS(); }
    else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(MouseEvent_instanceof);
    r = js_engine_eval_string(tc->engine,
        "var e = new MouseEvent('mousedown', {});\n"
        "String(e instanceof Event) + '|' + String(e instanceof MouseEvent)", "test");
    if (r && strcmp(r, "true|true") == 0) { PASS(); }
    else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

static void test_js_wheel_event_class(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(setup); FAIL("engine setup"); return; }

    TEST(WheelEvent_class);
    char *r = js_engine_eval_string(tc->engine,
        "var e = new WheelEvent('wheel', {\n"
        "    clientX: 50, clientY: 75, deltaY: 120\n"
        "});\n"
        "e.type + '|' + e.clientX + '|' + e.clientY + '|' + e.deltaY", "test");
    if (r && strcmp(r, "wheel|50|75|120") == 0) { PASS(); }
    else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(WheelEvent_instanceof);
    r = js_engine_eval_string(tc->engine,
        "var e = new WheelEvent('wheel', {});\n"
        "String(e instanceof Event) + '|' + String(e instanceof MouseEvent)"
        " + '|' + String(e instanceof WheelEvent)", "test");
    if (r && strcmp(r, "true|true|true") == 0) { PASS(); }
    else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

static void test_js_key_event_dispatch(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(setup); FAIL("engine setup"); return; }

    /* Set up a listener in JS. */
    js_engine_eval(tc->engine,
        "var lastKeyDown = null;\n"
        "document.addEventListener('keydown', function(e) {\n"
        "    lastKeyDown = e.type + '|' + e.keyCode + '|' + e.key + '|' + e.code;\n"
        "});\n", "test");

    /* Push a keyboard event from C. */
    InputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_KEY_DOWN;
    ev.keyCode = 13;
    strncpy(ev.key, "Enter", sizeof(ev.key) - 1);
    strncpy(ev.code, "Enter", sizeof(ev.code) - 1);
    input_manager_push_event(&ev);

    /* Flush input events via JS. */
    js_engine_eval(tc->engine, "__dom_flushInputEvents();", "test");

    TEST(key_event_dispatch_via_native);
    char *r = js_engine_eval_string(tc->engine, "lastKeyDown", "test");
    if (r && strcmp(r, "keydown|13|Enter|Enter") == 0) { PASS(); }
    else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

static void test_js_mouse_event_dispatch(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(setup); FAIL("engine setup"); return; }

    js_engine_eval(tc->engine,
        "var lastMouse = null;\n"
        "document.addEventListener('mousedown', function(e) {\n"
        "    lastMouse = e.type + '|' + e.clientX + '|' + e.clientY + '|' + e.button;\n"
        "});\n", "test");

    InputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_MOUSE_DOWN;
    ev.mouseX = 300;
    ev.mouseY = 400;
    ev.button = 0;
    input_manager_push_event(&ev);

    js_engine_eval(tc->engine, "__dom_flushInputEvents();", "test");

    TEST(mouse_event_dispatch_via_native);
    char *r = js_engine_eval_string(tc->engine, "lastMouse", "test");
    if (r && strcmp(r, "mousedown|300|400|0") == 0) { PASS(); }
    else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

static void test_js_mouse_move_dispatch(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(setup); FAIL("engine setup"); return; }

    js_engine_eval(tc->engine,
        "var moveResult = null;\n"
        "document.addEventListener('mousemove', function(e) {\n"
        "    moveResult = e.type + '|' + e.pageX + '|' + e.pageY;\n"
        "});\n", "test");

    InputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_MOUSE_MOVE;
    ev.mouseX = 500;
    ev.mouseY = 300;
    input_manager_push_event(&ev);

    js_engine_eval(tc->engine, "__dom_flushInputEvents();", "test");

    TEST(mousemove_dispatch);
    char *r = js_engine_eval_string(tc->engine, "moveResult", "test");
    if (r && strcmp(r, "mousemove|500|300") == 0) { PASS(); }
    else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

static void test_js_wheel_event_dispatch(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(setup); FAIL("engine setup"); return; }

    js_engine_eval(tc->engine,
        "var lastWheel = null;\n"
        "document.addEventListener('wheel', function(e) {\n"
        "    lastWheel = e.type + '|' + e.deltaY;\n"
        "});\n", "test");

    InputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_WHEEL;
    ev.wheelDeltaY = 120.0;
    ev.mouseX = 100;
    ev.mouseY = 100;
    input_manager_push_event(&ev);

    js_engine_eval(tc->engine, "__dom_flushInputEvents();", "test");

    TEST(wheel_event_dispatch_via_native);
    char *r = js_engine_eval_string(tc->engine, "lastWheel", "test");
    if (r && strcmp(r, "wheel|120") == 0) { PASS(); }
    else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

static void test_js_multiple_events_per_frame(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(setup); FAIL("engine setup"); return; }

    js_engine_eval(tc->engine,
        "var eventLog = [];\n"
        "document.addEventListener('keydown', function(e) {\n"
        "    eventLog.push('keydown:' + e.keyCode);\n"
        "});\n"
        "document.addEventListener('keyup', function(e) {\n"
        "    eventLog.push('keyup:' + e.keyCode);\n"
        "});\n"
        "document.addEventListener('mousedown', function(e) {\n"
        "    eventLog.push('mousedown:' + e.button);\n"
        "});\n", "test");

    InputEvent ev;
    memset(&ev, 0, sizeof(ev));

    ev.type = INPUT_KEY_DOWN;
    ev.keyCode = 90;
    strncpy(ev.key, "z", sizeof(ev.key) - 1);
    strncpy(ev.code, "KeyZ", sizeof(ev.code) - 1);
    input_manager_push_event(&ev);

    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_KEY_UP;
    ev.keyCode = 90;
    strncpy(ev.key, "z", sizeof(ev.key) - 1);
    strncpy(ev.code, "KeyZ", sizeof(ev.code) - 1);
    input_manager_push_event(&ev);

    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_MOUSE_DOWN;
    ev.mouseX = 10;
    ev.mouseY = 20;
    ev.button = 0;
    input_manager_push_event(&ev);

    js_engine_eval(tc->engine, "__dom_flushInputEvents();", "test");

    TEST(multiple_events_per_frame);
    char *r = js_engine_eval_string(tc->engine, "eventLog.join(',')", "test");
    if (r && strcmp(r, "keydown:90,keyup:90,mousedown:0") == 0) { PASS(); }
    else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

static void test_js_gamepad_state(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(setup); FAIL("engine setup"); return; }

    /* Set up a gamepad via the input manager. */
    GamepadState gs;
    memset(&gs, 0, sizeof(gs));
    gs.connected = true;
    gs.index = 0;
    strncpy(gs.id, "Test Pad", sizeof(gs.id) - 1);
    gs.buttons[0].pressed = true;
    gs.buttons[0].value = 1.0;
    gs.axes[0] = 0.75;
    input_manager_set_gamepad(0, &gs);

    /* Call __dom_updateGamepads to sync to navigator. */
    js_engine_eval(tc->engine, "__dom_updateGamepads();", "test");

    TEST(gamepad_via_navigator);
    char *r = js_engine_eval_string(tc->engine,
        "var pads = navigator.getGamepads();\n"
        "var gp = pads[0];\n"
        "gp.connected + '|' + gp.id + '|' + gp.buttons[0].pressed + '|' + gp.axes[0]", "test");
    if (r && strcmp(r, "true|Test Pad|true|0.75") == 0) { PASS(); }
    else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(gamepad_disconnect);
    input_manager_clear_gamepad(0);
    js_engine_eval(tc->engine, "__dom_updateGamepads();", "test");
    r = js_engine_eval_string(tc->engine,
        "var pads = navigator.getGamepads();\n"
        "String(pads[0] === null)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); }
    else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

static void test_js_native_input_binding(void)
{
    TestCtx *tc = create_test_ctx();
    if (!tc) { TEST(setup); FAIL("engine setup"); return; }

    TEST(native_input_exists);
    char *r = js_engine_eval_string(tc->engine,
        "String(typeof __native_input === 'object' && __native_input !== null)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); }
    else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(pollEvents_returns_array);
    r = js_engine_eval_string(tc->engine,
        "var evts = __native_input.pollEvents();\n"
        "Array.isArray(evts) + '|' + evts.length", "test");
    if (r && strcmp(r, "true|0") == 0) { PASS(); }
    else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    /* Push an event and verify it comes through. */
    InputEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_KEY_DOWN;
    ev.keyCode = 38;
    strncpy(ev.key, "ArrowUp", sizeof(ev.key) - 1);
    strncpy(ev.code, "ArrowUp", sizeof(ev.code) - 1);
    ev.repeat = false;
    input_manager_push_event(&ev);

    TEST(pollEvents_returns_pushed_event);
    r = js_engine_eval_string(tc->engine,
        "var evts = __native_input.pollEvents();\n"
        "evts.length + '|' + evts[0].type + '|' + evts[0].keyCode + '|' + evts[0].key", "test");
    if (r && strcmp(r, "1|keydown|38|ArrowUp") == 0) { PASS(); }
    else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(getGamepadCount);
    r = js_engine_eval_string(tc->engine,
        "String(__native_input.getGamepadCount())", "test");
    if (r && strcmp(r, "4") == 0) { PASS(); }
    else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    TEST(getGamepad_null_when_disconnected);
    r = js_engine_eval_string(tc->engine,
        "String(__native_input.getGamepad(0) === null)", "test");
    if (r && strcmp(r, "true") == 0) { PASS(); }
    else { FAIL(r ? r : "NULL"); }
    js_engine_free_string(r);

    destroy_test_ctx(tc);
}

int main(void)
{
    printf("=== Input System Tests ===\n");

    printf("\n-- Input Manager C-level --\n");
    test_input_manager_lifecycle();
    test_input_manager_poll_clears();
    test_input_manager_multiple_events();
    test_input_manager_noop_uninitialized();
    test_input_manager_gamepad();

    printf("\n-- JS Event Classes --\n");
    test_js_keyboard_event_class();
    test_js_mouse_event_class();
    test_js_wheel_event_class();

    printf("\n-- JS Event Dispatch via Native --\n");
    test_js_key_event_dispatch();
    test_js_mouse_event_dispatch();
    test_js_mouse_move_dispatch();
    test_js_wheel_event_dispatch();
    test_js_multiple_events_per_frame();

    printf("\n-- Gamepad State --\n");
    test_js_gamepad_state();

    printf("\n-- Native Input Binding --\n");
    test_js_native_input_binding();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
