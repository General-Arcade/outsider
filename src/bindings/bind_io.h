/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_BIND_IO_H
#define RMMZ_BIND_IO_H

#include <quickjs.h>

/* Register the __native_io global object exposing synchronous file I/O to JS.
   All paths are resolved relative to the game root. */
void bind_io_register(JSContext *ctx);

#endif /* RMMZ_BIND_IO_H */
