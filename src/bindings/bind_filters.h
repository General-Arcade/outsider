/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_BIND_FILTERS_H
#define RMMZ_BIND_FILTERS_H

#include <quickjs.h>

/* Register the __native_filters global object with filter shader compilation
   and rendering functions. Must be called after js_engine_init(). */
void bind_filters_register(JSContext *ctx);

/* Clean up built-in shader programs compiled during JS-level init().
   Call from C shutdown path before filters_shutdown(). */
void bind_filters_shutdown(void);

#endif /* RMMZ_BIND_FILTERS_H */
