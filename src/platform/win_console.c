/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#include "platform/win_console.h"

#include <SDL.h>
#include <stdio.h>

#ifdef _WIN32

#include <windows.h>
#include <io.h>

static int std_handle_usable(DWORD which)
{
    HANDLE h = GetStdHandle(which);
    return h != NULL && h != INVALID_HANDLE_VALUE;
}

void platform_setup_console(const char *fallback_log_path)
{
    int have_out = std_handle_usable(STD_OUTPUT_HANDLE);
    int have_err = std_handle_usable(STD_ERROR_HANDLE);

    /* Console subsystem, or the parent redirected our streams: keep them. */
    if (have_out && have_err) return;

    /* Launched from an interactive console: write there. */
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *dummy = NULL;
        if (!have_out) freopen_s(&dummy, "CONOUT$", "w", stdout);
        if (!have_err) freopen_s(&dummy, "CONOUT$", "w", stderr);
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
        return;
    }

    /* No console anywhere (Explorer double-click): capture to a file. */
    if (fallback_log_path) {
        FILE *dummy = NULL;
        if (freopen_s(&dummy, fallback_log_path, "w", stdout) == 0) {
            /* Point stderr at the same file so ordering is preserved. */
            _dup2(_fileno(stdout), _fileno(stderr));
        }
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
    }
}

void platform_show_error_dialog(const char *title, const char *message)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, NULL);
}

#else /* !_WIN32 */

void platform_setup_console(const char *fallback_log_path)
{
    (void)fallback_log_path;
}

void platform_show_error_dialog(const char *title, const char *message)
{
    (void)title;
    (void)message;
}

#endif
