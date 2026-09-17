/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_ERROR_HANDLER_H
#define RMMZ_ERROR_HANDLER_H

#include <stdbool.h>
#include <stddef.h>

/* Log levels, ordered by severity. */
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3
} LogLevel;

/* Callback signature for log output capture (used by tests). */
typedef void (*LogCallback)(LogLevel level, const char *message, void *userdata);

/* Initialize logging. log_file_path may be NULL to disable file output;
   messages below min_level are discarded. */
void error_handler_init(const char *log_file_path, LogLevel min_level);

/* Shut down the error handler and close any open log file. */
void error_handler_shutdown(void);

/* Log a message at the given level. Supports printf-style format strings. */
void error_handler_log(LogLevel level, const char *fmt, ...);

/* Set the minimum log level. Messages below this level are discarded. */
void error_handler_set_level(LogLevel min_level);

/* Get the current minimum log level. */
LogLevel error_handler_get_level(void);

/* Set a log callback for capturing output (primarily for testing).
   When set, log output goes to the callback in addition to file/console. */
void error_handler_set_callback(LogCallback cb, void *userdata);

/* Log the message at ERROR level and show an SDL2 message box (if SDL is up). */
void error_handler_show_error_dialog(const char *title, const char *message);

/* Log the pending JS exception (message + stack), dispatch it to window.onerror,
   and show an error dialog when fatal is true. engine is a JSEngine*. */
void error_handler_report_js_exception(void *engine, bool fatal);

/* Crash recovery: ask the JS side to auto-save. Returns true on success. */
bool error_handler_attempt_auto_save(void *engine);

/* Get the log level name as a string (e.g., "DEBUG", "INFO"). */
const char *error_handler_level_name(LogLevel level);

#endif /* RMMZ_ERROR_HANDLER_H */
