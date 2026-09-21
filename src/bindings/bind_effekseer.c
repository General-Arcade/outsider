/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#include "bindings/bind_effekseer.h"
#include "effects/effekseer_backend.h"
#include "io/file_io.h"

#include <quickjs.h>
#include <string.h>

#define PATH_BUF_SIZE 4096

/* Resolve a JS string argument to an absolute path. Returns the JS C-string
   (caller frees) or NULL with a pending exception. */
static const char *resolve_effect_path(JSContext *ctx, JSValueConst arg,
                                       char *out, size_t out_size)
{
    const char *raw = JS_ToCString(ctx, arg);
    if (!raw) return NULL;

    if (!file_io_resolve_path(raw, out, out_size)) {
        JS_ThrowTypeError(ctx, "effect path resolution failed: %s", raw);
        JS_FreeCString(ctx, raw);
        return NULL;
    }
    return raw;
}

/* init(screenWidth, screenHeight, maxSprites) -> bool */
static JSValue js_effekseer_init(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    (void)this_val;
    int32_t w = 816, h = 624, max_sprites = 8000;
    if (argc >= 1 && JS_ToInt32(ctx, &w, argv[0])) return JS_EXCEPTION;
    if (argc >= 2 && JS_ToInt32(ctx, &h, argv[1])) return JS_EXCEPTION;
    if (argc >= 3 && JS_ToInt32(ctx, &max_sprites, argv[2])) return JS_EXCEPTION;
    return JS_NewBool(ctx, effekseer_init(w, h, max_sprites));
}

/* shutdown() */
static JSValue js_effekseer_shutdown(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    effekseer_shutdown();
    return JS_UNDEFINED;
}

/* isInitialized() -> bool */
static JSValue js_effekseer_is_initialized(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return JS_NewBool(ctx, effekseer_is_initialized());
}

/* load(path, scale) -> effectHandle */
static JSValue js_effekseer_load(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "load requires a path argument");

    char resolved[PATH_BUF_SIZE];
    const char *raw = resolve_effect_path(ctx, argv[0], resolved, sizeof(resolved));
    if (!raw) {
        /* The shim checks for a 0 handle; a throw would bypass that. */
        JS_FreeValue(ctx, JS_GetException(ctx));
        return JS_NewUint32(ctx, 0);
    }
    JS_FreeCString(ctx, raw);

    double scale = 1.0;
    if (argc >= 2 && JS_ToFloat64(ctx, &scale, argv[1])) return JS_EXCEPTION;

    EffectHandle h = effekseer_load(resolved, (float)scale);
    return JS_NewUint32(ctx, h);
}

/* release(effectHandle) */
static JSValue js_effekseer_release(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;
    uint32_t handle;
    if (JS_ToUint32(ctx, &handle, argv[0])) return JS_EXCEPTION;
    effekseer_release(handle);
    return JS_UNDEFINED;
}

/* isLoaded(effectHandle) -> bool */
static JSValue js_effekseer_is_loaded(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_NewBool(ctx, 0);
    uint32_t handle;
    if (JS_ToUint32(ctx, &handle, argv[0])) return JS_EXCEPTION;
    return JS_NewBool(ctx, effekseer_is_loaded(handle));
}

/* play(effectHandle, x, y, z) -> instanceHandle */
static JSValue js_effekseer_play(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_NewInt32(ctx, EFFECT_INSTANCE_INVALID);

    uint32_t effect;
    if (JS_ToUint32(ctx, &effect, argv[0])) return JS_EXCEPTION;

    double x = 0, y = 0, z = 0;
    if (argc >= 2 && JS_ToFloat64(ctx, &x, argv[1])) return JS_EXCEPTION;
    if (argc >= 3 && JS_ToFloat64(ctx, &y, argv[2])) return JS_EXCEPTION;
    if (argc >= 4 && JS_ToFloat64(ctx, &z, argv[3])) return JS_EXCEPTION;

    EffectInstanceHandle inst = effekseer_play(effect, (float)x, (float)y, (float)z);
    return JS_NewInt32(ctx, inst);
}

/* stop(instanceHandle) */
static JSValue js_effekseer_stop(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;
    int32_t inst;
    if (JS_ToInt32(ctx, &inst, argv[0])) return JS_EXCEPTION;
    effekseer_stop(inst);
    return JS_UNDEFINED;
}

/* stopRoot(instanceHandle) */
static JSValue js_effekseer_stop_root(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;
    int32_t inst;
    if (JS_ToInt32(ctx, &inst, argv[0])) return JS_EXCEPTION;
    effekseer_stop_root(inst);
    return JS_UNDEFINED;
}

/* stopAll() */
static JSValue js_effekseer_stop_all(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    effekseer_stop_all();
    return JS_UNDEFINED;
}

/* setPosition(instanceHandle, x, y, z) */
static JSValue js_effekseer_set_position(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 4) return JS_UNDEFINED;
    int32_t inst;
    double x, y, z;
    if (JS_ToInt32(ctx, &inst, argv[0])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &x, argv[1])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &y, argv[2])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &z, argv[3])) return JS_EXCEPTION;
    effekseer_set_position(inst, (float)x, (float)y, (float)z);
    return JS_UNDEFINED;
}

/* setRotation(instanceHandle, x, y, z) */
static JSValue js_effekseer_set_rotation(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 4) return JS_UNDEFINED;
    int32_t inst;
    double x, y, z;
    if (JS_ToInt32(ctx, &inst, argv[0])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &x, argv[1])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &y, argv[2])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &z, argv[3])) return JS_EXCEPTION;
    effekseer_set_rotation(inst, (float)x, (float)y, (float)z);
    return JS_UNDEFINED;
}

/* setScale(instanceHandle, x, y, z) */
static JSValue js_effekseer_set_scale(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 4) return JS_UNDEFINED;
    int32_t inst;
    double x, y, z;
    if (JS_ToInt32(ctx, &inst, argv[0])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &x, argv[1])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &y, argv[2])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &z, argv[3])) return JS_EXCEPTION;
    effekseer_set_scale(inst, (float)x, (float)y, (float)z);
    return JS_UNDEFINED;
}

/* setSpeed(instanceHandle, speed) */
static JSValue js_effekseer_set_speed(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 2) return JS_UNDEFINED;
    int32_t inst;
    double speed;
    if (JS_ToInt32(ctx, &inst, argv[0])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &speed, argv[1])) return JS_EXCEPTION;
    effekseer_set_speed(inst, (float)speed);
    return JS_UNDEFINED;
}

/* exists(instanceHandle) -> bool */
static JSValue js_effekseer_exists(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_NewBool(ctx, 0);
    int32_t inst;
    if (JS_ToInt32(ctx, &inst, argv[0])) return JS_EXCEPTION;
    return JS_NewBool(ctx, effekseer_exists(inst));
}

/* setProjectionMatrix(array) - expects a 16-element JS array */
static JSValue js_effekseer_set_projection_matrix(JSContext *ctx, JSValueConst this_val,
                                                    int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1 || !JS_IsArray(argv[0]))
        return JS_ThrowTypeError(ctx, "setProjectionMatrix expects a 16-element array");

    float m[16];
    for (int i = 0; i < 16; i++) {
        JSValue elem = JS_GetPropertyUint32(ctx, argv[0], (uint32_t)i);
        double v = 0;
        int rc = JS_ToFloat64(ctx, &v, elem);
        JS_FreeValue(ctx, elem);
        if (rc) return JS_EXCEPTION;
        m[i] = (float)v;
    }
    effekseer_set_projection_matrix(m);
    return JS_UNDEFINED;
}

/* setCameraMatrix(array) - expects a 16-element JS array */
static JSValue js_effekseer_set_camera_matrix(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1 || !JS_IsArray(argv[0]))
        return JS_ThrowTypeError(ctx, "setCameraMatrix expects a 16-element array");

    float m[16];
    for (int i = 0; i < 16; i++) {
        JSValue elem = JS_GetPropertyUint32(ctx, argv[0], (uint32_t)i);
        double v = 0;
        int rc = JS_ToFloat64(ctx, &v, elem);
        JS_FreeValue(ctx, elem);
        if (rc) return JS_EXCEPTION;
        m[i] = (float)v;
    }
    effekseer_set_camera_matrix(m);
    return JS_UNDEFINED;
}

/* update(deltaFrames) */
static JSValue js_effekseer_update(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    (void)this_val;
    double delta = 1.0;
    if (argc >= 1 && JS_ToFloat64(ctx, &delta, argv[0])) return JS_EXCEPTION;
    effekseer_update((float)delta);
    return JS_UNDEFINED;
}

/* beginDraw() */
static JSValue js_effekseer_begin_draw(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    effekseer_begin_draw();
    return JS_UNDEFINED;
}

/* drawHandle(instanceHandle) */
static JSValue js_effekseer_draw_handle(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;
    int32_t inst;
    if (JS_ToInt32(ctx, &inst, argv[0])) return JS_EXCEPTION;
    effekseer_draw_handle(inst);
    return JS_UNDEFINED;
}

/* endDraw() */
static JSValue js_effekseer_end_draw(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    effekseer_end_draw();
    return JS_UNDEFINED;
}

/* setRestorationOfStatesFlag(flag) */
static JSValue js_effekseer_set_restoration_flag(JSContext *ctx, JSValueConst this_val,
                                                   int argc, JSValueConst *argv)
{
    (void)this_val;
    bool flag = true;
    if (argc >= 1) {
        int val = JS_ToBool(ctx, argv[0]);
        if (val < 0) return JS_EXCEPTION;
        flag = val != 0;
    }
    effekseer_set_restoration_of_states_flag(flag);
    return JS_UNDEFINED;
}

/* setBackground(glTexture) */
static JSValue js_effekseer_set_background(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv)
{
    (void)this_val;
    uint32_t tex = 0;
    if (argc >= 1 && JS_ToUint32(ctx, &tex, argv[0])) return JS_EXCEPTION;
    effekseer_set_background(tex);
    return JS_UNDEFINED;
}

/* resetBackground() */
static JSValue js_effekseer_reset_background(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv)
{
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    effekseer_reset_background();
    return JS_UNDEFINED;
}

/* getLoadedCount() -> int */
static JSValue js_effekseer_get_loaded_count(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return JS_NewInt32(ctx, effekseer_get_loaded_count());
}

/* getPlayingCount() -> int */
static JSValue js_effekseer_get_playing_count(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return JS_NewInt32(ctx, effekseer_get_playing_count());
}

/* Registration */

static const JSCFunctionListEntry js_effekseer_funcs[] = {
    JS_CFUNC_DEF("init", 3, js_effekseer_init),
    JS_CFUNC_DEF("shutdown", 0, js_effekseer_shutdown),
    JS_CFUNC_DEF("isInitialized", 0, js_effekseer_is_initialized),
    JS_CFUNC_DEF("load", 2, js_effekseer_load),
    JS_CFUNC_DEF("release", 1, js_effekseer_release),
    JS_CFUNC_DEF("isLoaded", 1, js_effekseer_is_loaded),
    JS_CFUNC_DEF("play", 4, js_effekseer_play),
    JS_CFUNC_DEF("stop", 1, js_effekseer_stop),
    JS_CFUNC_DEF("stopRoot", 1, js_effekseer_stop_root),
    JS_CFUNC_DEF("stopAll", 0, js_effekseer_stop_all),
    JS_CFUNC_DEF("setPosition", 4, js_effekseer_set_position),
    JS_CFUNC_DEF("setRotation", 4, js_effekseer_set_rotation),
    JS_CFUNC_DEF("setScale", 4, js_effekseer_set_scale),
    JS_CFUNC_DEF("setSpeed", 2, js_effekseer_set_speed),
    JS_CFUNC_DEF("exists", 1, js_effekseer_exists),
    JS_CFUNC_DEF("setProjectionMatrix", 1, js_effekseer_set_projection_matrix),
    JS_CFUNC_DEF("setCameraMatrix", 1, js_effekseer_set_camera_matrix),
    JS_CFUNC_DEF("update", 1, js_effekseer_update),
    JS_CFUNC_DEF("beginDraw", 0, js_effekseer_begin_draw),
    JS_CFUNC_DEF("drawHandle", 1, js_effekseer_draw_handle),
    JS_CFUNC_DEF("endDraw", 0, js_effekseer_end_draw),
    JS_CFUNC_DEF("setRestorationOfStatesFlag", 1, js_effekseer_set_restoration_flag),
    JS_CFUNC_DEF("setBackground", 1, js_effekseer_set_background),
    JS_CFUNC_DEF("resetBackground", 0, js_effekseer_reset_background),
    JS_CFUNC_DEF("getLoadedCount", 0, js_effekseer_get_loaded_count),
    JS_CFUNC_DEF("getPlayingCount", 0, js_effekseer_get_playing_count),
};

void bind_effekseer_register(JSContext *ctx)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, obj, js_effekseer_funcs,
                               sizeof(js_effekseer_funcs) / sizeof(js_effekseer_funcs[0]));
    JS_SetPropertyStr(ctx, global, "__native_effekseer", obj);
    JS_FreeValue(ctx, global);
}
