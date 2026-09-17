/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_CANVAS2D_H
#define RMMZ_CANVAS2D_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Opaque handle for a Canvas2D rendering context. */
typedef uint32_t Canvas2DHandle;
#define CANVAS2D_HANDLE_INVALID 0

/* Text alignment constants. */
enum {
    CANVAS2D_ALIGN_LEFT   = 0,
    CANVAS2D_ALIGN_CENTER = 1,
    CANVAS2D_ALIGN_RIGHT  = 2,
    CANVAS2D_ALIGN_START  = 3,
    CANVAS2D_ALIGN_END    = 4,
};

/* Text baseline constants. */
enum {
    CANVAS2D_BASELINE_TOP         = 0,
    CANVAS2D_BASELINE_HANGING     = 1,
    CANVAS2D_BASELINE_MIDDLE      = 2,
    CANVAS2D_BASELINE_ALPHABETIC  = 3,
    CANVAS2D_BASELINE_IDEOGRAPHIC = 4,
    CANVAS2D_BASELINE_BOTTOM      = 5,
};

/* Composite operation constants. */
enum {
    CANVAS2D_COMP_SOURCE_OVER = 0,
    CANVAS2D_COMP_SOURCE_ATOP = 1,
    CANVAS2D_COMP_SOURCE_IN   = 2,
    CANVAS2D_COMP_SOURCE_OUT  = 3,
    CANVAS2D_COMP_DEST_OVER   = 4,
    CANVAS2D_COMP_DEST_ATOP   = 5,
    CANVAS2D_COMP_DEST_IN     = 6,
    CANVAS2D_COMP_DEST_OUT    = 7,
    CANVAS2D_COMP_LIGHTER     = 8,
    CANVAS2D_COMP_COPY        = 9,
    CANVAS2D_COMP_XOR         = 10,
    CANVAS2D_COMP_MULTIPLY    = 11,
    CANVAS2D_COMP_SCREEN      = 12,
};

/* Initialize the Canvas2D subsystem. Call once at startup. */
void canvas2d_init(void);

/* Shut down and free all Canvas2D contexts. */
void canvas2d_shutdown(void);

/* Create a new Canvas2D context with the given dimensions.
   The backing pixel buffer is initialized to transparent black. */
Canvas2DHandle canvas2d_create(int width, int height);

/* Destroy a Canvas2D context and free its resources. */
void canvas2d_destroy(Canvas2DHandle handle);

/* Resize the canvas. Existing pixel data is discarded (cleared to transparent). */
void canvas2d_resize(Canvas2DHandle handle, int width, int height);

/* Get the canvas dimensions. Returns false if handle is invalid. */
bool canvas2d_get_size(Canvas2DHandle handle, int *out_w, int *out_h);

/* Get a pointer to the raw RGBA pixel data. Returns NULL if handle is invalid.
   The buffer has width * height * 4 bytes. Caller must NOT free the pointer. */
uint8_t *canvas2d_get_pixels(Canvas2DHandle handle);

/* Drawing state */

void canvas2d_set_fill_color(Canvas2DHandle h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void canvas2d_set_stroke_color(Canvas2DHandle h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void canvas2d_set_global_alpha(Canvas2DHandle h, float alpha);
void canvas2d_set_composite_op(Canvas2DHandle h, int op);
void canvas2d_set_line_width(Canvas2DHandle h, float width);
void canvas2d_set_text_align(Canvas2DHandle h, int align);
void canvas2d_set_text_baseline(Canvas2DHandle h, int baseline);

/* State stack (save/restore). */
void canvas2d_save(Canvas2DHandle h);
void canvas2d_restore(Canvas2DHandle h);

/* Drawing operations */

/* Fill a rectangle with the current fill color. */
void canvas2d_fill_rect(Canvas2DHandle h, int x, int y, int w, int h2);

/* Clear a rectangle to transparent black. */
void canvas2d_clear_rect(Canvas2DHandle h, int x, int y, int w, int h2);

/* Stroke the outline of a rectangle with the current stroke color. */
void canvas2d_stroke_rect(Canvas2DHandle h, int x, int y, int w, int h2);

/* Blit source rect (sx,sy,sw,sh) of an RGBA buffer to dest rect (dx,dy,dw,dh). */
void canvas2d_draw_image(Canvas2DHandle h,
                          const uint8_t *src_pixels, int src_w, int src_h,
                          int sx, int sy, int sw, int sh,
                          int dx, int dy, int dw, int dh);

/* Pixel manipulation */

/* Copy a region of the canvas pixel data into `out`.
   `out` must have space for w * h * 4 bytes. */
void canvas2d_get_image_data(Canvas2DHandle h, int x, int y, int w, int h2,
                              uint8_t *out);

/* Write pixel data into the canvas at (x, y).
   `data` must have w * h * 4 bytes (RGBA). */
void canvas2d_put_image_data(Canvas2DHandle h, const uint8_t *data,
                              int x, int y, int w, int h2);

/* Font and text */

/* Load a TrueType font from a file. Returns true on success. */
bool canvas2d_load_font(const char *name, const uint8_t *data, size_t size);

/* Set the current font for a context by name and pixel size. */
void canvas2d_set_font(Canvas2DHandle h, const char *name, float pixel_size);

/* Render filled text at (x, y). */
void canvas2d_fill_text(Canvas2DHandle h, const char *text, float x, float y);

/* Render stroked text at (x, y) (outline only). */
void canvas2d_stroke_text(Canvas2DHandle h, const char *text, float x, float y);

/* Measure the width of `text` in pixels using the current font. */
float canvas2d_measure_text(Canvas2DHandle h, const char *text);

/* Return the number of active Canvas2D contexts. */
size_t canvas2d_context_count(void);

#endif /* RMMZ_CANVAS2D_H */
