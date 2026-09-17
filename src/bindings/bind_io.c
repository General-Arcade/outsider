/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#include "bindings/bind_io.h"
#include "io/file_io.h"

#include <quickjs.h>
#include <stdlib.h>
#include <string.h>

#define PATH_BUF_SIZE 4096

/* Resolve a JS string argument to an absolute path in `out`. Returns the JS
   C-string (caller must JS_FreeCString) or NULL with a pending exception. */
static const char *resolve_path_arg(JSContext *ctx, JSValueConst arg,
                                    char *out, size_t out_size)
{
    const char *raw = JS_ToCString(ctx, arg);
    if (!raw) return NULL;

    if (!file_io_resolve_path(raw, out, out_size)) {
        JS_ThrowTypeError(ctx, "path resolution failed: %s", raw);
        JS_FreeCString(ctx, raw);
        return NULL;
    }
    return raw;
}

/* readFileSync(path) -> string */
static JSValue js_read_file_sync(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "readFileSync requires a path argument");

    char resolved[PATH_BUF_SIZE];
    const char *raw = resolve_path_arg(ctx, argv[0], resolved, sizeof(resolved));
    if (!raw) return JS_EXCEPTION;
    JS_FreeCString(ctx, raw);

    size_t size = 0;
    char *data = file_io_read_text(resolved, &size);
    if (!data) {
        return JS_ThrowInternalError(ctx, "failed to read file: %s", resolved);
    }

    JSValue result = JS_NewStringLen(ctx, data, size);
    free(data);
    return result;
}

/* readFileBinary(path) -> ArrayBuffer */
static JSValue js_read_file_binary(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "readFileBinary requires a path argument");

    char resolved[PATH_BUF_SIZE];
    const char *raw = resolve_path_arg(ctx, argv[0], resolved, sizeof(resolved));
    if (!raw) return JS_EXCEPTION;
    JS_FreeCString(ctx, raw);

    size_t size = 0;
    uint8_t *data = file_io_read_binary(resolved, &size);
    if (!data) {
        return JS_ThrowInternalError(ctx, "failed to read binary file: %s", resolved);
    }

    JSValue result = JS_NewArrayBufferCopy(ctx, data, size);
    free(data);
    return result;
}

/* writeFileSync(path, data) */
static JSValue js_write_file_sync(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 2) return JS_ThrowTypeError(ctx, "writeFileSync requires path and data arguments");

    char resolved[PATH_BUF_SIZE];
    const char *raw = resolve_path_arg(ctx, argv[0], resolved, sizeof(resolved));
    if (!raw) return JS_EXCEPTION;
    JS_FreeCString(ctx, raw);

    size_t data_len = 0;
    const char *data = JS_ToCStringLen(ctx, &data_len, argv[1]);
    if (!data) return JS_EXCEPTION;

    /* Write with explicit length: strings may contain null bytes (e.g. deflated save data). */
    bool ok = file_io_write_binary(resolved, (const uint8_t *)data, data_len);
    JS_FreeCString(ctx, data);

    if (!ok) {
        return JS_ThrowInternalError(ctx, "failed to write file: %s", resolved);
    }
    return JS_UNDEFINED;
}

/* writeFileBinary(path, arrayBuffer) */
static JSValue js_write_file_binary(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 2) return JS_ThrowTypeError(ctx, "writeFileBinary requires path and data arguments");

    char resolved[PATH_BUF_SIZE];
    const char *raw = resolve_path_arg(ctx, argv[0], resolved, sizeof(resolved));
    if (!raw) return JS_EXCEPTION;
    JS_FreeCString(ctx, raw);

    size_t size = 0;
    uint8_t *buf = JS_GetArrayBuffer(ctx, &size, argv[1]);
    if (!buf) {
        return JS_ThrowTypeError(ctx, "writeFileBinary: second argument must be an ArrayBuffer");
    }

    bool ok = file_io_write_binary(resolved, buf, size);
    if (!ok) {
        return JS_ThrowInternalError(ctx, "failed to write binary file: %s", resolved);
    }
    return JS_UNDEFINED;
}

/* mkdirSync(path) -> bool */
static JSValue js_mkdir_sync(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "mkdirSync requires a path argument");

    char resolved[PATH_BUF_SIZE];
    const char *raw = resolve_path_arg(ctx, argv[0], resolved, sizeof(resolved));
    if (!raw) return JS_EXCEPTION;
    JS_FreeCString(ctx, raw);

    return JS_NewBool(ctx, file_io_mkdir(resolved));
}

/* existsSync(path) -> bool */
static JSValue js_exists_sync(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "existsSync requires a path argument");

    char resolved[PATH_BUF_SIZE];
    const char *raw = resolve_path_arg(ctx, argv[0], resolved, sizeof(resolved));
    if (!raw) return JS_EXCEPTION;
    JS_FreeCString(ctx, raw);

    return JS_NewBool(ctx, file_io_exists(resolved));
}

/* isDirSync(path) -> bool */
static JSValue js_is_dir_sync(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "isDirSync requires a path argument");

    char resolved[PATH_BUF_SIZE];
    const char *raw = resolve_path_arg(ctx, argv[0], resolved, sizeof(resolved));
    if (!raw) return JS_EXCEPTION;
    JS_FreeCString(ctx, raw);

    return JS_NewBool(ctx, file_io_is_directory(resolved));
}

/* readdirSync(path) -> string[] */
static JSValue js_readdir_sync(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "readdirSync requires a path argument");

    char resolved[PATH_BUF_SIZE];
    const char *raw = resolve_path_arg(ctx, argv[0], resolved, sizeof(resolved));
    if (!raw) return JS_EXCEPTION;
    JS_FreeCString(ctx, raw);

    size_t count = 0;
    char **entries = file_io_readdir(resolved, &count);
    if (!entries) {
        return JS_ThrowInternalError(ctx, "failed to read directory: %s", resolved);
    }

    JSValue arr = JS_NewArray(ctx);
    for (size_t i = 0; i < count; i++) {
        JS_SetPropertyUint32(ctx, arr, (uint32_t)i,
                             JS_NewString(ctx, entries[i]));
    }
    file_io_free_dirlist(entries);
    return arr;
}

/* unlinkSync(path) -> bool */
static JSValue js_unlink_sync(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "unlinkSync requires a path argument");

    char resolved[PATH_BUF_SIZE];
    const char *raw = resolve_path_arg(ctx, argv[0], resolved, sizeof(resolved));
    if (!raw) return JS_EXCEPTION;
    JS_FreeCString(ctx, raw);

    return JS_NewBool(ctx, file_io_unlink(resolved));
}

/* renameSync(oldPath, newPath) -> bool */
static JSValue js_rename_sync(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 2) return JS_ThrowTypeError(ctx, "renameSync requires oldPath and newPath arguments");

    char resolved_old[PATH_BUF_SIZE];
    const char *raw_old = resolve_path_arg(ctx, argv[0], resolved_old, sizeof(resolved_old));
    if (!raw_old) return JS_EXCEPTION;
    JS_FreeCString(ctx, raw_old);

    char resolved_new[PATH_BUF_SIZE];
    const char *raw_new = resolve_path_arg(ctx, argv[1], resolved_new, sizeof(resolved_new));
    if (!raw_new) return JS_EXCEPTION;
    JS_FreeCString(ctx, raw_new);

    return JS_NewBool(ctx, file_io_rename(resolved_old, resolved_new));
}

/* getGameRoot() -> string */
static JSValue js_get_game_root(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewString(ctx, file_io_get_game_root());
}

/* resolvePath(path) -> string */
static JSValue js_resolve_path(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_ThrowTypeError(ctx, "resolvePath requires a path argument");

    char resolved[PATH_BUF_SIZE];
    const char *raw = resolve_path_arg(ctx, argv[0], resolved, sizeof(resolved));
    if (!raw) return JS_EXCEPTION;
    JS_FreeCString(ctx, raw);

    return JS_NewString(ctx, resolved);
}

/* Registration */

static const JSCFunctionListEntry js_io_funcs[] = {
    JS_CFUNC_DEF("readFileSync",    1, js_read_file_sync),
    JS_CFUNC_DEF("readFileBinary",  1, js_read_file_binary),
    JS_CFUNC_DEF("writeFileSync",   2, js_write_file_sync),
    JS_CFUNC_DEF("writeFileBinary", 2, js_write_file_binary),
    JS_CFUNC_DEF("mkdirSync",       1, js_mkdir_sync),
    JS_CFUNC_DEF("existsSync",      1, js_exists_sync),
    JS_CFUNC_DEF("isDirSync",       1, js_is_dir_sync),
    JS_CFUNC_DEF("readdirSync",     1, js_readdir_sync),
    JS_CFUNC_DEF("unlinkSync",      1, js_unlink_sync),
    JS_CFUNC_DEF("renameSync",      2, js_rename_sync),
    JS_CFUNC_DEF("getGameRoot",     0, js_get_game_root),
    JS_CFUNC_DEF("resolvePath",     1, js_resolve_path),
};

void bind_io_register(JSContext *ctx)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue io_obj = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, io_obj, js_io_funcs,
                               sizeof(js_io_funcs) / sizeof(js_io_funcs[0]));

    JS_SetPropertyStr(ctx, global, "__native_io", io_obj);
    JS_FreeValue(ctx, global);
}
