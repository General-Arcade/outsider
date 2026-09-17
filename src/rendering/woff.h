/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_WOFF_H
#define RMMZ_WOFF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* WOFF 1.0 unwrapping. RPG Maker MZ ships its default fonts (the M+ family)
   as .woff, which is an sfnt (TrueType/CFF) wrapped in a table directory
   whose tables are individually zlib-compressed. stb_truetype only reads
   raw sfnt data, so the wrapper is undone here. WOFF 2.0 (Brotli) is not
   supported. */

/* True if the buffer starts with the WOFF 1.0 signature. */
bool woff_is_woff(const uint8_t *data, size_t size);

/* Convert a WOFF 1.0 font to a raw sfnt blob. Returns a malloc'd buffer
   (free() it) and its size, or NULL if the data is malformed. */
uint8_t *woff_to_sfnt(const uint8_t *data, size_t size, size_t *out_size);

#endif /* RMMZ_WOFF_H */
