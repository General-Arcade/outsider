/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#ifndef RMMZ_GLSL_TRANSLATE_H
#define RMMZ_GLSL_TRANSLATE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Rewrites the GLSL that RPG Maker plugins ship into something a Vulkan
 * shader compiler will accept.
 *
 * Plugin filters are written for WebGL 1: GLSL ES 1.00, with `varying`,
 * `texture2D`, `gl_FragColor`, and loose `uniform float x;` declarations at
 * file scope. Vulkan GLSL has none of those. In particular there is no default
 * uniform block, so every loose uniform has to be gathered into a real block --
 * which is also what lets the runtime keep setting them by name, since the
 * rewrite records where each one landed.
 *
 * References in the body are left alone and redirected with #define, so the
 * shader's own code is never rewritten -- only its declarations.
 */

#define GLSL_MAX_NAME     64
#define GLSL_MAX_UNIFORMS 32
#define GLSL_MAX_SAMPLERS 4

typedef struct {
    char     name[GLSL_MAX_NAME];
    /* The type exactly as the shader declared it. Kept verbatim because
       float, int and bool are indistinguishable by size alone, and emitting
       the wrong one turns `if (someBool)` into a type error. */
    char     type[16];
    uint32_t offset;       /* byte offset within the uniform block (std140) */
    uint32_t size;         /* bytes the value occupies */
    uint32_t components;   /* 1 = float, 2 = vec2, 4 = vec4, 16 = mat4 */
    /* int and bool members hold an integer, so a value arriving from JS as a
       float has to be converted rather than copied bit for bit. */
    bool     integer;
} GlslUniform;

typedef struct {
    /* Translated GLSL 450 source, owned by this struct. */
    char       *source;

    GlslUniform uniforms[GLSL_MAX_UNIFORMS];
    int         uniform_count;
    /* Size of the uniform block, rounded up to 16 bytes. Zero when the
       shader declares no non-sampler uniforms. */
    uint32_t    uniform_size;

    /* Sampler uniforms in declaration order, which is also binding order. */
    char        samplers[GLSL_MAX_SAMPLERS][GLSL_MAX_NAME];
    int         sampler_count;
} GlslTranslation;

/* Translate a fragment shader. Returns false when the source uses something
   the rewrite cannot express, leaving *out untouched. */
bool glsl_translate_fragment(const char *source, GlslTranslation *out);

/* Release the translated source. */
void glsl_translation_free(GlslTranslation *t);

/* Look up a uniform by the name the shader used. Returns NULL if absent. */
const GlslUniform *glsl_translation_find(const GlslTranslation *t, const char *name);

/* Index of a sampler by name, or -1. */
int glsl_translation_sampler_index(const GlslTranslation *t, const char *name);

#endif /* RMMZ_GLSL_TRANSLATE_H */
