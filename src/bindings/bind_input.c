/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#include "bindings/bind_input.h"
#include "input/input_manager.h"

#include <quickjs.h>
#include <string.h>

/* Build a DOM-style JS event object from an InputEvent. */

static JSValue input_event_to_js(JSContext *ctx, const InputEvent *ev)
{
    JSValue obj = JS_NewObject(ctx);

    /* Common: event type string */
    const char *type_str;
    switch (ev->type) {
        case INPUT_KEY_DOWN:   type_str = "keydown";     break;
        case INPUT_KEY_UP:     type_str = "keyup";       break;
        case INPUT_MOUSE_DOWN: type_str = "mousedown";   break;
        case INPUT_MOUSE_UP:   type_str = "mouseup";     break;
        case INPUT_MOUSE_MOVE: type_str = "mousemove";   break;
        case INPUT_WHEEL:      type_str = "wheel";       break;
        case INPUT_TEXT:       type_str = "textinput";   break;
        default:               type_str = "unknown";     break;
    }
    JS_SetPropertyStr(ctx, obj, "type", JS_NewString(ctx, type_str));

    if (ev->type == INPUT_TEXT) {
        JS_SetPropertyStr(ctx, obj, "text", JS_NewString(ctx, ev->key));
    }

    /* Keyboard fields */
    if (ev->type == INPUT_KEY_DOWN || ev->type == INPUT_KEY_UP) {
        JS_SetPropertyStr(ctx, obj, "keyCode",  JS_NewInt32(ctx, ev->keyCode));
        JS_SetPropertyStr(ctx, obj, "key",      JS_NewString(ctx, ev->key));
        JS_SetPropertyStr(ctx, obj, "code",     JS_NewString(ctx, ev->code));
        JS_SetPropertyStr(ctx, obj, "repeat",   JS_NewBool(ctx, ev->repeat));
        JS_SetPropertyStr(ctx, obj, "shiftKey", JS_NewBool(ctx, ev->shiftKey));
        JS_SetPropertyStr(ctx, obj, "ctrlKey",  JS_NewBool(ctx, ev->ctrlKey));
        JS_SetPropertyStr(ctx, obj, "altKey",   JS_NewBool(ctx, ev->altKey));
        JS_SetPropertyStr(ctx, obj, "metaKey",  JS_NewBool(ctx, ev->metaKey));
    }

    /* Mouse fields */
    if (ev->type == INPUT_MOUSE_DOWN || ev->type == INPUT_MOUSE_UP ||
        ev->type == INPUT_MOUSE_MOVE || ev->type == INPUT_WHEEL) {
        JS_SetPropertyStr(ctx, obj, "clientX", JS_NewInt32(ctx, ev->mouseX));
        JS_SetPropertyStr(ctx, obj, "clientY", JS_NewInt32(ctx, ev->mouseY));
        JS_SetPropertyStr(ctx, obj, "pageX",   JS_NewInt32(ctx, ev->mouseX));
        JS_SetPropertyStr(ctx, obj, "pageY",   JS_NewInt32(ctx, ev->mouseY));
        JS_SetPropertyStr(ctx, obj, "screenX", JS_NewInt32(ctx, ev->mouseX));
        JS_SetPropertyStr(ctx, obj, "screenY", JS_NewInt32(ctx, ev->mouseY));
        JS_SetPropertyStr(ctx, obj, "button",  JS_NewInt32(ctx, ev->button));
    }

    /* Wheel fields */
    if (ev->type == INPUT_WHEEL) {
        JS_SetPropertyStr(ctx, obj, "deltaX", JS_NewFloat64(ctx, ev->wheelDeltaX));
        JS_SetPropertyStr(ctx, obj, "deltaY", JS_NewFloat64(ctx, ev->wheelDeltaY));
        JS_SetPropertyStr(ctx, obj, "deltaZ", JS_NewFloat64(ctx, 0.0));
        JS_SetPropertyStr(ctx, obj, "deltaMode", JS_NewInt32(ctx, 0));
    }

    return obj;
}

/* pollEvents() -> Array of event objects */

static JSValue js_input_poll_events(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;

    InputEvent events[INPUT_MAX_EVENTS];
    int count = input_manager_poll_events(events, INPUT_MAX_EVENTS);

    JSValue arr = JS_NewArray(ctx);
    for (int i = 0; i < count; i++) {
        JS_SetPropertyUint32(ctx, arr, (uint32_t)i,
                             input_event_to_js(ctx, &events[i]));
    }
    return arr;
}

/* getGamepad(index) -> Gamepad-like object | null */

static JSValue js_input_get_gamepad(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_NULL;

    int32_t index;
    if (JS_ToInt32(ctx, &index, argv[0]))
        return JS_EXCEPTION;

    const GamepadState *gp = input_manager_get_gamepad(index);
    if (!gp || !gp->connected) return JS_NULL;

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "id",        JS_NewString(ctx, gp->id));
    JS_SetPropertyStr(ctx, obj, "index",     JS_NewInt32(ctx, gp->index));
    JS_SetPropertyStr(ctx, obj, "connected", JS_NewBool(ctx, true));
    JS_SetPropertyStr(ctx, obj, "mapping",   JS_NewString(ctx, "standard"));
    JS_SetPropertyStr(ctx, obj, "timestamp", JS_NewFloat64(ctx, 0.0));

    /* Buttons array: each element has { pressed, value } */
    JSValue buttons = JS_NewArray(ctx);
    for (int i = 0; i < INPUT_GAMEPAD_BUTTONS; i++) {
        JSValue btn = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, btn, "pressed",
                          JS_NewBool(ctx, gp->buttons[i].pressed));
        JS_SetPropertyStr(ctx, btn, "value",
                          JS_NewFloat64(ctx, gp->buttons[i].value));
        JS_SetPropertyStr(ctx, btn, "touched",
                          JS_NewBool(ctx, gp->buttons[i].pressed));
        JS_SetPropertyUint32(ctx, buttons, (uint32_t)i, btn);
    }
    JS_SetPropertyStr(ctx, obj, "buttons", buttons);

    /* Axes array */
    JSValue axes = JS_NewArray(ctx);
    for (int i = 0; i < INPUT_GAMEPAD_AXES; i++) {
        JS_SetPropertyUint32(ctx, axes, (uint32_t)i,
                             JS_NewFloat64(ctx, gp->axes[i]));
    }
    JS_SetPropertyStr(ctx, obj, "axes", axes);

    return obj;
}

/* getGamepadCount() -> int (number of gamepad slots) */

static JSValue js_input_get_gamepad_count(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return JS_NewInt32(ctx, INPUT_MAX_GAMEPADS);
}

/* Registration */

static const JSCFunctionListEntry js_input_funcs[] = {
    JS_CFUNC_DEF("pollEvents",      0, js_input_poll_events),
    JS_CFUNC_DEF("getGamepad",      1, js_input_get_gamepad),
    JS_CFUNC_DEF("getGamepadCount", 0, js_input_get_gamepad_count),
};

void bind_input_register(JSContext *ctx)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue input_obj = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, input_obj, js_input_funcs,
                               sizeof(js_input_funcs) / sizeof(js_input_funcs[0]));

    JS_SetPropertyStr(ctx, global, "__native_input", input_obj);
    JS_FreeValue(ctx, global);
}
