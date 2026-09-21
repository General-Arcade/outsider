/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_BIND_TILEMAP_H
#define RMMZ_BIND_TILEMAP_H

#include <quickjs.h>

/* Register the __native_tilemap global object. drawTiles() takes a Float32Array
   of 7 floats per tile [setNumber, sx, sy, dx, dy, w, h], per-tileset texture
   id/width/height arrays indexed by setNumber, and a layer offset. */
void bind_tilemap_register(JSContext *ctx);

#endif /* RMMZ_BIND_TILEMAP_H */
