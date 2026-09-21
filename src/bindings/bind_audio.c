/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#include "bindings/bind_audio.h"
#include "audio/audio_engine.h"
#include "io/file_io.h"

#include <quickjs.h>
#include <string.h>

#define PATH_BUF_SIZE 4096

/* Resolve a JS string argument to an absolute path. Returns the JS C-string
   (caller frees) or NULL with a pending exception. */
static const char *resolve_audio_path(JSContext *ctx, JSValueConst arg,
                                       char *out, size_t out_size)
{
    const char *raw = JS_ToCString(ctx, arg);
    if (!raw) return NULL;

    if (!file_io_resolve_path(raw, out, out_size)) {
        JS_ThrowTypeError(ctx, "audio path resolution failed: %s", raw);
        JS_FreeCString(ctx, raw);
        return NULL;
    }
    return raw;
}

/* init() -> bool */
static JSValue js_audio_init(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return JS_NewBool(ctx, audio_engine_init());
}

/* shutdown() */
static JSValue js_audio_shutdown(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    audio_engine_shutdown();
    return JS_UNDEFINED;
}

/* isInitialized() -> bool */
static JSValue js_audio_is_initialized(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return JS_NewBool(ctx, audio_engine_is_initialized());
}

/* load(path) -> sourceHandle */
static JSValue js_audio_load(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "load requires a path argument");

    char resolved[PATH_BUF_SIZE];
    const char *raw = resolve_audio_path(ctx, argv[0], resolved, sizeof(resolved));
    if (!raw) return JS_EXCEPTION;
    JS_FreeCString(ctx, raw);

    AudioSourceHandle h = audio_load(resolved);
    return JS_NewUint32(ctx, h);
}

/* loadStream(path) -> sourceHandle */
static JSValue js_audio_load_stream(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "loadStream requires a path argument");

    char resolved[PATH_BUF_SIZE];
    const char *raw = resolve_audio_path(ctx, argv[0], resolved, sizeof(resolved));
    if (!raw) return JS_EXCEPTION;
    JS_FreeCString(ctx, raw);

    AudioSourceHandle h = audio_load_stream(resolved);
    return JS_NewUint32(ctx, h);
}

/* loadFromMemory(arrayBuffer) -> sourceHandle */
static JSValue js_audio_load_from_memory(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "loadFromMemory requires an ArrayBuffer");

    size_t size = 0;
    uint8_t *data = JS_GetArrayBuffer(ctx, &size, argv[0]);
    if (!data) return JS_ThrowTypeError(ctx, "argument must be an ArrayBuffer");

    if (size > UINT32_MAX) return JS_ThrowRangeError(ctx, "audio buffer too large");
    AudioSourceHandle h = audio_load_from_memory(data, (uint32_t)size);
    return JS_NewUint32(ctx, h);
}

/* freeSource(sourceHandle) */
static JSValue js_audio_free_source(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;
    uint32_t h;
    if (JS_ToUint32(ctx, &h, argv[0])) return JS_EXCEPTION;
    audio_source_free(h);
    return JS_UNDEFINED;
}

/* play(source, bus, volume, pitch, pan, loop) -> voiceHandle */
static JSValue js_audio_play(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 6) return JS_ThrowTypeError(ctx, "play requires 6 arguments");

    uint32_t source;
    int32_t bus;
    double volume, pitch, pan;
    int loop;

    if (JS_ToUint32(ctx, &source, argv[0])) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &bus, argv[1])) return JS_EXCEPTION;
    if (bus < 0 || bus >= AUDIO_BUS_COUNT)
        return JS_ThrowRangeError(ctx, "invalid audio bus: %d", bus);
    if (JS_ToFloat64(ctx, &volume, argv[2])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &pitch, argv[3])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &pan, argv[4])) return JS_EXCEPTION;
    loop = JS_ToBool(ctx, argv[5]);
    if (loop < 0) return JS_EXCEPTION;

    AudioVoice voice = audio_play(source, (AudioBus)bus, (float)volume,
                                   (float)pitch, (float)pan, loop != 0);
    return JS_NewUint32(ctx, voice);
}

/* stop(voiceHandle) */
static JSValue js_audio_stop(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;
    uint32_t v;
    if (JS_ToUint32(ctx, &v, argv[0])) return JS_EXCEPTION;
    audio_stop(v);
    return JS_UNDEFINED;
}

/* stopBus(bus) */
static JSValue js_audio_stop_bus(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;
    int32_t bus;
    if (JS_ToInt32(ctx, &bus, argv[0])) return JS_EXCEPTION;
    if (bus < 0 || bus >= AUDIO_BUS_COUNT)
        return JS_ThrowRangeError(ctx, "invalid audio bus: %d", bus);
    audio_stop_bus((AudioBus)bus);
    return JS_UNDEFINED;
}

/* stopAll() */
static JSValue js_audio_stop_all(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    audio_stop_all();
    return JS_UNDEFINED;
}

/* pause(voiceHandle) */
static JSValue js_audio_pause(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;
    uint32_t v;
    if (JS_ToUint32(ctx, &v, argv[0])) return JS_EXCEPTION;
    audio_pause(v);
    return JS_UNDEFINED;
}

/* resume(voiceHandle) */
static JSValue js_audio_resume(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;
    uint32_t v;
    if (JS_ToUint32(ctx, &v, argv[0])) return JS_EXCEPTION;
    audio_resume(v);
    return JS_UNDEFINED;
}

/* pauseBus(bus) / resumeBus(bus) */
static JSValue js_audio_pause_bus(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;
    int32_t bus;
    if (JS_ToInt32(ctx, &bus, argv[0])) return JS_EXCEPTION;
    if (bus < 0 || bus >= AUDIO_BUS_COUNT)
        return JS_ThrowRangeError(ctx, "invalid audio bus: %d", bus);
    audio_pause_bus((AudioBus)bus);
    return JS_UNDEFINED;
}

static JSValue js_audio_resume_bus(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;
    int32_t bus;
    if (JS_ToInt32(ctx, &bus, argv[0])) return JS_EXCEPTION;
    if (bus < 0 || bus >= AUDIO_BUS_COUNT)
        return JS_ThrowRangeError(ctx, "invalid audio bus: %d", bus);
    audio_resume_bus((AudioBus)bus);
    return JS_UNDEFINED;
}

/* setVolume(voice, volume) / setPitch / setPan */
static JSValue js_audio_set_volume(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 2) return JS_UNDEFINED;
    uint32_t v; double vol;
    if (JS_ToUint32(ctx, &v, argv[0])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &vol, argv[1])) return JS_EXCEPTION;
    audio_set_volume(v, (float)vol);
    return JS_UNDEFINED;
}

static JSValue js_audio_set_pitch(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 2) return JS_UNDEFINED;
    uint32_t v; double p;
    if (JS_ToUint32(ctx, &v, argv[0])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &p, argv[1])) return JS_EXCEPTION;
    audio_set_pitch(v, (float)p);
    return JS_UNDEFINED;
}

static JSValue js_audio_set_pan(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 2) return JS_UNDEFINED;
    uint32_t v; double pan;
    if (JS_ToUint32(ctx, &v, argv[0])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &pan, argv[1])) return JS_EXCEPTION;
    audio_set_pan(v, (float)pan);
    return JS_UNDEFINED;
}

/* setBusVolume(bus, volume) */
static JSValue js_audio_set_bus_volume(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 2) return JS_UNDEFINED;
    int32_t bus; double vol;
    if (JS_ToInt32(ctx, &bus, argv[0])) return JS_EXCEPTION;
    if (bus < 0 || bus >= AUDIO_BUS_COUNT)
        return JS_ThrowRangeError(ctx, "invalid audio bus: %d", bus);
    if (JS_ToFloat64(ctx, &vol, argv[1])) return JS_EXCEPTION;
    audio_set_bus_volume((AudioBus)bus, (float)vol);
    return JS_UNDEFINED;
}

/* fadeVolume(voice, target, duration) */
static JSValue js_audio_fade_volume(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 3) return JS_UNDEFINED;
    uint32_t v; double target, dur;
    if (JS_ToUint32(ctx, &v, argv[0])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &target, argv[1])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &dur, argv[2])) return JS_EXCEPTION;
    audio_fade_volume(v, (float)target, (float)dur);
    return JS_UNDEFINED;
}

/* fadeBusVolume(bus, target, duration) */
static JSValue js_audio_fade_bus_volume(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 3) return JS_UNDEFINED;
    int32_t bus; double target, dur;
    if (JS_ToInt32(ctx, &bus, argv[0])) return JS_EXCEPTION;
    if (bus < 0 || bus >= AUDIO_BUS_COUNT)
        return JS_ThrowRangeError(ctx, "invalid audio bus: %d", bus);
    if (JS_ToFloat64(ctx, &target, argv[1])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &dur, argv[2])) return JS_EXCEPTION;
    audio_fade_bus_volume((AudioBus)bus, (float)target, (float)dur);
    return JS_UNDEFINED;
}

/* isPlaying(voice) -> bool */
static JSValue js_audio_is_playing(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_FALSE;
    uint32_t v;
    if (JS_ToUint32(ctx, &v, argv[0])) return JS_EXCEPTION;
    return JS_NewBool(ctx, audio_is_playing(v));
}

/* isPaused(voice) -> bool */
static JSValue js_audio_is_paused(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_FALSE;
    uint32_t v;
    if (JS_ToUint32(ctx, &v, argv[0])) return JS_EXCEPTION;
    return JS_NewBool(ctx, audio_is_paused(v));
}

/* getVolume(voice) -> float */
static JSValue js_audio_get_volume(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_NewFloat64(ctx, 0.0);
    uint32_t v;
    if (JS_ToUint32(ctx, &v, argv[0])) return JS_EXCEPTION;
    return JS_NewFloat64(ctx, audio_get_volume(v));
}

/* getPosition(voice) -> float */
static JSValue js_audio_get_position(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_NewFloat64(ctx, 0.0);
    uint32_t v;
    if (JS_ToUint32(ctx, &v, argv[0])) return JS_EXCEPTION;
    return JS_NewFloat64(ctx, audio_get_position(v));
}

/* seek(voice, position) */
static JSValue js_audio_seek(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 2) return JS_UNDEFINED;
    uint32_t v; double pos;
    if (JS_ToUint32(ctx, &v, argv[0])) return JS_EXCEPTION;
    if (JS_ToFloat64(ctx, &pos, argv[1])) return JS_EXCEPTION;
    audio_seek(v, pos);
    return JS_UNDEFINED;
}

/* getDuration(sourceHandle) -> float */
static JSValue js_audio_get_duration(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_NewFloat64(ctx, 0.0);
    uint32_t h;
    if (JS_ToUint32(ctx, &h, argv[0])) return JS_EXCEPTION;
    return JS_NewFloat64(ctx, audio_get_duration(h));
}

/* getActiveVoiceCount() -> int */
static JSValue js_audio_get_active_voice_count(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return JS_NewInt32(ctx, audio_get_active_voice_count());
}

/* Registration */

static const JSCFunctionListEntry js_audio_funcs[] = {
    JS_CFUNC_DEF("init",                0, js_audio_init),
    JS_CFUNC_DEF("shutdown",            0, js_audio_shutdown),
    JS_CFUNC_DEF("isInitialized",       0, js_audio_is_initialized),
    JS_CFUNC_DEF("load",                1, js_audio_load),
    JS_CFUNC_DEF("loadStream",          1, js_audio_load_stream),
    JS_CFUNC_DEF("loadFromMemory",      1, js_audio_load_from_memory),
    JS_CFUNC_DEF("freeSource",          1, js_audio_free_source),
    JS_CFUNC_DEF("play",                6, js_audio_play),
    JS_CFUNC_DEF("stop",                1, js_audio_stop),
    JS_CFUNC_DEF("stopBus",             1, js_audio_stop_bus),
    JS_CFUNC_DEF("stopAll",             0, js_audio_stop_all),
    JS_CFUNC_DEF("pause",               1, js_audio_pause),
    JS_CFUNC_DEF("resume",              1, js_audio_resume),
    JS_CFUNC_DEF("pauseBus",            1, js_audio_pause_bus),
    JS_CFUNC_DEF("resumeBus",           1, js_audio_resume_bus),
    JS_CFUNC_DEF("setVolume",           2, js_audio_set_volume),
    JS_CFUNC_DEF("setPitch",            2, js_audio_set_pitch),
    JS_CFUNC_DEF("setPan",              2, js_audio_set_pan),
    JS_CFUNC_DEF("setBusVolume",        2, js_audio_set_bus_volume),
    JS_CFUNC_DEF("fadeVolume",          3, js_audio_fade_volume),
    JS_CFUNC_DEF("fadeBusVolume",       3, js_audio_fade_bus_volume),
    JS_CFUNC_DEF("isPlaying",           1, js_audio_is_playing),
    JS_CFUNC_DEF("isPaused",            1, js_audio_is_paused),
    JS_CFUNC_DEF("getVolume",           1, js_audio_get_volume),
    JS_CFUNC_DEF("getPosition",         1, js_audio_get_position),
    JS_CFUNC_DEF("seek",                2, js_audio_seek),
    JS_CFUNC_DEF("getDuration",         1, js_audio_get_duration),
    JS_CFUNC_DEF("getActiveVoiceCount", 0, js_audio_get_active_voice_count),
};

void bind_audio_register(JSContext *ctx)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue audio_obj = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, audio_obj, js_audio_funcs,
                               sizeof(js_audio_funcs) / sizeof(js_audio_funcs[0]));

    /* Export bus constants. */
    JS_SetPropertyStr(ctx, audio_obj, "BUS_BGM", JS_NewInt32(ctx, AUDIO_BUS_BGM));
    JS_SetPropertyStr(ctx, audio_obj, "BUS_BGS", JS_NewInt32(ctx, AUDIO_BUS_BGS));
    JS_SetPropertyStr(ctx, audio_obj, "BUS_ME",  JS_NewInt32(ctx, AUDIO_BUS_ME));
    JS_SetPropertyStr(ctx, audio_obj, "BUS_SE",  JS_NewInt32(ctx, AUDIO_BUS_SE));

    JS_SetPropertyStr(ctx, global, "__native_audio", audio_obj);
    JS_FreeValue(ctx, global);
}
