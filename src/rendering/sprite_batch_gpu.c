/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

/* SDL3 GPU implementation of sprite_batch.h. Quad building, culling and the
   flush-on-texture-change policy are the same as the OpenGL backend; the only
   difference is that a flush records a draw rather than issuing one. */

#include "rendering/sprite_batch.h"
#include "rendering/gpu_backend.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define VERTS_PER_QUAD 4

struct SpriteBatch {
    int        max_quads;
    int        quad_count;
    int        frame_quads;
    int        frame_culled;
    int        draw_calls;
    uint32_t   current_texture;
    int        current_blend;
    GpuVertex *vertices;

    float      viewport_w;
    float      viewport_h;

    uint32_t   white_texture;   /* 1x1 white pixel for untextured draws */
    float      proj[16];
};

/* Column-major orthographic matrix mapping (0,0)-(w,h) to NDC.
   SDL3 puts NDC +1 at a render target's *first* row, the opposite of OpenGL,
   so this matrix is the mirror of the GL backend's: sending game y=0 to NDC -1
   lands it on the last row and leaves the target stored bottom-up, which is
   the layout the JS side inherited from the GL backend. */
static void ortho_projection(float *m, float w, float h)
{
    memset(m, 0, 16 * sizeof(float));
    m[0]  =  2.0f / w;
    m[5]  =  2.0f / h;
    m[10] = -1.0f;
    m[12] = -1.0f;
    m[13] = -1.0f;
    m[15] =  1.0f;
}

/* 1.0 when the source texture is already premultiplied. */
static float premultiplied_flag(int mode)
{
    return (mode == BLEND_MODE_NORMAL_PREMULT) ? 1.0f : 0.0f;
}

static uint32_t create_white_texture(void)
{
    uint32_t tex = gpu_texture_create(1, 1, false);
    if (!tex) return 0;
    const uint8_t white[4] = { 255, 255, 255, 255 };
    gpu_texture_upload(tex, 1, 1, white);
    return tex;
}

SpriteBatch *sprite_batch_create(int max_quads)
{
    if (max_quads <= 0) max_quads = 4096;
    if (max_quads > 16384) max_quads = 16384;

    SpriteBatch *sb = calloc(1, sizeof(SpriteBatch));
    if (!sb) return NULL;

    sb->max_quads = max_quads;
    sb->viewport_w = 816.0f;
    sb->viewport_h = 624.0f;
    sb->vertices = malloc(sizeof(GpuVertex) * (size_t)max_quads * VERTS_PER_QUAD);
    if (!sb->vertices) {
        free(sb);
        return NULL;
    }

    ortho_projection(sb->proj, 816.0f, 624.0f);
    sb->white_texture = create_white_texture();
    return sb;
}

void sprite_batch_destroy(SpriteBatch *sb)
{
    if (!sb) return;
    if (sb->white_texture) gpu_texture_destroy(sb->white_texture);
    free(sb->vertices);
    free(sb);
}

void sprite_batch_set_projection(SpriteBatch *sb, float width, float height)
{
    if (!sb) return;
    ortho_projection(sb->proj, width, height);
    sb->viewport_w = width;
    sb->viewport_h = height;
}

void sprite_batch_begin(SpriteBatch *sb)
{
    if (!sb) return;
    sb->quad_count = 0;
    sb->frame_quads = 0;
    sb->frame_culled = 0;
    sb->draw_calls = 0;
    sb->current_texture = 0;
    sb->current_blend = BLEND_MODE_NORMAL;
}

void sprite_batch_draw(SpriteBatch *sb, uint32_t texture,
                       float x, float y, float w, float h,
                       float u0, float v0, float u1, float v1,
                       uint32_t tint, float alpha)
{
    sprite_batch_draw_verts(sb, texture,
                            x,     y,
                            x + w, y,
                            x + w, y + h,
                            x,     y + h,
                            u0, v0, u1, v1, tint, alpha);
}

void sprite_batch_draw_verts(SpriteBatch *sb, uint32_t texture,
                             float x0, float y0, float x1, float y1,
                             float x2, float y2, float x3, float y3,
                             float u0, float v0, float u1, float v1,
                             uint32_t tint, float alpha)
{
    if (!sb) return;

    float min_x = x0, max_x = x0, min_y = y0, max_y = y0;
    float xs[3] = { x1, x2, x3 };
    float ys[3] = { y1, y2, y3 };
    for (int i = 0; i < 3; i++) {
        if (xs[i] < min_x) min_x = xs[i];
        if (xs[i] > max_x) max_x = xs[i];
        if (ys[i] < min_y) min_y = ys[i];
        if (ys[i] > max_y) max_y = ys[i];
    }
    if (max_x < 0.0f || min_x > sb->viewport_w ||
        max_y < 0.0f || min_y > sb->viewport_h) {
        sb->frame_culled++;
        return;
    }

    if (alpha <= 0.0f) {
        sb->frame_culled++;
        return;
    }

    if (texture == 0) texture = sb->white_texture;

    if (sb->quad_count > 0 &&
        (texture != sb->current_texture || sb->quad_count >= sb->max_quads)) {
        sprite_batch_flush(sb);
    }
    sb->current_texture = texture;

    float tr = (float)((tint >> 16) & 0xFF) / 255.0f;
    float tg = (float)((tint >> 8) & 0xFF) / 255.0f;
    float tb = (float)(tint & 0xFF) / 255.0f;
    float ta = alpha;

    /* Slots TL, TR, BL, BR; the backend expands them into two triangles. */
    GpuVertex *v = &sb->vertices[sb->quad_count * VERTS_PER_QUAD];
    v[0] = (GpuVertex){ x0, y0, u0, v0, tr, tg, tb, ta };
    v[1] = (GpuVertex){ x1, y1, u1, v0, tr, tg, tb, ta };
    v[2] = (GpuVertex){ x3, y3, u0, v1, tr, tg, tb, ta };
    v[3] = (GpuVertex){ x2, y2, u1, v1, tr, tg, tb, ta };

    sb->quad_count++;
}

void sprite_batch_draw_verts_uv(SpriteBatch *sb, uint32_t texture,
                                const float xy[8], const float uv[8],
                                uint32_t tint, float alpha)
{
    if (!sb || alpha <= 0.0f) return;

    float min_x = xy[0], max_x = xy[0], min_y = xy[1], max_y = xy[1];
    for (int i = 1; i < 4; i++) {
        if (xy[i * 2] < min_x) min_x = xy[i * 2];
        if (xy[i * 2] > max_x) max_x = xy[i * 2];
        if (xy[i * 2 + 1] < min_y) min_y = xy[i * 2 + 1];
        if (xy[i * 2 + 1] > max_y) max_y = xy[i * 2 + 1];
    }
    if (max_x < 0.0f || min_x > sb->viewport_w ||
        max_y < 0.0f || min_y > sb->viewport_h) {
        sb->frame_culled++;
        return;
    }

    if (texture == 0) texture = sb->white_texture;
    if (sb->quad_count > 0 &&
        (texture != sb->current_texture || sb->quad_count >= sb->max_quads)) {
        sprite_batch_flush(sb);
    }
    sb->current_texture = texture;

    float tr = (float)((tint >> 16) & 0xFF) / 255.0f;
    float tg = (float)((tint >> 8) & 0xFF) / 255.0f;
    float tb = (float)(tint & 0xFF) / 255.0f;

    GpuVertex *v = &sb->vertices[sb->quad_count * VERTS_PER_QUAD];
    v[0] = (GpuVertex){ xy[0], xy[1], uv[0], uv[1], tr, tg, tb, alpha };
    v[1] = (GpuVertex){ xy[2], xy[3], uv[2], uv[3], tr, tg, tb, alpha };
    v[2] = (GpuVertex){ xy[6], xy[7], uv[6], uv[7], tr, tg, tb, alpha };
    v[3] = (GpuVertex){ xy[4], xy[5], uv[4], uv[5], tr, tg, tb, alpha };
    sb->quad_count++;
}

void sprite_batch_flush(SpriteBatch *sb)
{
    if (!sb || sb->quad_count == 0) return;

    gpu_record_sprites(sb->vertices, sb->quad_count, sb->current_texture,
                       sb->current_blend, sb->proj,
                       premultiplied_flag(sb->current_blend));

    sb->frame_quads += sb->quad_count;
    sb->draw_calls++;
    sb->quad_count = 0;
}

void sprite_batch_end(SpriteBatch *sb)
{
    if (!sb) return;
    sprite_batch_flush(sb);
}

void sprite_batch_set_blend_mode(SpriteBatch *sb, int mode)
{
    if (!sb) return;
    if (mode == sb->current_blend) return;
    sprite_batch_flush(sb);
    sb->current_blend = mode;
}

int sprite_batch_get_draw_calls(const SpriteBatch *sb)
{
    return sb ? sb->draw_calls : 0;
}

int sprite_batch_get_quad_count(const SpriteBatch *sb)
{
    return sb ? sb->frame_quads : 0;
}

int sprite_batch_get_culled_count(const SpriteBatch *sb)
{
    return sb ? sb->frame_culled : 0;
}

void sprite_batch_rebind(SpriteBatch *sb)
{
    /* Nothing to rebind: pipeline and buffer bindings are chosen per recorded
       draw, so a filter pass cannot leave state behind. */
    (void)sb;
}
