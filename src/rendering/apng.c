/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#include "rendering/apng.h"

/* Prototypes only; the implementation is compiled in image_loader.c. */
#include "stb_image.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Each frame is rebuilt as a standalone PNG (signature, IHDR sized to the
   frame, palette chunks, the frame's IDAT/fdAT data, IEND) and handed to
   stb_image, which does not verify chunk CRCs. Frames are then composed
   onto a canvas following the APNG dispose_op / blend_op rules. */

#define APNG_MAX_TOTAL_BYTES (512u * 1024u * 1024u)

static const uint8_t PNG_SIGNATURE[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static bool tag_is(const uint8_t *type, const char *name)
{
    return memcmp(type, name, 4) == 0;
}

/* Growable byte buffer for the reconstructed per-frame PNG. */
typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} Buf;

static bool buf_append(Buf *b, const void *src, size_t n)
{
    if (b->len + n > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        while (cap < b->len + n) cap *= 2;
        uint8_t *p = realloc(b->data, cap);
        if (!p) return false;
        b->data = p;
        b->cap = cap;
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
    return true;
}

/* Append a chunk; the CRC is left as zero (stb_image skips it). */
static bool buf_chunk(Buf *b, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t hdr[8];
    wr32(hdr, len);
    memcpy(hdr + 4, type, 4);
    uint8_t crc[4] = { 0, 0, 0, 0 };
    return buf_append(b, hdr, 8) &&
           (len == 0 || buf_append(b, data, len)) &&
           buf_append(b, crc, 4);
}

typedef struct {
    uint32_t width, height, x, y;
    uint16_t delay_num, delay_den;
    uint8_t  dispose, blend;
} FrameControl;

typedef struct {
    const uint8_t *ihdr;            /* 13 bytes */
    const uint8_t *plte; uint32_t plte_len;
    const uint8_t *trns; uint32_t trns_len;
} SharedChunks;

/* Straight-alpha "over" composite of one pixel. */
static void blend_over(uint8_t *dst, const uint8_t *src)
{
    unsigned sa = src[3];
    if (sa == 255) { memcpy(dst, src, 4); return; }
    if (sa == 0) return;
    unsigned da = dst[3];
    unsigned oa = sa + da * (255 - sa) / 255;
    if (oa == 0) { dst[0] = dst[1] = dst[2] = dst[3] = 0; return; }
    for (int c = 0; c < 3; c++) {
        unsigned v = (src[c] * sa + dst[c] * da * (255 - sa) / 255) / oa;
        dst[c] = (uint8_t)(v > 255 ? 255 : v);
    }
    dst[3] = (uint8_t)oa;
}

static void clear_region(uint8_t *canvas, int cw, const FrameControl *fc)
{
    for (uint32_t row = 0; row < fc->height; row++) {
        memset(canvas + (((size_t)(fc->y + row) * (size_t)cw + fc->x) * 4), 0, (size_t)fc->width * 4);
    }
}

bool apng_is_apng(const uint8_t *data, size_t size)
{
    if (!data || size < 8 + 25 || memcmp(data, PNG_SIGNATURE, 8) != 0) return false;
    size_t pos = 8;
    while (pos + 12 <= size) {
        uint32_t len = rd32(data + pos);
        const uint8_t *type = data + pos + 4;
        if (tag_is(type, "acTL")) return true;
        if (tag_is(type, "IDAT") || tag_is(type, "IEND")) return false;
        if (len > size - pos - 12) return false;
        pos += 12 + len;
    }
    return false;
}

/* Decode the frame PNG accumulated in `png` and compose it onto the canvas. */
static bool finish_frame(ApngImage *img, int *frame_index, Buf *png,
                         const FrameControl *fc, uint8_t *canvas,
                         uint8_t **saved_canvas, const FrameControl *prev_fc)
{
    uint8_t iend[12] = { 0, 0, 0, 0, 'I', 'E', 'N', 'D', 0, 0, 0, 0 };
    if (!buf_append(png, iend, sizeof(iend))) return false;
    if (png->len > INT_MAX) return false;

    int w = 0, h = 0, ch = 0;
    uint8_t *pixels = stbi_load_from_memory(png->data, (int)png->len, &w, &h, &ch, 4);
    if (!pixels || (uint32_t)w != fc->width || (uint32_t)h != fc->height) {
        stbi_image_free(pixels);
        return false;
    }

    size_t canvas_bytes = (size_t)img->width * (size_t)img->height * 4;

    /* Dispose of the previous frame's region first. */
    if (prev_fc) {
        if (prev_fc->dispose == 2 && *saved_canvas) {
            memcpy(canvas, *saved_canvas, canvas_bytes);
        } else if (prev_fc->dispose == 1) {
            clear_region(canvas, img->width, prev_fc);
        }
    }
    /* "Previous" disposal needs the canvas as it was before this frame. */
    if (fc->dispose == 2) {
        if (!*saved_canvas) {
            *saved_canvas = malloc(canvas_bytes);
            if (!*saved_canvas) { stbi_image_free(pixels); return false; }
        }
        memcpy(*saved_canvas, canvas, canvas_bytes);
    }

    for (uint32_t row = 0; row < fc->height; row++) {
        uint8_t *dst = canvas + (((size_t)(fc->y + row) * (size_t)img->width + fc->x) * 4);
        const uint8_t *src = pixels + (size_t)row * fc->width * 4;
        if (fc->blend == 0) {
            memcpy(dst, src, (size_t)fc->width * 4);
        } else {
            for (uint32_t col = 0; col < fc->width; col++) {
                blend_over(dst + col * 4, src + col * 4);
            }
        }
    }
    stbi_image_free(pixels);

    ApngFrame *out = &img->frames[*frame_index];
    out->rgba = malloc(canvas_bytes);
    if (!out->rgba) return false;
    memcpy(out->rgba, canvas, canvas_bytes);
    uint32_t den = fc->delay_den ? fc->delay_den : 100;
    out->delay_ms = (int)(((uint64_t)fc->delay_num * 1000u + den / 2) / den);
    (*frame_index)++;
    return true;
}

void apng_free(ApngImage *image)
{
    if (!image) return;
    if (image->frames) {
        for (int i = 0; i < image->num_frames; i++) free(image->frames[i].rgba);
        free(image->frames);
    }
    free(image);
}

ApngImage *apng_decode(const uint8_t *data, size_t size)
{
    if (!apng_is_apng(data, size)) return NULL;

    /* Pass 1: header, animation control, palette chunks, frame count. */
    SharedChunks shared = { 0 };
    uint32_t declared_frames = 0, num_plays = 0;
    size_t pos = 8;
    while (pos + 12 <= size) {
        uint32_t len = rd32(data + pos);
        const uint8_t *type = data + pos + 4;
        const uint8_t *body = data + pos + 8;
        if (len > size - pos - 12) return NULL;
        if (tag_is(type, "IHDR") && len == 13) shared.ihdr = body;
        else if (tag_is(type, "acTL") && len == 8) { declared_frames = rd32(body); num_plays = rd32(body + 4); }
        else if (tag_is(type, "PLTE")) { shared.plte = body; shared.plte_len = len; }
        else if (tag_is(type, "tRNS")) { shared.trns = body; shared.trns_len = len; }
        else if (tag_is(type, "IEND")) break;
        pos += 12 + len;
    }
    if (!shared.ihdr || declared_frames == 0 || declared_frames > 4096) return NULL;

    uint32_t width = rd32(shared.ihdr), height = rd32(shared.ihdr + 4);
    if (width == 0 || height == 0 || width > 16384 || height > 16384) return NULL;
    uint64_t canvas_bytes = (uint64_t)width * height * 4;
    if (canvas_bytes * (declared_frames + 1) > APNG_MAX_TOTAL_BYTES) return NULL;

    ApngImage *img = calloc(1, sizeof(ApngImage));
    if (!img) return NULL;
    img->width = (int)width;
    img->height = (int)height;
    img->num_plays = (int)num_plays;
    img->frames = calloc(declared_frames, sizeof(ApngFrame));
    uint8_t *canvas = calloc(1, (size_t)canvas_bytes);
    uint8_t *saved_canvas = NULL;
    Buf png = { 0 };
    bool ok = img->frames && canvas;

    /* Pass 2: walk the frames. */
    FrameControl fc = { 0 }, prev_fc = { 0 };
    bool have_fc = false, have_prev = false, in_frame = false;
    int frame_index = 0;
    pos = 8;
    while (ok && pos + 12 <= size) {
        uint32_t len = rd32(data + pos);
        const uint8_t *type = data + pos + 4;
        const uint8_t *body = data + pos + 8;

        if (tag_is(type, "fcTL") && len == 26) {
            if (in_frame) {
                ok = finish_frame(img, &frame_index, &png, &fc, canvas, &saved_canvas,
                                  have_prev ? &prev_fc : NULL);
                prev_fc = fc; have_prev = true; in_frame = false;
            }
            fc.width = rd32(body + 4);  fc.height = rd32(body + 8);
            fc.x = rd32(body + 12);     fc.y = rd32(body + 16);
            fc.delay_num = rd16(body + 20); fc.delay_den = rd16(body + 22);
            fc.dispose = body[24];      fc.blend = body[25];
            if (fc.width == 0 || fc.height == 0 || fc.x > width || fc.y > height ||
                fc.width > width - fc.x || fc.height > height - fc.y || fc.dispose > 2 || fc.blend > 1) {
                ok = false;
                break;
            }
            /* The first frame may not use "previous" disposal (treat as none). */
            if (frame_index == 0 && fc.dispose == 2) fc.dispose = 1;
            have_fc = true;
        } else if ((tag_is(type, "IDAT") || tag_is(type, "fdAT")) && have_fc) {
            if ((uint32_t)frame_index >= declared_frames) { ok = false; break; }
            if (!in_frame) {
                /* Start a standalone PNG for this frame. */
                png.len = 0;
                uint8_t ihdr[13];
                memcpy(ihdr, shared.ihdr, 13);
                wr32(ihdr, fc.width);
                wr32(ihdr + 4, fc.height);
                ok = buf_append(&png, PNG_SIGNATURE, 8) &&
                     buf_chunk(&png, "IHDR", ihdr, 13) &&
                     (!shared.plte || buf_chunk(&png, "PLTE", shared.plte, shared.plte_len)) &&
                     (!shared.trns || buf_chunk(&png, "tRNS", shared.trns, shared.trns_len));
                in_frame = true;
            }
            if (ok) {
                if (tag_is(type, "IDAT")) {
                    ok = buf_chunk(&png, "IDAT", body, len);
                } else if (len >= 4) {
                    ok = buf_chunk(&png, "IDAT", body + 4, len - 4);   /* drop sequence number */
                }
            }
        } else if (tag_is(type, "IEND")) {
            break;
        }
        /* IDAT before any fcTL is the default image, not part of the animation. */
        pos += 12 + len;
    }
    if (ok && in_frame) {
        ok = finish_frame(img, &frame_index, &png, &fc, canvas, &saved_canvas,
                          have_prev ? &prev_fc : NULL);
    }

    free(png.data);
    free(canvas);
    free(saved_canvas);

    if (!ok || frame_index == 0) {
        img->num_frames = frame_index;   /* so apng_free releases decoded frames */
        apng_free(img);
        return NULL;
    }
    img->num_frames = frame_index;
    return img;
}
