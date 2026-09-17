/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#include "engine/js_engine.h"

#include <quickjs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* 256 MB memory limit for game runtime. */
#define JS_MEMORY_LIMIT (256 * 1024 * 1024)
/* JS stack limit. Must stay well below the real thread stack so QuickJS
   throws a catchable "stack overflow" (some plugins rely on catching it)
   instead of the process faulting: Linux main threads default to 8 MB and
   the Windows build links with 16 MB (see CMakeLists.txt). */
#define JS_STACK_SIZE   (4 * 1024 * 1024)

struct JSEngine {
    JSRuntime  *rt;
    JSContext  *ctx;
    JSConsoleCallback console_cb;
    void              *console_userdata;

    /* Pending unhandled rejections. QuickJS reports a rejection immediately and
       retracts it if a handler is attached later, so entries are held here and
       reported only after each job drain (when a browser fires unhandledrejection). */
    JSValue *rej_promises;
    JSValue *rej_reasons;
    int      rej_count;
    int      rej_cap;

    /* Script watchdog (debug aid, 0 = off): interrupt JS that runs longer
       than watchdog_ms since the last arm. */
    unsigned watchdog_ms;
    uint64_t watchdog_armed_ms;
};

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* Called periodically by QuickJS while JS runs. Returning 1 raises an
   uncatchable InternalError, so the stack trace shows where the script was. */
static int watchdog_interrupt(JSRuntime *rt, void *opaque)
{
    (void)rt;
    JSEngine *engine = opaque;
    if (!engine->watchdog_ms) return 0;
    uint64_t now = monotonic_ms();
    if (now - engine->watchdog_armed_ms > engine->watchdog_ms) {
        fprintf(stderr, "js_engine: watchdog: script ran longer than %u ms, interrupting\n",
                engine->watchdog_ms);
        engine->watchdog_armed_ms = now;   /* one interruption per overrun */
        return 1;
    }
    return 0;
}

void js_engine_set_watchdog(JSEngine *engine, unsigned limit_ms)
{
    if (!engine) return;
    engine->watchdog_ms = limit_ms;
    engine->watchdog_armed_ms = monotonic_ms();
    JS_SetInterruptHandler(engine->rt, limit_ms ? watchdog_interrupt : NULL, engine);
}

void js_engine_watchdog_arm(JSEngine *engine)
{
    if (engine && engine->watchdog_ms) engine->watchdog_armed_ms = monotonic_ms();
}

/* console.log / console.warn / console.error; `magic` selects the level (0/1/2). */
static JSValue js_console_output(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv, int magic)
{
    (void)this_val;

    static const char *levels[] = { "log", "warn", "error" };
    const char *level = levels[magic < 0 || magic > 2 ? 0 : magic];

    JSEngine *engine = JS_GetContextOpaque(ctx);

    size_t total_len = 0;
    const char **parts = NULL;
    size_t *part_lens = NULL;

    if (argc > 0) {
        parts = malloc(sizeof(char *) * (size_t)argc);
        part_lens = malloc(sizeof(size_t) * (size_t)argc);
        if (!parts || !part_lens) {
            free(parts);
            free(part_lens);
            return JS_UNDEFINED;
        }
    }

    for (int i = 0; i < argc; i++) {
        size_t len = 0;
        parts[i] = JS_ToCStringLen(ctx, &len, argv[i]);
        if (!parts[i]) {
            /* toString threw; clear the pending exception so console.* never throws. */
            JS_FreeValue(ctx, JS_GetException(ctx));
        }
        part_lens[i] = len;
        total_len += len;
        if (i > 0) total_len++;
    }

    char *msg = malloc(total_len + 1);
    if (msg) {
        size_t pos = 0;
        for (int i = 0; i < argc; i++) {
            if (i > 0) msg[pos++] = ' ';
            if (parts[i]) {
                memcpy(msg + pos, parts[i], part_lens[i]);
                pos += part_lens[i];
            }
        }
        msg[pos] = '\0';
    }

    if (engine && engine->console_cb) {
        engine->console_cb(level, msg ? msg : "", engine->console_userdata);
    } else {
        FILE *out = (magic == 2) ? stderr : stdout;
        if (magic == 1)
            fprintf(out, "[WARN] %s\n", msg ? msg : "");
        else if (magic == 2)
            fprintf(out, "[ERROR] %s\n", msg ? msg : "");
        else
            fprintf(out, "%s\n", msg ? msg : "");
    }

    free(msg);
    for (int i = 0; i < argc; i++) {
        JS_FreeCString(ctx, parts[i]);
    }
    free(parts);
    free(part_lens);

    return JS_UNDEFINED;
}

static void js_register_console(JSContext *ctx)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue console = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, console, "log",
        JS_NewCFunctionMagic(ctx, js_console_output, "log", 0,
                             JS_CFUNC_generic_magic, 0));
    JS_SetPropertyStr(ctx, console, "warn",
        JS_NewCFunctionMagic(ctx, js_console_output, "warn", 0,
                             JS_CFUNC_generic_magic, 1));
    JS_SetPropertyStr(ctx, console, "error",
        JS_NewCFunctionMagic(ctx, js_console_output, "error", 0,
                             JS_CFUNC_generic_magic, 2));

    JS_SetPropertyStr(ctx, global, "console", console);
    JS_FreeValue(ctx, global);
}

/* Print the pending exception and stack to stderr, or to the console callback. */
static void js_dump_exception(JSEngine *engine, JSContext *ctx)
{
    JSValue exc = JS_GetException(ctx);

    const char *str = JS_ToCString(ctx, exc);
    if (str) {
        if (engine && engine->console_cb) {
            engine->console_cb("error", str, engine->console_userdata);
        } else {
            fprintf(stderr, "JS Exception: %s\n", str);
        }
        JS_FreeCString(ctx, str);
    }

    if (JS_IsError(ctx, exc)) {
        JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
        if (!JS_IsUndefined(stack)) {
            const char *stack_str = JS_ToCString(ctx, stack);
            if (stack_str) {
                if (engine && engine->console_cb) {
                    engine->console_cb("error", stack_str, engine->console_userdata);
                } else {
                    fprintf(stderr, "Stack:\n%s\n", stack_str);
                }
                JS_FreeCString(ctx, stack_str);
            }
        }
        JS_FreeValue(ctx, stack);
    }

    JS_FreeValue(ctx, exc);
}

/* Unhandled promise rejection tracking */

static void promise_rejection_tracker(JSContext *ctx, JSValueConst promise,
                                      JSValueConst reason, bool is_handled,
                                      void *opaque)
{
    JSEngine *engine = opaque;
    if (!engine) return;

    if (is_handled) {
        /* A handler was attached after the fact — retract the pending report. */
        for (int i = 0; i < engine->rej_count; i++) {
            if (JS_VALUE_GET_PTR(engine->rej_promises[i]) == JS_VALUE_GET_PTR(promise)) {
                JS_FreeValue(ctx, engine->rej_promises[i]);
                JS_FreeValue(ctx, engine->rej_reasons[i]);
                engine->rej_count--;
                engine->rej_promises[i] = engine->rej_promises[engine->rej_count];
                engine->rej_reasons[i]  = engine->rej_reasons[engine->rej_count];
                return;
            }
        }
        return;
    }

    if (engine->rej_count >= engine->rej_cap) {
        int ncap = engine->rej_cap ? engine->rej_cap * 2 : 8;
        JSValue *np = realloc(engine->rej_promises, sizeof(JSValue) * (size_t)ncap);
        if (!np) return;
        engine->rej_promises = np;
        JSValue *nr = realloc(engine->rej_reasons, sizeof(JSValue) * (size_t)ncap);
        if (!nr) return;                 /* rej_cap unchanged, so the larger array is harmless */
        engine->rej_reasons = nr;
        engine->rej_cap = ncap;
    }
    engine->rej_promises[engine->rej_count] = JS_DupValue(ctx, promise);
    engine->rej_reasons[engine->rej_count]  = JS_DupValue(ctx, reason);
    engine->rej_count++;
}

/* Report every rejection still unhandled via the shim's
   __error_handler_dispatchRejection, so the game's `unhandledrejection` listener fires. */
static void flush_unhandled_rejections(JSEngine *engine)
{
    if (!engine || engine->rej_count == 0) return;
    JSContext *ctx = engine->ctx;

    /* Detach the list first: dispatching may reject further promises. */
    int n = engine->rej_count;
    JSValue *promises = engine->rej_promises;
    JSValue *reasons  = engine->rej_reasons;
    engine->rej_promises = NULL;
    engine->rej_reasons  = NULL;
    engine->rej_count = 0;
    engine->rej_cap   = 0;

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, global, "__error_handler_dispatchRejection");
    bool have_fn = JS_IsFunction(ctx, fn);

    for (int i = 0; i < n; i++) {
        if (have_fn) {
            JSValue ret = JS_Call(ctx, fn, global, 1, &reasons[i]);
            if (JS_IsException(ret)) js_dump_exception(engine, ctx);
            JS_FreeValue(ctx, ret);
        } else {
            const char *s = JS_ToCString(ctx, reasons[i]);
            if (!s) {
                JS_FreeValue(ctx, JS_GetException(ctx));
                s = "<unprintable reason>";
            }
            if (engine->console_cb) {
                char msg[512];
                snprintf(msg, sizeof(msg), "Unhandled promise rejection: %s", s);
                engine->console_cb("error", msg, engine->console_userdata);
            } else {
                fprintf(stderr, "[ERROR] Unhandled promise rejection: %s\n", s);
            }
            if (strcmp(s, "<unprintable reason>") != 0) JS_FreeCString(ctx, s);
        }
        JS_FreeValue(ctx, promises[i]);
        JS_FreeValue(ctx, reasons[i]);
    }

    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, global);
    free(promises);
    free(reasons);
}

JSEngine *js_engine_init(void)
{
    JSRuntime *rt = JS_NewRuntime();
    if (!rt) {
        fprintf(stderr, "Failed to create JS runtime\n");
        return NULL;
    }

    JS_SetMemoryLimit(rt, JS_MEMORY_LIMIT);
    JS_SetMaxStackSize(rt, JS_STACK_SIZE);

    JSContext *ctx = JS_NewContext(rt);
    if (!ctx) {
        fprintf(stderr, "Failed to create JS context\n");
        JS_FreeRuntime(rt);
        return NULL;
    }

    JSEngine *engine = calloc(1, sizeof(JSEngine));
    if (!engine) {
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return NULL;
    }
    engine->rt = rt;
    engine->ctx = ctx;

    JS_SetContextOpaque(ctx, engine);

    /* Without a tracker QuickJS drops unhandled rejections silently. */
    JS_SetHostPromiseRejectionTracker(rt, promise_rejection_tracker, engine);

    js_register_console(ctx);

    return engine;
}

void js_engine_shutdown(JSEngine *engine)
{
    if (!engine) return;
    if (engine->ctx) {
        for (int i = 0; i < engine->rej_count; i++) {
            JS_FreeValue(engine->ctx, engine->rej_promises[i]);
            JS_FreeValue(engine->ctx, engine->rej_reasons[i]);
        }
    }
    free(engine->rej_promises);
    free(engine->rej_reasons);
    if (engine->ctx) JS_FreeContext(engine->ctx);
    if (engine->rt)  JS_FreeRuntime(engine->rt);
    free(engine);
}

bool js_engine_eval(JSEngine *engine, const char *script, const char *filename)
{
    if (!engine || !script) return false;
    const char *fname = filename ? filename : "<eval>";

    js_engine_watchdog_arm(engine);
    JSValue result = JS_Eval(engine->ctx, script, strlen(script),
                             fname, JS_EVAL_TYPE_GLOBAL);

    if (JS_IsException(result)) {
        js_dump_exception(engine, engine->ctx);
        return false;
    }

    JS_FreeValue(engine->ctx, result);
    return true;
}

char *js_engine_eval_string(JSEngine *engine, const char *script, const char *filename)
{
    if (!engine || !script) return NULL;
    const char *fname = filename ? filename : "<eval>";

    JSValue result = JS_Eval(engine->ctx, script, strlen(script),
                             fname, JS_EVAL_TYPE_GLOBAL);

    if (JS_IsException(result)) {
        js_dump_exception(engine, engine->ctx);
        return NULL;
    }

    const char *cstr = JS_ToCString(engine->ctx, result);
    JS_FreeValue(engine->ctx, result);

    if (!cstr) return NULL;

    char *copy = strdup(cstr);
    JS_FreeCString(engine->ctx, cstr);
    return copy;
}

void js_engine_free_string(char *str)
{
    free(str);
}

bool js_engine_eval_file(JSEngine *engine, const char *filepath)
{
    if (!engine || !filepath) return false;

    js_engine_watchdog_arm(engine);
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open file: %s\n", filepath);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        fprintf(stderr, "Failed to determine file size: %s\n", filepath);
        return false;
    }

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "Out of memory loading file: %s\n", filepath);
        return false;
    }

    size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read] = '\0';

    bool ok = js_engine_eval(engine, buf, filepath);
    free(buf);
    return ok;
}

/* Bytecode caching: <file>.js -> <file>.jsc, prefixed with the source mtime. */

/* Append 'c' to the JS file path (.js -> .jsc). */
static char *make_cache_path(const char *filepath)
{
    size_t len = strlen(filepath);
    char *cache_path = malloc(len + 2);
    if (!cache_path) return NULL;
    memcpy(cache_path, filepath, len);
    cache_path[len] = 'c';
    cache_path[len + 1] = '\0';
    return cache_path;
}

/* Returns 0 and the file's mtime on success, -1 if the file is missing. */
static int get_file_mtime(const char *path, int64_t *mtime)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    *mtime = (int64_t)st.st_mtime;
    return 0;
}

/* Read a whole file into a NUL-terminated buffer. Returns NULL on failure. */
static uint8_t *read_file_binary(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return NULL; }

    uint8_t *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read] = '\0';
    *out_size = read;
    return buf;
}

static bool write_file_binary(const char *path, const uint8_t *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    return written == size;
}

bool js_engine_eval_file_cached(JSEngine *engine, const char *filepath)
{
    if (!engine || !filepath) return false;

    char *cache_path = make_cache_path(filepath);
    if (!cache_path) return js_engine_eval_file(engine, filepath);

    /* Use the cache only if its stored source mtime matches the current one. */
    int64_t src_mtime = 0, cache_mtime = 0;
    if (get_file_mtime(filepath, &src_mtime) == 0 &&
        get_file_mtime(cache_path, &cache_mtime) == 0) {
        size_t cache_size = 0;
        uint8_t *cache_data = read_file_binary(cache_path, &cache_size);
        if (cache_data && cache_size > sizeof(int64_t)) {
            int64_t stored_mtime;
            memcpy(&stored_mtime, cache_data, sizeof(int64_t));
            if (stored_mtime == src_mtime) {
                JSValue obj = JS_ReadObject(engine->ctx,
                    cache_data + sizeof(int64_t),
                    cache_size - sizeof(int64_t),
                    JS_READ_OBJ_BYTECODE);
                free(cache_data);

                if (!JS_IsException(obj)) {
                    JSValue result = JS_EvalFunction(engine->ctx, obj);
                    if (JS_IsException(result)) {
                        js_dump_exception(engine, engine->ctx);
                        free(cache_path);
                        return false;
                    }
                    JS_FreeValue(engine->ctx, result);
                    free(cache_path);
                    return true;
                }
                /* Unreadable cache: consume the exception and recompile from source. */
                JS_FreeValue(engine->ctx, obj);
                {
                    JSValue exc = JS_GetException(engine->ctx);
                    JS_FreeValue(engine->ctx, exc);
                }
            } else {
                free(cache_data);
            }
        } else {
            free(cache_data);
        }
    }

    size_t src_size = 0;
    uint8_t *src_data = read_file_binary(filepath, &src_size);
    if (!src_data) {
        fprintf(stderr, "Failed to open file: %s\n", filepath);
        free(cache_path);
        return false;
    }

    JSValue obj = JS_Eval(engine->ctx, (const char *)src_data, src_size,
                          filepath, JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    free(src_data);

    if (JS_IsException(obj)) {
        js_dump_exception(engine, engine->ctx);
        free(cache_path);
        return false;
    }

    /* Write the cache: source mtime header followed by the bytecode. */
    size_t bc_size = 0;
    uint8_t *bc_data = JS_WriteObject(engine->ctx, &bc_size, obj, JS_WRITE_OBJ_BYTECODE);
    if (bc_data && bc_size > 0) {
        size_t total = sizeof(int64_t) + bc_size;
        uint8_t *full_cache = malloc(total);
        if (full_cache) {
            memcpy(full_cache, &src_mtime, sizeof(int64_t));
            memcpy(full_cache + sizeof(int64_t), bc_data, bc_size);
            write_file_binary(cache_path, full_cache, total);
            free(full_cache);
        }
        js_free(engine->ctx, bc_data);
    }

    JSValue result = JS_EvalFunction(engine->ctx, obj);
    free(cache_path);

    if (JS_IsException(result)) {
        js_dump_exception(engine, engine->ctx);
        return false;
    }

    JS_FreeValue(engine->ctx, result);
    return true;
}

int js_engine_execute_pending_jobs(JSEngine *engine)
{
    if (!engine) return -1;

    int count = 0;
    JSContext *pctx = NULL;
    while (JS_IsJobPending(engine->rt)) {
        int ret = JS_ExecutePendingJob(engine->rt, &pctx);
        if (ret < 0) {
            js_dump_exception(engine, pctx ? pctx : engine->ctx);
            flush_unhandled_rejections(engine);
            return -1;
        }
        count += ret;
    }
    /* Microtask checkpoint: anything still unhandled is reported now. */
    flush_unhandled_rejections(engine);
    return count;
}

void js_engine_set_console_callback(JSEngine *engine, JSConsoleCallback cb, void *userdata)
{
    if (!engine) return;
    engine->console_cb = cb;
    engine->console_userdata = userdata;
}

JSContext *js_engine_get_context(JSEngine *engine)
{
    if (!engine) return NULL;
    return engine->ctx;
}
