/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_TILEMAP_H
#define RMMZ_TILEMAP_H

#include "rendering/sprite_batch.h"

#include <stdint.h>

/* Tileset texture information for a single tileset image. */
typedef struct {
    uint32_t gl_texture;  /* OpenGL texture ID */
    int      width;       /* Texture width in pixels */
    int      height;      /* Texture height in pixels */
} TilesetInfo;

/* Draw packed tile rectangles into the sprite batch. `elements` holds 7 floats
   per tile: [setNumber, sx, sy, dx, dy, w, h]; setNumber indexes `tilesets`
   and -1 draws a shadow tile (black, 50% alpha). offset_x/y shift all
   destination coordinates. */
void tilemap_draw_tiles(const float *elements, int count,
                        const TilesetInfo *tilesets, int num_tilesets,
                        float offset_x, float offset_y,
                        SpriteBatch *batch);

#endif /* RMMZ_TILEMAP_H */
