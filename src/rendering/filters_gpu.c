/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

/* SDL3 GPU implementation of filters.h.
 *
 * The GL backend set uniforms by name against a linked program. SDL3 has no
 * shader reflection: uniforms arrive as a block of bytes bound to a slot. Each
 * built-in shader therefore carries a table mapping the names the JS side uses
 * onto offsets in its constant buffer, and filter_set_uniform_* writes into a
 * staging block that filter_draw_quad records alongside the draw.
 *
 * Shaders the game supplies at run time (PIXI.Filter with its own GLSL) cannot
 * be compiled here -- SDL3 takes bytecode, not source -- so
 * filter_compile_shader returns 0 for them and the shim falls back to its
 * passthrough copy, as it already does when GL fails to compile a shader. */

#include "rendering/filters.h"
#include "rendering/gpu_backend.h"

#include <stdint.h>
#include <string.h>

/* Built-in shader sources. These are still the GLSL the OpenGL backend
   compiles; here they serve only as identity tokens, since bind_filters.c
   passes the pointer returned by the getters below straight back to
   filter_compile_shader. The shipped bytecode is built from the matching
   HLSL in src/rendering/shaders. */

static const char *DEFAULT_VERT_SRC = "/* built-in: fullscreen quad */";
static const char *COLOR_MATRIX_FRAG_SRC = "/* built-in: colour matrix */";
static const char *BLUR_FRAG_SRC = "/* built-in: gaussian blur */";
static const char *ALPHA_FRAG_SRC = "/* built-in: alpha */";
static const char *COLOR_FILTER_FRAG_SRC = "/* built-in: rmmz colour filter */";
static const char *MASK_FRAG_SRC = "/* built-in: sprite mask */";

typedef struct {
    const char *name;
    uint32_t    offset;
} UniformSlot;

/* Offsets match the cbuffer layouts in src/rendering/shaders/*.hlsl. */

static const UniformSlot COLOR_MATRIX_UNIFORMS[] = {
    { "u_colorMatrix", 0 },
    { "u_colorOffset", 64 },
    { NULL, 0 },
};

static const UniformSlot BLUR_UNIFORMS[] = {
    { "u_direction", 0 },
    { "u_strength", 8 },
    { NULL, 0 },
};

static const UniformSlot ALPHA_UNIFORMS[] = {
    { "u_alpha", 0 },
    { NULL, 0 },
};

static const UniformSlot COLOR_FILTER_UNIFORMS[] = {
    { "colorTone", 0 },
    { "blendColor", 16 },
    { "hue", 32 },
    { "brightness", 36 },
    { NULL, 0 },
};

typedef struct {
    const UniformSlot *slots;
    uint32_t           size;   /* constant buffer size, a multiple of 16 */
} ShaderInfo;

static const ShaderInfo SHADER_INFO[GPU_SHADER_COUNT] = {
    [GPU_SHADER_COLOR_MATRIX] = { COLOR_MATRIX_UNIFORMS, 80 },
    [GPU_SHADER_BLUR]         = { BLUR_UNIFORMS,         16 },
    [GPU_SHADER_ALPHA]        = { ALPHA_UNIFORMS,        16 },
    [GPU_SHADER_COLOR_FILTER] = { COLOR_FILTER_UNIFORMS, 48 },
    [GPU_SHADER_MASK]         = { NULL,                   0 },
};

static int s_initialized = 0;

/* State between filter_begin() and filter_end(). */
static struct {
    int      shader;
    uint32_t texture;
    uint32_t mask_texture;
    uint8_t  uniforms[GPU_MAX_UNIFORM_BYTES];
} s_current;

void filters_init(void)
{
    if (s_initialized) return;
    s_initialized = 1;
}

void filters_shutdown(void)
{
    s_initialized = 0;
}

uint32_t filter_compile_shader(const char *vert_src, const char *frag_src)
{
    (void)vert_src;
    if (!frag_src) return 0;

    if (frag_src == COLOR_MATRIX_FRAG_SRC) return GPU_SHADER_COLOR_MATRIX;
    if (frag_src == BLUR_FRAG_SRC)         return GPU_SHADER_BLUR;
    if (frag_src == ALPHA_FRAG_SRC)        return GPU_SHADER_ALPHA;
    if (frag_src == COLOR_FILTER_FRAG_SRC) return GPU_SHADER_COLOR_FILTER;
    if (frag_src == MASK_FRAG_SRC)         return GPU_SHADER_MASK;

    /* Game-supplied GLSL: unsupported, so the caller draws unfiltered. */
    return 0;
}

void filter_delete_shader(uint32_t program)
{
    /* Built-in pipelines live for the lifetime of the device. */
    (void)program;
}

void filter_begin(uint32_t program, uint32_t input_texture, int width, int height)
{
    (void)width; (void)height;
    s_current.shader = (int)program;
    s_current.texture = input_texture;
    s_current.mask_texture = 0;
    memset(s_current.uniforms, 0, sizeof(s_current.uniforms));
}

static const UniformSlot *find_slot(int shader, const char *name)
{
    if (shader <= 0 || shader >= GPU_SHADER_COUNT) return NULL;
    const UniformSlot *slots = SHADER_INFO[shader].slots;
    if (!slots || !name) return NULL;
    for (const UniformSlot *s = slots; s->name; s++) {
        if (strcmp(s->name, name) == 0) return s;
    }
    return NULL;
}

static void write_uniform(const char *name, const float *values, size_t count)
{
    const UniformSlot *slot = find_slot(s_current.shader, name);
    if (!slot) return;
    if (slot->offset + count * sizeof(float) > sizeof(s_current.uniforms)) return;
    memcpy(s_current.uniforms + slot->offset, values, count * sizeof(float));
}

void filter_set_uniform_texture(uint32_t program, const char *name,
                                uint32_t texture, int unit)
{
    (void)program; (void)unit;
    /* The mask shader is the only one with a second sampler. */
    if (name && strcmp(name, "u_mask") == 0)
        s_current.mask_texture = texture;
}

void filter_set_uniform_1f(uint32_t program, const char *name, float value)
{
    (void)program;
    write_uniform(name, &value, 1);
}

void filter_set_uniform_2f(uint32_t program, const char *name, float x, float y)
{
    (void)program;
    float v[2] = { x, y };
    write_uniform(name, v, 2);
}

void filter_set_uniform_4f(uint32_t program, const char *name,
                           float x, float y, float z, float w)
{
    (void)program;
    float v[4] = { x, y, z, w };
    write_uniform(name, v, 4);
}

void filter_set_uniform_mat4(uint32_t program, const char *name, const float *values)
{
    (void)program;
    if (!values) return;
    write_uniform(name, values, 16);
}

void filter_draw_quad(void)
{
    if (s_current.shader <= 0 || s_current.shader >= GPU_SHADER_COUNT) return;
    uint32_t size = SHADER_INFO[s_current.shader].size;
    gpu_record_filter(s_current.shader, s_current.texture, s_current.mask_texture,
                      size ? s_current.uniforms : NULL, size);
}

void filter_end(void)
{
    s_current.shader = 0;
    s_current.texture = 0;
    s_current.mask_texture = 0;
}

const char *filter_default_vert_src(void)      { return DEFAULT_VERT_SRC; }
const char *filter_color_matrix_frag_src(void) { return COLOR_MATRIX_FRAG_SRC; }
const char *filter_blur_frag_src(void)         { return BLUR_FRAG_SRC; }
const char *filter_alpha_frag_src(void)        { return ALPHA_FRAG_SRC; }
const char *filter_color_filter_frag_src(void) { return COLOR_FILTER_FRAG_SRC; }
const char *filter_mask_frag_src(void)         { return MASK_FRAG_SRC; }
