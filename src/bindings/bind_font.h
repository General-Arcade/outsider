/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_BIND_FONT_H
#define RMMZ_BIND_FONT_H

#include <quickjs.h>

/* Register the __native_font global object with font management functions.
   Must be called after js_engine_init(). */
void bind_font_register(JSContext *ctx);

#endif /* RMMZ_BIND_FONT_H */
