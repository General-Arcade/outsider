/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_WIN_CONSOLE_H
#define RMMZ_WIN_CONSOLE_H

/* Windows GUI-subsystem builds start without stdout/stderr. Keep them if the
   parent redirected them, else attach to the parent's console, else redirect
   both to fallback_log_path. No-op on other platforms. */
void platform_setup_console(const char *fallback_log_path);

/* Show a blocking error dialog; usable before SDL_Init(). No-op on platforms
   without a native dialog (the caller is expected to have logged the message). */
void platform_show_error_dialog(const char *title, const char *message);

#endif /* RMMZ_WIN_CONSOLE_H */
