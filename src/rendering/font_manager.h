/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_FONT_MANAGER_H
#define RMMZ_FONT_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Font style flags (combinable). */
#define FONT_STYLE_NORMAL  0
#define FONT_STYLE_BOLD    1
#define FONT_STYLE_ITALIC  2

/* Font metrics for vertical layout. */
typedef struct {
    float ascent;      /* Ascent in pixels (positive, above baseline). */
    float descent;     /* Descent in pixels (negative, below baseline). */
    float line_gap;    /* Extra line gap in pixels. */
    float line_height; /* ascent - descent + line_gap. */
} FontMetrics;

/* Initialize the font manager. Call once at startup. */
void font_manager_init(void);

/* Shut down and free all loaded fonts. */
void font_manager_shutdown(void);

/* Load a TrueType font from memory buffer.
   `family` is the font family name (e.g., "GameFont").
   Returns true on success. */
bool font_manager_load(const char *family, const uint8_t *data, size_t size);

/* Load a TrueType font from a file path.
   `family` is the font family name.
   Returns true on success. */
bool font_manager_load_file(const char *family, const char *path);

/* Check if a font family is loaded. */
bool font_manager_has_font(const char *family);

/* Get font metrics at the given pixel size. Returns false if font not found. */
bool font_manager_get_metrics(const char *family, float pixel_size, FontMetrics *out);

/* Measure text width in pixels using the specified font family and size. */
float font_manager_measure_text(const char *family, float pixel_size, const char *text);

/* Get the number of loaded font families. */
int font_manager_font_count(void);

/* Get the name of a loaded font by index (0-based). Returns NULL if out of range. */
const char *font_manager_font_name(int index);

#endif /* RMMZ_FONT_MANAGER_H */
