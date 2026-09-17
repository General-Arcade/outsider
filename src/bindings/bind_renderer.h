/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_BIND_RENDERER_H
#define RMMZ_BIND_RENDERER_H

#include <quickjs.h>

/* Register the __native_renderer global object with GPU renderer functions
   (textures, render targets, sprite batch). Must be called after js_engine_init(). */
void bind_renderer_register(JSContext *ctx);

#endif /* RMMZ_BIND_RENDERER_H */
