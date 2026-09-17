/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_PLATFORM_H
#define RMMZ_PLATFORM_H

#include <stdbool.h>

typedef struct Platform Platform;

/* Create the window and OpenGL 4.5 context. Returns NULL on failure. */
/* Application name used for the window title and dialogs. */
#define RMMZ_APP_NAME "Outsider"

Platform *platform_init(const char *title, int width, int height);

/* Shut down and free all platform resources. */
void platform_shutdown(Platform *p);

/* Poll input events. Returns false when the user requests quit. */
bool platform_poll_events(Platform *p);

/* Swap the back buffer to the screen. */
void platform_swap_buffers(Platform *p);

/* Query the current window dimensions. */
void platform_get_window_size(Platform *p, int *w, int *h);

/* Set the window title. */
void platform_set_window_title(Platform *p, const char *title);

/* Enter or leave (borderless desktop) fullscreen. */
void platform_set_fullscreen(Platform *p, bool fullscreen);

/* Query fullscreen state. */
bool platform_is_fullscreen(Platform *p);

/* Get the resolution of the display the window is on. */
void platform_get_display_size(Platform *p, int *w, int *h);

/* Get the display DPI scale factor (1.0 = normal, 2.0 = HiDPI). */
float platform_get_display_scale(Platform *p);

/* The desktop scaling of the window's monitor (1.25 for Windows "125%"),
   as far as it is not already folded into platform_get_display_scale().
   A DPI-aware window opens in raw pixels, so this is what a game window
   must be multiplied by to appear as large as a browser would draw it. */
float platform_get_desktop_scale(Platform *p);

/* Resize the window (client area, in pixels) and keep it centred. */
void platform_set_window_size(Platform *p, int w, int h);

/* Returns true (and the new size) once per resize since the last call. */
bool platform_check_resize(Platform *p, int *w, int *h);

#endif /* RMMZ_PLATFORM_H */
