/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_APNG_H
#define RMMZ_APNG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Animated PNG decoding. Popular plugins (ApngPicture + pixi-apngAndGif)
   decode APNG in pure JavaScript, which takes minutes under QuickJS when a
   game preloads hundreds of animations. This decoder yields fully composed
   RGBA frames so the JS side only has to wrap them as textures. */

typedef struct {
    int      delay_ms;   /* Display time of this frame. */
    uint8_t *rgba;       /* width * height * 4, straight alpha, top-down. */
} ApngFrame;

typedef struct {
    int        width;
    int        height;
    int        num_frames;
    int        num_plays;  /* 0 = loop forever */
    ApngFrame *frames;
} ApngImage;

/* True if the data is a PNG containing an animation control chunk. */
bool apng_is_apng(const uint8_t *data, size_t size);

/* Decode every frame. Returns NULL if the data is not a valid APNG. */
ApngImage *apng_decode(const uint8_t *data, size_t size);

void apng_free(ApngImage *image);

#endif /* RMMZ_APNG_H */
