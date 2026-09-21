/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_JS_ENGINE_H
#define RMMZ_JS_ENGINE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct JSEngine JSEngine;

/* Callback signature for console output capture (used by tests). */
typedef void (*JSConsoleCallback)(const char *level, const char *message, void *userdata);

/* Create a QuickJS runtime and context. Returns NULL on failure. */
JSEngine *js_engine_init(void);

/* Shut down and free all engine resources. Safe to call with NULL. */
void js_engine_shutdown(JSEngine *engine);

/* Evaluate a JS string. Returns false on error (the exception is printed). */
bool js_engine_eval(JSEngine *engine, const char *script, const char *filename);

/* Evaluate a JS string and return its result as a string, or NULL on error.
   Free the result with js_engine_free_string(). */
char *js_engine_eval_string(JSEngine *engine, const char *script, const char *filename);

/* Free a string returned by js_engine_eval_string(). */
void js_engine_free_string(char *str);

/* Load and evaluate a JS file from disk. Returns true on success. */
bool js_engine_eval_file(JSEngine *engine, const char *filepath);

/* Like js_engine_eval_file(), but caches compiled bytecode in a .jsc file next
   to the source (invalidated by source mtime). Falls back to plain eval. */
bool js_engine_eval_file_cached(JSEngine *engine, const char *filepath);

/* Drain the job queue (promise callbacks) and report unhandled rejections.
   Returns the number of jobs executed, or -1 on error. */
int js_engine_execute_pending_jobs(JSEngine *engine);

/* Route console.log/warn/error to a callback instead of stdout/stderr. */
void js_engine_set_console_callback(JSEngine *engine, JSConsoleCallback cb, void *userdata);

/* Get the underlying QuickJS context (NULL if engine is NULL). */
struct JSContext *js_engine_get_context(JSEngine *engine);

/* Debug aid: interrupt JS that runs longer than limit_ms since the last
   js_engine_watchdog_arm() call, raising an uncatchable InternalError whose
   stack trace shows where the script was stuck. 0 disables (the default). */
void js_engine_set_watchdog(JSEngine *engine, unsigned limit_ms);

/* Restart the watchdog clock. Eval entry points arm it themselves; the game
   loop arms it once per frame. */
void js_engine_watchdog_arm(JSEngine *engine);

#endif /* RMMZ_JS_ENGINE_H */
