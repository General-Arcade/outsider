/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#include "rendering/canvas2d.h"
#include "rendering/woff.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* stb_truetype implementation lives in this translation unit. */
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

typedef struct {
    uint8_t fill_r, fill_g, fill_b, fill_a;
    uint8_t stroke_r, stroke_g, stroke_b, stroke_a;
    float   global_alpha;      /* 0.0 .. 1.0 */
    int     composite_op;
    float   line_width;
    int     text_align;
    int     text_baseline;
    char   *font_name;
    float   font_size;
} DrawState;

#define STATE_STACK_MAX 64

#define MAX_FONTS 64

typedef struct {
    char            *name;
    uint8_t         *data;       /* TTF file data (owned). */
    size_t           data_size;
    stbtt_fontinfo   info;
    bool             valid;
} FontEntry;

static FontEntry s_fonts[MAX_FONTS];
static int       s_font_count = 0;

/* --- Glyph fallback ---------------------------------------------------
   A game's font rarely covers every script the game prints: a Korean font
   with the database's Japanese parameter names, or symbols such as arrows.
   Browsers substitute another installed font per glyph; so does this
   renderer: first the other fonts the game loaded, then well-known system
   fonts, read from disk the first time a glyph is missing. */
static const char *const SYSTEM_FALLBACK_FILES[] = {
#ifdef _WIN32
    "YuGothM.ttc", "meiryo.ttc", "msgothic.ttc", "malgun.ttf", "msyh.ttc",
    "simsun.ttc", "seguisym.ttf", "segoeui.ttf", "arial.ttf",
#elif defined(__APPLE__)
    "/System/Library/Fonts/PingFang.ttc",
    "/System/Library/Fonts/AppleSDGothicNeo.ttc",
    "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
    "/System/Library/Fonts/Apple Symbols.ttf",
#else
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
#endif
};
#define SYSTEM_FALLBACK_COUNT (sizeof(SYSTEM_FALLBACK_FILES) / sizeof(SYSTEM_FALLBACK_FILES[0]))
static FontEntry   s_fallback_fonts[SYSTEM_FALLBACK_COUNT];
static signed char s_fallback_state[SYSTEM_FALLBACK_COUNT];   /* 0 untried, 1 loaded, -1 unavailable */

typedef struct {
    Canvas2DHandle handle;
    uint8_t       *pixels;       /* RGBA pixel buffer (owned). */
    int            width;
    int            height;
    DrawState      state;
    DrawState      state_stack[STATE_STACK_MAX];
    int            state_depth;
    bool           active;
} Canvas2DCtx;

#define MAX_CONTEXTS 256

static Canvas2DCtx    s_contexts[MAX_CONTEXTS];
static Canvas2DHandle s_next_handle = 1;
static size_t         s_ctx_count   = 0;

static Canvas2DCtx *find_ctx(Canvas2DHandle h)
{
    for (size_t i = 0; i < MAX_CONTEXTS; i++) {
        if (s_contexts[i].active && s_contexts[i].handle == h) {
            return &s_contexts[i];
        }
    }
    return NULL;
}

static FontEntry *find_font(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < s_font_count; i++) {
        if (s_fonts[i].valid && strcmp(s_fonts[i].name, name) == 0) {
            return &s_fonts[i];
        }
    }
    return NULL;
}

static inline int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Composite a source RGBA pixel onto a destination pixel with the given
   composite operation and global alpha. */
static inline void blend_pixel(uint8_t *dst,
                                uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa,
                                float global_alpha, int comp_op)
{
    int src_a = (int)(sa * global_alpha + 0.5f);
    if (src_a <= 0) return;

    if (comp_op == CANVAS2D_COMP_COPY) {
        dst[0] = sr;
        dst[1] = sg;
        dst[2] = sb;
        dst[3] = (uint8_t)src_a;
        return;
    }

    int dst_a = dst[3];

    if (comp_op == CANVAS2D_COMP_SOURCE_OVER) {
        if (src_a >= 255) {
            dst[0] = sr;
            dst[1] = sg;
            dst[2] = sb;
            dst[3] = 255;
            return;
        }

        int inv = 255 - src_a;
        int out_a = src_a + ((dst_a * inv + 127) / 255);
        if (out_a == 0) {
            dst[0] = dst[1] = dst[2] = dst[3] = 0;
            return;
        }
        dst[0] = (uint8_t)((sr * src_a + dst[0] * dst_a * inv / 255 + out_a / 2) / out_a);
        dst[1] = (uint8_t)((sg * src_a + dst[1] * dst_a * inv / 255 + out_a / 2) / out_a);
        dst[2] = (uint8_t)((sb * src_a + dst[2] * dst_a * inv / 255 + out_a / 2) / out_a);
        dst[3] = (uint8_t)out_a;
        return;
    }

    if (comp_op == CANVAS2D_COMP_LIGHTER) {
        int out_a = clampi(src_a + dst_a, 0, 255);
        dst[0] = (uint8_t)clampi(((int)sr * src_a + (int)dst[0] * dst_a + 127) / 255, 0, 255);
        dst[1] = (uint8_t)clampi(((int)sg * src_a + (int)dst[1] * dst_a + 127) / 255, 0, 255);
        dst[2] = (uint8_t)clampi(((int)sb * src_a + (int)dst[2] * dst_a + 127) / 255, 0, 255);
        dst[3] = (uint8_t)out_a;
        return;
    }

    if (comp_op == CANVAS2D_COMP_MULTIPLY) {
        int inv = 255 - src_a;
        int out_a = src_a + ((dst_a * inv + 127) / 255);
        if (out_a == 0) {
            dst[0] = dst[1] = dst[2] = dst[3] = 0;
            return;
        }
        int mr = ((int)sr * (int)dst[0] + 127) / 255;
        int mg = ((int)sg * (int)dst[1] + 127) / 255;
        int mb = ((int)sb * (int)dst[2] + 127) / 255;
        dst[0] = (uint8_t)clampi((mr * src_a + dst[0] * dst_a * inv / 255 + out_a / 2) / out_a, 0, 255);
        dst[1] = (uint8_t)clampi((mg * src_a + dst[1] * dst_a * inv / 255 + out_a / 2) / out_a, 0, 255);
        dst[2] = (uint8_t)clampi((mb * src_a + dst[2] * dst_a * inv / 255 + out_a / 2) / out_a, 0, 255);
        dst[3] = (uint8_t)out_a;
        return;
    }

    if (comp_op == CANVAS2D_COMP_SCREEN) {
        int inv = 255 - src_a;
        int out_a = src_a + ((dst_a * inv + 127) / 255);
        if (out_a == 0) {
            dst[0] = dst[1] = dst[2] = dst[3] = 0;
            return;
        }
        int mr = (int)sr + (int)dst[0] - ((int)sr * (int)dst[0] + 127) / 255;
        int mg = (int)sg + (int)dst[1] - ((int)sg * (int)dst[1] + 127) / 255;
        int mb = (int)sb + (int)dst[2] - ((int)sb * (int)dst[2] + 127) / 255;
        dst[0] = (uint8_t)clampi((mr * src_a + dst[0] * dst_a * inv / 255 + out_a / 2) / out_a, 0, 255);
        dst[1] = (uint8_t)clampi((mg * src_a + dst[1] * dst_a * inv / 255 + out_a / 2) / out_a, 0, 255);
        dst[2] = (uint8_t)clampi((mb * src_a + dst[2] * dst_a * inv / 255 + out_a / 2) / out_a, 0, 255);
        dst[3] = (uint8_t)out_a;
        return;
    }

    /* Unimplemented modes fall back to source-over. */
    {
        int inv = 255 - src_a;
        int out_a = src_a + ((dst_a * inv + 127) / 255);
        if (out_a == 0) {
            dst[0] = dst[1] = dst[2] = dst[3] = 0;
            return;
        }
        dst[0] = (uint8_t)((sr * src_a + dst[0] * dst_a * inv / 255 + out_a / 2) / out_a);
        dst[1] = (uint8_t)((sg * src_a + dst[1] * dst_a * inv / 255 + out_a / 2) / out_a);
        dst[2] = (uint8_t)((sb * src_a + dst[2] * dst_a * inv / 255 + out_a / 2) / out_a);
        dst[3] = (uint8_t)out_a;
    }
}

static void init_state(DrawState *s)
{
    s->fill_r = 0;   s->fill_g = 0;   s->fill_b = 0;   s->fill_a = 255;
    s->stroke_r = 0; s->stroke_g = 0; s->stroke_b = 0; s->stroke_a = 255;
    s->global_alpha = 1.0f;
    s->composite_op = CANVAS2D_COMP_SOURCE_OVER;
    s->line_width = 1.0f;
    s->text_align = CANVAS2D_ALIGN_START;
    s->text_baseline = CANVAS2D_BASELINE_ALPHABETIC;
    s->font_name = NULL;
    s->font_size = 10.0f;
}

static void copy_state(DrawState *dst, const DrawState *src)
{
    *dst = *src;
    if (src->font_name) {
        dst->font_name = strdup(src->font_name);
    }
}

static void free_state(DrawState *s)
{
    free(s->font_name);
    s->font_name = NULL;
}

/* Lifecycle */

void canvas2d_init(void)
{
    memset(s_contexts, 0, sizeof(s_contexts));
    s_next_handle = 1;
    s_ctx_count = 0;
}

void canvas2d_shutdown(void)
{
    for (size_t i = 0; i < MAX_CONTEXTS; i++) {
        if (s_contexts[i].active) {
            free(s_contexts[i].pixels);
            free_state(&s_contexts[i].state);
            for (int j = 0; j < s_contexts[i].state_depth; j++) {
                free_state(&s_contexts[i].state_stack[j]);
            }
            s_contexts[i].active = false;
        }
    }
    s_ctx_count = 0;

    for (int i = 0; i < s_font_count; i++) {
        free(s_fonts[i].name);
        free(s_fonts[i].data);
        s_fonts[i].valid = false;
    }
    s_font_count = 0;

    for (size_t i = 0; i < SYSTEM_FALLBACK_COUNT; i++) {
        if (s_fallback_state[i] == 1) {
            free(s_fallback_fonts[i].name);
            free(s_fallback_fonts[i].data);
        }
        memset(&s_fallback_fonts[i], 0, sizeof(FontEntry));
        s_fallback_state[i] = 0;
    }
}

Canvas2DHandle canvas2d_create(int width, int height)
{
    if (width <= 0 || height <= 0) return CANVAS2D_HANDLE_INVALID;

    /* Guard against size_t overflow on 32-bit targets. */
    uint64_t alloc_check = (uint64_t)width * (uint64_t)height * 4;
    if (alloc_check > SIZE_MAX) return CANVAS2D_HANDLE_INVALID;

    for (size_t i = 0; i < MAX_CONTEXTS; i++) {
        if (!s_contexts[i].active) {
            Canvas2DCtx *ctx = &s_contexts[i];
            size_t buf_size = (size_t)alloc_check;
            ctx->pixels = calloc(1, buf_size);
            if (!ctx->pixels) return CANVAS2D_HANDLE_INVALID;

            ctx->width = width;
            ctx->height = height;
            ctx->handle = s_next_handle++;
            if (s_next_handle == 0) s_next_handle = 1; /* skip INVALID */
            ctx->active = true;
            ctx->state_depth = 0;
            init_state(&ctx->state);

            s_ctx_count++;
            return ctx->handle;
        }
    }
    return CANVAS2D_HANDLE_INVALID;
}

void canvas2d_destroy(Canvas2DHandle handle)
{
    Canvas2DCtx *ctx = find_ctx(handle);
    if (!ctx) return;

    free(ctx->pixels);
    free_state(&ctx->state);
    for (int j = 0; j < ctx->state_depth; j++) {
        free_state(&ctx->state_stack[j]);
    }
    ctx->active = false;
    ctx->pixels = NULL;
    s_ctx_count--;
}

void canvas2d_resize(Canvas2DHandle handle, int width, int height)
{
    Canvas2DCtx *ctx = find_ctx(handle);
    if (!ctx || width <= 0 || height <= 0) return;

    /* Guard against size_t overflow on 32-bit targets. */
    uint64_t alloc_check = (uint64_t)width * (uint64_t)height * 4;
    if (alloc_check > SIZE_MAX) return;

    size_t buf_size = (size_t)alloc_check;
    uint8_t *new_pixels = calloc(1, buf_size);
    if (!new_pixels) return;

    free(ctx->pixels);
    ctx->pixels = new_pixels;
    ctx->width = width;
    ctx->height = height;
}

bool canvas2d_get_size(Canvas2DHandle handle, int *out_w, int *out_h)
{
    Canvas2DCtx *ctx = find_ctx(handle);
    if (!ctx) return false;
    if (out_w) *out_w = ctx->width;
    if (out_h) *out_h = ctx->height;
    return true;
}

uint8_t *canvas2d_get_pixels(Canvas2DHandle handle)
{
    Canvas2DCtx *ctx = find_ctx(handle);
    return ctx ? ctx->pixels : NULL;
}

size_t canvas2d_context_count(void)
{
    return s_ctx_count;
}

/* State setters */

void canvas2d_set_fill_color(Canvas2DHandle h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    Canvas2DCtx *ctx = find_ctx(h);
    if (!ctx) return;
    ctx->state.fill_r = r;
    ctx->state.fill_g = g;
    ctx->state.fill_b = b;
    ctx->state.fill_a = a;
}

void canvas2d_set_stroke_color(Canvas2DHandle h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    Canvas2DCtx *ctx = find_ctx(h);
    if (!ctx) return;
    ctx->state.stroke_r = r;
    ctx->state.stroke_g = g;
    ctx->state.stroke_b = b;
    ctx->state.stroke_a = a;
}

void canvas2d_set_global_alpha(Canvas2DHandle h, float alpha)
{
    Canvas2DCtx *ctx = find_ctx(h);
    if (!ctx) return;
    ctx->state.global_alpha = (alpha < 0.0f) ? 0.0f : (alpha > 1.0f) ? 1.0f : alpha;
}

void canvas2d_set_composite_op(Canvas2DHandle h, int op)
{
    Canvas2DCtx *ctx = find_ctx(h);
    if (!ctx) return;
    ctx->state.composite_op = op;
}

void canvas2d_set_line_width(Canvas2DHandle h, float width)
{
    Canvas2DCtx *ctx = find_ctx(h);
    if (!ctx) return;
    ctx->state.line_width = width;
}

void canvas2d_set_text_align(Canvas2DHandle h, int align)
{
    Canvas2DCtx *ctx = find_ctx(h);
    if (!ctx) return;
    ctx->state.text_align = align;
}

void canvas2d_set_text_baseline(Canvas2DHandle h, int baseline)
{
    Canvas2DCtx *ctx = find_ctx(h);
    if (!ctx) return;
    ctx->state.text_baseline = baseline;
}

/* State stack */

void canvas2d_save(Canvas2DHandle h)
{
    Canvas2DCtx *ctx = find_ctx(h);
    if (!ctx || ctx->state_depth >= STATE_STACK_MAX) return;
    copy_state(&ctx->state_stack[ctx->state_depth], &ctx->state);
    ctx->state_depth++;
}

void canvas2d_restore(Canvas2DHandle h)
{
    Canvas2DCtx *ctx = find_ctx(h);
    if (!ctx || ctx->state_depth <= 0) return;
    ctx->state_depth--;
    free_state(&ctx->state);
    ctx->state = ctx->state_stack[ctx->state_depth];
    /* The popped slot no longer owns font_name. */
    ctx->state_stack[ctx->state_depth].font_name = NULL;
}

/* Drawing operations */

void canvas2d_fill_rect(Canvas2DHandle h, int x, int y, int w, int h2)
{
    Canvas2DCtx *ctx = find_ctx(h);
    if (!ctx || w <= 0 || h2 <= 0) return;

    int x0 = clampi(x, 0, ctx->width);
    int y0 = clampi(y, 0, ctx->height);
    int x1 = clampi((int)((int64_t)x + w), 0, ctx->width);
    int y1 = clampi((int)((int64_t)y + h2), 0, ctx->height);

    uint8_t fr = ctx->state.fill_r;
    uint8_t fg = ctx->state.fill_g;
    uint8_t fb = ctx->state.fill_b;
    uint8_t fa = ctx->state.fill_a;
    float ga = ctx->state.global_alpha;
    int comp = ctx->state.composite_op;

    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            uint8_t *dst = ctx->pixels + ((size_t)py * ctx->width + px) * 4;
            blend_pixel(dst, fr, fg, fb, fa, ga, comp);
        }
    }
}

void canvas2d_clear_rect(Canvas2DHandle h, int x, int y, int w, int h2)
{
    Canvas2DCtx *ctx = find_ctx(h);
    if (!ctx || w <= 0 || h2 <= 0) return;

    int x0 = clampi(x, 0, ctx->width);
    int y0 = clampi(y, 0, ctx->height);
    int x1 = clampi((int)((int64_t)x + w), 0, ctx->width);
    int y1 = clampi((int)((int64_t)y + h2), 0, ctx->height);

    for (int py = y0; py < y1; py++) {
        uint8_t *row = ctx->pixels + (size_t)py * ctx->width * 4;
        memset(row + x0 * 4, 0, (size_t)(x1 - x0) * 4);
    }
}

void canvas2d_stroke_rect(Canvas2DHandle h, int x, int y, int w, int h2)
{
    Canvas2DCtx *ctx = find_ctx(h);
    if (!ctx || w <= 0 || h2 <= 0) return;

    uint8_t sr = ctx->state.stroke_r;
    uint8_t sg = ctx->state.stroke_g;
    uint8_t sb = ctx->state.stroke_b;
    uint8_t sa = ctx->state.stroke_a;
    float ga = ctx->state.global_alpha;
    int comp = ctx->state.composite_op;
    int lw = (int)(ctx->state.line_width + 0.5f);
    if (lw < 1) lw = 1;

    /* Clamped edges; int64 avoids overflow. */
    int top0 = clampi(y, 0, ctx->height);
    int top1 = clampi((int)((int64_t)y + lw), 0, ctx->height);
    int bot0 = clampi((int)((int64_t)y + h2 - lw), 0, ctx->height);
    int bot1 = clampi((int)((int64_t)y + h2), 0, ctx->height);
    int lft0 = clampi(x, 0, ctx->width);
    int lft1 = clampi((int)((int64_t)x + lw), 0, ctx->width);
    int rgt0 = clampi((int)((int64_t)x + w - lw), 0, ctx->width);
    int rgt1 = clampi((int)((int64_t)x + w), 0, ctx->width);

    /* Top edge */
    for (int py = top0; py < top1; py++) {
        for (int px = lft0; px < rgt1; px++) {
            blend_pixel(ctx->pixels + ((size_t)py * ctx->width + px) * 4,
                        sr, sg, sb, sa, ga, comp);
        }
    }
    /* Bottom edge */
    for (int py = bot0; py < bot1; py++) {
        for (int px = lft0; px < rgt1; px++) {
            blend_pixel(ctx->pixels + ((size_t)py * ctx->width + px) * 4,
                        sr, sg, sb, sa, ga, comp);
        }
    }
    /* Left edge */
    for (int py = top1; py < bot0; py++) {
        for (int px = lft0; px < lft1; px++) {
            blend_pixel(ctx->pixels + ((size_t)py * ctx->width + px) * 4,
                        sr, sg, sb, sa, ga, comp);
        }
    }
    /* Right edge */
    for (int py = top1; py < bot0; py++) {
        for (int px = rgt0; px < rgt1; px++) {
            blend_pixel(ctx->pixels + ((size_t)py * ctx->width + px) * 4,
                        sr, sg, sb, sa, ga, comp);
        }
    }
}

void canvas2d_draw_image(Canvas2DHandle h,
                          const uint8_t *src_pixels, int src_w, int src_h,
                          int sx, int sy, int sw, int sh,
                          int dx, int dy, int dw, int dh)
{
    Canvas2DCtx *ctx = find_ctx(h);
    if (!ctx || !src_pixels) return;
    /* Zero-size blits are legal no-ops (RPG Maker routinely draws 0-width
       window-frame slices). */
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;

    float ga = ctx->state.global_alpha;
    int comp = ctx->state.composite_op;

    for (int py = 0; py < dh; py++) {
        int dst_y = dy + py;
        if (dst_y < 0 || dst_y >= ctx->height) continue;

        for (int px = 0; px < dw; px++) {
            int dst_x = dx + px;
            if (dst_x < 0 || dst_x >= ctx->width) continue;

            /* Nearest-neighbor sampling; int64 avoids overflow. */
            int src_x = sx + (int)(((int64_t)px * sw) / dw);
            int src_y = sy + (int)(((int64_t)py * sh) / dh);
            if (src_x < 0 || src_x >= src_w || src_y < 0 || src_y >= src_h) continue;

            const uint8_t *s = src_pixels + ((size_t)src_y * src_w + src_x) * 4;
            uint8_t *d = ctx->pixels + ((size_t)dst_y * ctx->width + dst_x) * 4;
            blend_pixel(d, s[0], s[1], s[2], s[3], ga, comp);
        }
    }
}

/* Pixel manipulation */

void canvas2d_get_image_data(Canvas2DHandle h, int x, int y, int w, int h2,
                              uint8_t *out)
{
    Canvas2DCtx *ctx = find_ctx(h);
    if (!ctx || !out || w <= 0 || h2 <= 0) return;

    for (int py = 0; py < h2; py++) {
        int src_y = y + py;
        uint8_t *dst_row = out + (size_t)py * w * 4;
        if (src_y < 0 || src_y >= ctx->height) {
            memset(dst_row, 0, (size_t)w * 4);
            continue;
        }
        for (int px = 0; px < w; px++) {
            int src_x = x + px;
            uint8_t *dst = dst_row + px * 4;
            if (src_x < 0 || src_x >= ctx->width) {
                dst[0] = dst[1] = dst[2] = dst[3] = 0;
            } else {
                const uint8_t *src = ctx->pixels + ((size_t)src_y * ctx->width + src_x) * 4;
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = src[3];
            }
        }
    }
}

void canvas2d_put_image_data(Canvas2DHandle h, const uint8_t *data,
                              int x, int y, int w, int h2)
{
    Canvas2DCtx *ctx = find_ctx(h);
    if (!ctx || !data || w <= 0 || h2 <= 0) return;

    for (int py = 0; py < h2; py++) {
        int dst_y = y + py;
        if (dst_y < 0 || dst_y >= ctx->height) continue;
        for (int px = 0; px < w; px++) {
            int dst_x = x + px;
            if (dst_x < 0 || dst_x >= ctx->width) continue;

            const uint8_t *src = data + ((size_t)py * w + px) * 4;
            uint8_t *dst = ctx->pixels + ((size_t)dst_y * ctx->width + dst_x) * 4;
            /* putImageData writes raw pixels without blending. */
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
        }
    }
}

/* Font loading and text rendering */

static bool load_sfnt(const char *name, const uint8_t *data, size_t size);

bool canvas2d_load_font(const char *name, const uint8_t *data, size_t size)
{
    if (woff_is_woff(data, size)) {
        size_t sfnt_size = 0;
        uint8_t *sfnt = woff_to_sfnt(data, size, &sfnt_size);
        if (!sfnt) return false;
        bool ok = load_sfnt(name, sfnt, sfnt_size);
        free(sfnt);
        return ok;
    }
    return load_sfnt(name, data, size);
}

static bool load_sfnt(const char *name, const uint8_t *data, size_t size)
{
    /* stb_truetype needs at least the TrueType offset table (12 bytes). */
    if (!name || !data || size < 12) return false;

    /* Replacing an existing entry reuses its slot, so it works on a full table. */
    FontEntry *existing = find_font(name);
    if (existing) {
        uint8_t *new_data = malloc(size);
        if (!new_data) return false;   /* keep the old font intact on OOM */
        memcpy(new_data, data, size);
        free(existing->data);
        existing->data = new_data;
        existing->data_size = size;
        existing->valid = stbtt_InitFont(&existing->info, existing->data, 0);
        if (!existing->valid) {
            /* Unparseable font: release and compact rather than keep a dead entry. */
            free(existing->data);
            free(existing->name);
            int idx = (int)(existing - s_fonts);
            s_font_count--;
            if (idx != s_font_count) s_fonts[idx] = s_fonts[s_font_count];
            memset(&s_fonts[s_font_count], 0, sizeof(FontEntry));
            return false;
        }
        return true;
    }

    if (s_font_count >= MAX_FONTS) return false;

    FontEntry *fe = &s_fonts[s_font_count];
    fe->name = strdup(name);
    fe->data = malloc(size);
    if (!fe->data) { free(fe->name); return false; }
    memcpy(fe->data, data, size);
    fe->data_size = size;
    fe->valid = stbtt_InitFont(&fe->info, fe->data, 0);
    if (!fe->valid) {
        free(fe->name);
        free(fe->data);
        return false;
    }
    s_font_count++;
    return true;
}

void canvas2d_set_font(Canvas2DHandle h, const char *name, float pixel_size)
{
    Canvas2DCtx *ctx = find_ctx(h);
    if (!ctx) return;
    free(ctx->state.font_name);
    ctx->state.font_name = name ? strdup(name) : NULL;
    ctx->state.font_size = pixel_size > 0.0f ? pixel_size : 10.0f;
}

/* Vertical offset from the requested baseline to the alphabetic baseline. */
static float get_baseline_offset(stbtt_fontinfo *fi, float scale, int baseline)
{
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(fi, &ascent, &descent, &lineGap);

    float a = ascent * scale;
    float d = descent * scale;

    switch (baseline) {
        case CANVAS2D_BASELINE_TOP:
            return a;
        case CANVAS2D_BASELINE_HANGING:
            return a * 0.8f;
        case CANVAS2D_BASELINE_MIDDLE:
            return (a + d) * 0.5f;
        case CANVAS2D_BASELINE_ALPHABETIC:
            return 0.0f;
        case CANVAS2D_BASELINE_IDEOGRAPHIC:
            return d;
        case CANVAS2D_BASELINE_BOTTOM:
            return d;
        default:
            return 0.0f;
    }
}

/* Decode one UTF-8 sequence, advancing *p. Invalid bytes become '?'. */
static int utf8_next(const char **pp)
{
    const unsigned char *p = (const unsigned char *)*pp;
    int cp;
    if ((*p & 0x80) == 0) {
        cp = *p; p += 1;
    } else if ((*p & 0xE0) == 0xC0 && p[1]) {
        cp = ((*p & 0x1F) << 6) | (p[1] & 0x3F); p += 2;
    } else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
        cp = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3;
    } else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
        cp = ((*p & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); p += 4;
    } else {
        cp = '?'; p += 1;
    }
    *pp = (const char *)p;
    return cp;
}

static int utf8_peek(const char *p)
{
    return *p ? utf8_next(&p) : 0;
}

/* Read a whole file; NULL when it cannot be opened. */
static uint8_t *read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len <= 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *data = malloc((size_t)len);
    if (!data) { fclose(f); return NULL; }
    size_t got = fread(data, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) { free(data); return NULL; }
    *out_size = (size_t)len;
    return data;
}

static FontEntry *load_fallback_font(size_t i)
{
    if (s_fallback_state[i] == 1) return &s_fallback_fonts[i];
    if (s_fallback_state[i] == -1) return NULL;
    s_fallback_state[i] = -1;

    const char *file = SYSTEM_FALLBACK_FILES[i];
    uint8_t *data = NULL;
    size_t size = 0;
#ifdef _WIN32
    const char *dirs[2] = { getenv("WINDIR"), getenv("LOCALAPPDATA") };
    const char *subs[2] = { "\\Fonts\\", "\\Microsoft\\Windows\\Fonts\\" };
    for (int d = 0; d < 2 && !data; d++) {
        if (!dirs[d]) continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s%s%s", dirs[d], subs[d], file);
        data = read_file(path, &size);
    }
#else
    data = read_file(file, &size);
#endif
    if (!data) return NULL;

    /* .ttc collections: use the first face. */
    int offset = stbtt_GetFontOffsetForIndex(data, 0);
    FontEntry *fe = &s_fallback_fonts[i];
    if (offset < 0 || !stbtt_InitFont(&fe->info, data, offset)) {
        free(data);
        return NULL;
    }
    fe->name = strdup(file);
    fe->data = data;
    fe->data_size = size;
    fe->valid = true;
    s_fallback_state[i] = 1;
    return fe;
}

typedef struct {
    FontEntry *fe;
    int        glyph;     /* 0 = notdef in fe */
} GlyphRef;

/* The font that can draw `cp`: the requested font, another game font, or a
   system fallback. Whitespace and control characters stay with the
   requested font so their advances match its metrics. */
static GlyphRef resolve_glyph(FontEntry *primary, int cp)
{
    GlyphRef r = { primary, stbtt_FindGlyphIndex(&primary->info, cp) };
    if (r.glyph || cp <= 0x20 || cp == 0x3000 || cp == 0xA0) return r;

    for (int i = 0; i < s_font_count; i++) {
        FontEntry *fe = &s_fonts[i];
        if (!fe->valid || fe == primary) continue;
        int g = stbtt_FindGlyphIndex(&fe->info, cp);
        if (g) { r.fe = fe; r.glyph = g; return r; }
    }
    for (size_t i = 0; i < SYSTEM_FALLBACK_COUNT; i++) {
        FontEntry *fe = load_fallback_font(i);
        if (!fe) continue;
        int g = stbtt_FindGlyphIndex(&fe->info, cp);
        if (g) { r.fe = fe; r.glyph = g; return r; }
    }
    return r;
}

/* Advance of `cp` at pixel_size, including kerning with next_cp when both
   come from the same font. */
static float glyph_advance(FontEntry *primary, float pixel_size, int cp, int next_cp, GlyphRef *out)
{
    GlyphRef g = resolve_glyph(primary, cp);
    float scale = stbtt_ScaleForMappingEmToPixels(&g.fe->info, pixel_size);
    int advance, lsb;
    stbtt_GetGlyphHMetrics(&g.fe->info, g.glyph, &advance, &lsb);
    float width = advance * scale;
    if (next_cp) {
        GlyphRef n = resolve_glyph(primary, next_cp);
        if (n.fe == g.fe) {
            width += stbtt_GetGlyphKernAdvance(&g.fe->info, g.glyph, n.glyph) * scale;
        }
    }
    if (out) *out = g;
    return width;
}

static float measure_text_internal(FontEntry *fe, float pixel_size, const char *text)
{
    if (!fe || !text || !*text) return 0.0f;

    float width = 0.0f;
    const char *p = text;
    while (*p) {
        int codepoint = utf8_next(&p);
        width += glyph_advance(fe, pixel_size, codepoint, utf8_peek(p), NULL);
    }
    return width;
}

float canvas2d_measure_text(Canvas2DHandle h, const char *text)
{
    Canvas2DCtx *ctx = find_ctx(h);
    if (!ctx || !text) return 0.0f;

    FontEntry *fe = find_font(ctx->state.font_name);
    if (!fe) {
        /* Fall back to the first loaded font. */
        for (int i = 0; i < s_font_count; i++) {
            if (s_fonts[i].valid) { fe = &s_fonts[i]; break; }
        }
    }
    if (!fe) {
        /* No font loaded: rough estimate of 0.6 * size per character. */
        int len = 0;
        const char *p = text;
        while (*p) { len++; p++; }
        return len * ctx->state.font_size * 0.6f;
    }

    return measure_text_internal(fe, ctx->state.font_size, text);
}

/* Browser text rasterizers (Skia/DirectWrite) boost partial coverage so
   glyphs read heavier than linear anti-aliasing; without it, text here is
   visibly lighter than in the NW.js player. Coverage is mapped through
   c^(1/TEXT_GAMMA), built once. */
#define TEXT_GAMMA 2.2f
static uint8_t s_text_gamma[256];
static bool    s_text_gamma_ready = false;

static void init_text_gamma(void)
{
    if (s_text_gamma_ready) return;
    for (int i = 0; i < 256; i++) {
        float c = powf((float)i / 255.0f, 1.0f / TEXT_GAMMA);
        int v = (int)(c * 255.0f + 0.5f);
        s_text_gamma[i] = (uint8_t)(v > 255 ? 255 : v);
    }
    s_text_gamma_ready = true;
}

/* Grow glyph coverage outward by `radius` pixels (max over a disc), the
   way a stroke of width 2*radius spreads beyond the glyph outline. Returns a
   malloc'd (gw + 2*margin) x (gh + 2*margin) bitmap. */
static uint8_t *dilate_coverage(const uint8_t *bitmap, int gw, int gh,
                                float radius, int *margin_out)
{
    int m = (int)(radius + 0.999f);
    int dw = gw + 2 * m, dh = gh + 2 * m;
    uint8_t *out = calloc((size_t)dw * (size_t)dh, 1);
    if (!out) return NULL;
    float r2 = radius * radius + 0.01f;
    for (int y = 0; y < dh; y++) {
        for (int x = 0; x < dw; x++) {
            uint8_t best = 0;
            for (int dy = -m; dy <= m && best < 255; dy++) {
                int sy = y - m + dy;
                if (sy < 0 || sy >= gh) continue;
                for (int dx = -m; dx <= m; dx++) {
                    if ((float)(dx * dx + dy * dy) > r2) continue;
                    int sx = x - m + dx;
                    if (sx < 0 || sx >= gw) continue;
                    uint8_t v = bitmap[sy * gw + sx];
                    if (v > best) best = v;
                }
            }
            out[y * dw + x] = best;
        }
    }
    *margin_out = m;
    return out;
}

/* Draw text in the given colour. stroke_radius > 0 draws the glyphs
   expanded by that many pixels (a stroke of width 2 * stroke_radius, as
   Canvas strokeText spreads half the line width outside the outline). */
static void draw_text(Canvas2DCtx *ctx, const char *text, float x, float y,
                      uint8_t fr, uint8_t fg, uint8_t fb, uint8_t fa,
                      float stroke_radius)
{
    if (!ctx || !text || !*text) return;
    init_text_gamma();

    FontEntry *fe = find_font(ctx->state.font_name);
    if (!fe) {
        /* Fall back to the first loaded font. */
        for (int i = 0; i < s_font_count; i++) {
            if (s_fonts[i].valid) { fe = &s_fonts[i]; break; }
        }
        if (!fe) return;
    }

    float pixel_size = ctx->state.font_size;
    float scale = stbtt_ScaleForMappingEmToPixels(&fe->info, pixel_size);

    float text_width = 0.0f;
    int align = ctx->state.text_align;
    if (align == CANVAS2D_ALIGN_CENTER || align == CANVAS2D_ALIGN_RIGHT ||
        align == CANVAS2D_ALIGN_END) {
        text_width = measure_text_internal(fe, pixel_size, text);
        if (align == CANVAS2D_ALIGN_CENTER) {
            x -= text_width * 0.5f;
        } else {
            x -= text_width;
        }
    }

    float base_offset = get_baseline_offset(&fe->info, scale, ctx->state.text_baseline);
    float draw_y = y + base_offset;

    float ga = ctx->state.global_alpha;
    int comp = ctx->state.composite_op;

    float cursor_x = x;
    const char *p = text;
    while (*p) {
        int codepoint = utf8_next(&p);
        GlyphRef g;
        float advance = glyph_advance(fe, pixel_size, codepoint, utf8_peek(p), &g);
        float gscale = stbtt_ScaleForMappingEmToPixels(&g.fe->info, pixel_size);

        int gw, gh, gx, gy;
        uint8_t *bitmap = stbtt_GetGlyphBitmap(&g.fe->info, 0, gscale,
                                                g.glyph, &gw, &gh, &gx, &gy);
        if (bitmap && stroke_radius > 0.0f) {
            int margin = 0;
            uint8_t *fat = dilate_coverage(bitmap, gw, gh, stroke_radius, &margin);
            stbtt_FreeBitmap(bitmap, NULL);
            bitmap = fat;           /* freed with free() below */
            gw += 2 * margin;
            gh += 2 * margin;
            gx -= margin;
            gy -= margin;
        }
        if (bitmap) {
            int bx = (int)(cursor_x + 0.5f) + gx;
            int by = (int)(draw_y + 0.5f) + gy;

            for (int row = 0; row < gh; row++) {
                int dst_y = by + row;
                if (dst_y < 0 || dst_y >= ctx->height) continue;
                for (int col = 0; col < gw; col++) {
                    int dst_x = bx + col;
                    if (dst_x < 0 || dst_x >= ctx->width) continue;

                    uint8_t coverage = s_text_gamma[bitmap[row * gw + col]];
                    if (coverage == 0) continue;

                    /* Modulate fill alpha by glyph coverage. */
                    uint8_t glyph_a = (uint8_t)((fa * coverage + 127) / 255);
                    uint8_t *dst = ctx->pixels + ((size_t)dst_y * ctx->width + dst_x) * 4;
                    blend_pixel(dst, fr, fg, fb, glyph_a, ga, comp);
                }
            }
            if (stroke_radius > 0.0f) free(bitmap);
            else stbtt_FreeBitmap(bitmap, NULL);
        }

        cursor_x += advance;
    }
}

void canvas2d_fill_text(Canvas2DHandle h, const char *text, float x, float y)
{
    Canvas2DCtx *ctx = find_ctx(h);
    if (!ctx) return;
    draw_text(ctx, text, x, y, ctx->state.fill_r, ctx->state.fill_g,
              ctx->state.fill_b, ctx->state.fill_a, 0.0f);
}

void canvas2d_stroke_text(Canvas2DHandle h, const char *text, float x, float y)
{
    /* The stroke straddles the outline: half the line width lies outside
       the glyph, which is what shows once fillText paints over the inside
       (RPG Maker draws every string as outline then fill). */
    Canvas2DCtx *ctx = find_ctx(h);
    if (!ctx) return;
    float radius = ctx->state.line_width * 0.5f;
    if (radius < 0.5f) radius = 0.5f;
    draw_text(ctx, text, x, y, ctx->state.stroke_r, ctx->state.stroke_g,
              ctx->state.stroke_b, ctx->state.stroke_a, radius);
}
