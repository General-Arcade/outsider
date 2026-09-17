/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_BIND_CANVAS2D_H
#define RMMZ_BIND_CANVAS2D_H

#include <quickjs.h>

/* Register the __native_canvas2d global object with Canvas2D functions.
   Must be called after js_engine_init(). */
void bind_canvas2d_register(JSContext *ctx);

#endif /* RMMZ_BIND_CANVAS2D_H */
