/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

/* SDL3 GPU implementation of filters.h.
 *
 * The GL backend set uniforms by name against a linked program. SDL3 has no
 * shader reflection: uniforms arrive as a block of bytes bound to a slot. Each
 * built-in shader therefore carries a table mapping the names the JS side uses
 * onto offsets in its constant buffer, and filter_set_uniform_* writes into a
 * staging block that filter_draw_quad records alongside the draw.
 *
 * Shaders the game supplies at run time (PIXI.Filter with its own GLSL) are
 * rewritten by glsl_translate and compiled by gpu_shader_runtime. Their
 * uniform offsets come from that rewrite rather than a table here. When the
 * build has no runtime compiler, or the source defeats the rewrite,
 * filter_compile_shader returns 0 and the shim falls back to its passthrough
 * copy, as it already does when GL fails to compile a shader. */

#include "rendering/filters.h"
#include "rendering/gpu_backend.h"
#include "rendering/glsl_translate.h"

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

/* Largest uniform block a plugin filter may declare. Generous next to the
   built-ins, which need 80 bytes at most. */
#define RUNTIME_UNIFORM_BYTES 512

/* State between filter_begin() and filter_end(). */
static struct {
    int      shader;
    uint32_t textures[GPU_MAX_FILTER_TEXTURES];
    int      texture_count;
    uint8_t  uniforms[RUNTIME_UNIFORM_BYTES];
    /* Set for a shader the game supplied, whose uniform offsets come from the
       rewrite rather than a table here. */
    const GlslTranslation *layout;
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

    /* Anything else is the game's own GLSL. Translating and compiling it is
       the runtime shader path; a failure there returns 0 and the caller falls
       back to drawing unfiltered, as it does when GL rejects a shader. */
    return gpu_runtime_shader_create(frag_src);
}

void filter_delete_shader(uint32_t program)
{
    /* Built-in pipelines live for the lifetime of the device; a shader
       compiled from a game's source is owned by whoever asked for it. */
    if (program >= GPU_SHADER_COUNT) gpu_runtime_shader_destroy(program);
}

/* Uniforms PIXI fills in for a filter without the filter ever declaring them
   in JS. A plugin shader that samples neighbouring pixels reads its texel size
   from these, so leaving them at zero collapses every tap onto one point and
   the pass comes out blank. */
static void supply_pixi_uniforms(int width, int height)
{
    if (!s_current.layout || width <= 0 || height <= 0) return;

    const float w = (float)width, h = (float)height;
    const struct { const char *name; int count; float v[4]; } builtins[] = {
        /* PIXI 4: size in xy, source offset in zw. */
        { "filterArea",  4, { w, h, 0.0f, 0.0f } },
        /* Half-texel inset, so a clamped tap cannot bleed past the edge. */
        { "filterClamp", 4, { 0.5f / w, 0.5f / h, (w - 0.5f) / w, (h - 0.5f) / h } },
        { "dimensions",  2, { w, h, 0.0f, 0.0f } },
        /* PIXI 5 spellings, which some filters use instead. */
        { "inputSize",   4, { w, h, 1.0f / w, 1.0f / h } },
        { "inputPixel",  4, { w, h, 1.0f / w, 1.0f / h } },
        { "outputFrame", 4, { 0.0f, 0.0f, w, h } },
    };

    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
        const GlslUniform *u = glsl_translation_find(s_current.layout, builtins[i].name);
        if (!u) continue;
        size_t count = (size_t)builtins[i].count;
        size_t max_floats = u->size / sizeof(float);
        if (count > max_floats) count = max_floats;
        if (u->offset + count * sizeof(float) > sizeof(s_current.uniforms)) continue;
        memcpy(s_current.uniforms + u->offset, builtins[i].v, count * sizeof(float));
    }
}

void filter_begin(uint32_t program, uint32_t input_texture, int width, int height)
{
    memset(&s_current, 0, sizeof(s_current));
    s_current.shader = (int)program;
    s_current.textures[0] = input_texture;
    s_current.texture_count = 1;
    s_current.layout = (const GlslTranslation *)gpu_runtime_shader_layout(program);
    /* Set before the filter's own uniforms, so anything it names itself wins. */
    supply_pixi_uniforms(width, height);
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
    uint32_t offset;
    if (s_current.layout) {
        const GlslUniform *u = glsl_translation_find(s_current.layout, name);
        if (!u) return;
        /* A shader declaring vec4 but handed a single float (or the reverse)
           would scribble past the member; keep to whichever is smaller. */
        size_t max_floats = u->size / sizeof(float);
        if (count > max_floats) count = max_floats;
        if (u->integer) {
            /* The value came from JS as a float; an int or bool member needs
               the number, not its bit pattern. */
            if (u->offset + count * sizeof(int32_t) > sizeof(s_current.uniforms)) return;
            int32_t *dst = (int32_t *)(s_current.uniforms + u->offset);
            for (size_t i = 0; i < count; i++) dst[i] = (int32_t)values[i];
            return;
        }
        offset = u->offset;
    } else {
        const UniformSlot *slot = find_slot(s_current.shader, name);
        if (!slot) return;
        offset = slot->offset;
    }
    if (offset + count * sizeof(float) > sizeof(s_current.uniforms)) return;
    memcpy(s_current.uniforms + offset, values, count * sizeof(float));
}

void filter_set_uniform_texture(uint32_t program, const char *name,
                                uint32_t texture, int unit)
{
    (void)program; (void)unit;
    if (!name) return;

    int index = -1;
    if (s_current.layout) {
        index = glsl_translation_sampler_index(s_current.layout, name);
    } else if (strcmp(name, "u_mask") == 0) {
        /* The mask shader is the only built-in with a second sampler. */
        index = 1;
    }
    if (index <= 0 || index >= GPU_MAX_FILTER_TEXTURES) return;

    s_current.textures[index] = texture;
    if (index + 1 > s_current.texture_count) s_current.texture_count = index + 1;
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
    if (s_current.shader <= 0) return;

    uint32_t size;
    if (s_current.layout) {
        size = s_current.layout->uniform_size;
        if (size > sizeof(s_current.uniforms)) return;
    } else if (s_current.shader < GPU_SHADER_COUNT) {
        size = SHADER_INFO[s_current.shader].size;
    } else {
        return;
    }

    gpu_record_filter(s_current.shader, s_current.textures, s_current.texture_count,
                      size ? s_current.uniforms : NULL, size);
}

void filter_end(void)
{
    memset(&s_current, 0, sizeof(s_current));
}

const char *filter_default_vert_src(void)      { return DEFAULT_VERT_SRC; }
const char *filter_color_matrix_frag_src(void) { return COLOR_MATRIX_FRAG_SRC; }
const char *filter_blur_frag_src(void)         { return BLUR_FRAG_SRC; }
const char *filter_alpha_frag_src(void)        { return ALPHA_FRAG_SRC; }
const char *filter_color_filter_frag_src(void) { return COLOR_FILTER_FRAG_SRC; }
const char *filter_mask_frag_src(void)         { return MASK_FRAG_SRC; }
