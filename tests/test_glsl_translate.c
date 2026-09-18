/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

/* Tests for the GLSL ES 1.00 -> Vulkan GLSL rewrite that lets plugin filters
   reach a shader compiler. Shader sources here are the real thing, taken from
   filters the test games ship. */

#include "rendering/glsl_translate.h"

#include <stdio.h>
#include <string.h>

static int g_tests = 0;
static int g_failures = 0;
static const char *g_current = "";

#define TEST(name) do { g_current = #name; g_tests++; } while (0)
#define PASS() printf("  PASS  %s\n", g_current)
#define FAIL(msg) do { \
    printf("  FAIL  %s: %s\n", g_current, msg); \
    g_failures++; \
} while (0)

#define CHECK(cond, msg) do { \
    if (cond) { PASS(); } else { FAIL(msg); } \
} while (0)

/* pixi-filters ColorOverlayFilter, as Pocket Mirror ships it. */
static const char *COLOR_OVERLAY =
    "varying vec2 vTextureCoord;\n"
    "uniform sampler2D uSampler;\n"
    "uniform vec3 color;\n"
    "void main(void) {\n"
    "    vec4 currentColor = texture2D(uSampler, vTextureCoord);\n"
    "    vec3 colorOverlay = color * currentColor.a;\n"
    "    gl_FragColor = vec4(colorOverlay.r, colorOverlay.g, colorOverlay.b, currentColor.a);\n"
    "}\n";

/* DRAPLINE's BlendColorFilter, which mixes scalars and a vec4. */
static const char *BLEND_COLOR =
    "precision mediump float;\n"
    "varying vec2 vTextureCoord;\n"
    "uniform sampler2D uSampler;\n"
    "uniform vec4 blendColor;\n"
    "uniform float brightness;\n"
    "void main() {\n"
    "    vec4 c = texture2D(uSampler, vTextureCoord);\n"
    "    gl_FragColor = c * brightness + blendColor;\n"
    "}\n";

static void test_translate_basic(void)
{
    GlslTranslation t;

    TEST(translates_a_real_plugin_shader);
    CHECK(glsl_translate_fragment(COLOR_OVERLAY, &t), "translation failed");

    TEST(emits_a_desktop_version_directive);
    CHECK(t.source && strncmp(t.source, "#version 450", 12) == 0, "no #version 450");

    TEST(varying_becomes_an_input);
    CHECK(strstr(t.source, "layout(location = 0) in vec2 vTextureCoord;") != NULL,
          "varying not rewritten");

    TEST(sampler_gets_the_fragment_set);
    CHECK(strstr(t.source, "layout(set = 2, binding = 0) uniform sampler2D uSampler;") != NULL,
          "sampler not bound to set 2");

    TEST(loose_uniform_moves_into_a_block);
    CHECK(strstr(t.source, "uniform _RmmzUniforms") != NULL &&
          strstr(t.source, "vec3 color;") != NULL,
          "uniform block missing");

    TEST(uniform_block_is_anonymous_so_names_stay_global);
    /* The block has no instance name, so the shader's own statement still
       reads `color` and needs no rewriting -- and a local of the same name
       may still shadow it, as GLSL ES allowed. */
    CHECK(strstr(t.source, "};") != NULL &&
          strstr(t.source, "} _rmmz;") == NULL &&
          strstr(t.source, "vec3 colorOverlay = color * currentColor.a;") != NULL,
          "block should be anonymous and the body untouched");

    TEST(frag_color_gets_an_output);
    CHECK(strstr(t.source, "out vec4 _rmmzFragColor;") != NULL &&
          strstr(t.source, "#define gl_FragColor _rmmzFragColor") != NULL,
          "gl_FragColor not redirected");

    TEST(texture2D_is_aliased);
    CHECK(strstr(t.source, "#define texture2D texture") != NULL, "texture2D not aliased");

    TEST(sampler_is_recorded);
    CHECK(t.sampler_count == 1 && glsl_translation_sampler_index(&t, "uSampler") == 0,
          "sampler not recorded");

    TEST(uniform_offset_is_recorded);
    {
        const GlslUniform *u = glsl_translation_find(&t, "color");
        CHECK(u && u->offset == 0 && u->components == 3, "uniform lookup wrong");
    }

    TEST(block_size_is_rounded_to_16);
    CHECK(t.uniform_size == 16, "vec3 block should round up to 16 bytes");

    glsl_translation_free(&t);
}

static void test_translate_layout(void)
{
    GlslTranslation t;

    TEST(translates_mixed_uniform_types);
    CHECK(glsl_translate_fragment(BLEND_COLOR, &t), "translation failed");

    TEST(precision_statement_is_dropped);
    CHECK(strstr(t.source, "precision mediump") == NULL, "precision statement kept");

    TEST(std140_alignment_is_applied);
    {
        /* vec4 aligns to 16 and occupies 0..15; the float follows at 16. */
        const GlslUniform *bc = glsl_translation_find(&t, "blendColor");
        const GlslUniform *br = glsl_translation_find(&t, "brightness");
        char msg[128];
        if (bc && br && bc->offset == 0 && br->offset == 16) {
            PASS();
        } else {
            snprintf(msg, sizeof(msg), "blendColor@%u brightness@%u",
                     bc ? bc->offset : 9999u, br ? br->offset : 9999u);
            FAIL(msg);
        }
    }

    TEST(block_size_covers_every_member);
    CHECK(t.uniform_size == 32, "expected 32-byte block");

    glsl_translation_free(&t);
}

static void test_translate_edges(void)
{
    GlslTranslation t;

    TEST(comments_do_not_hide_declarations);
    {
        const char *src =
            "// uniform float decoy;\n"
            "/* uniform float decoy2; */\n"
            "varying vec2 vTextureCoord;\n"
            "uniform sampler2D uSampler;\n"
            "uniform float real;\n"
            "void main() { gl_FragColor = texture2D(uSampler, vTextureCoord) * real; }\n";
        if (glsl_translate_fragment(src, &t)) {
            CHECK(t.uniform_count == 1 && glsl_translation_find(&t, "real") != NULL,
                  "commented-out uniforms were counted");
            glsl_translation_free(&t);
        } else {
            FAIL("translation failed");
        }
    }

    TEST(declarations_inside_functions_are_left_alone);
    {
        const char *src =
            "varying vec2 vTextureCoord;\n"
            "uniform sampler2D uSampler;\n"
            "void main() {\n"
            "    float uniform_like = 1.0;\n"
            "    gl_FragColor = texture2D(uSampler, vTextureCoord) * uniform_like;\n"
            "}\n";
        if (glsl_translate_fragment(src, &t)) {
            CHECK(t.uniform_count == 0 &&
                  strstr(t.source, "float uniform_like = 1.0;") != NULL,
                  "local variable was treated as a uniform");
            glsl_translation_free(&t);
        } else {
            FAIL("translation failed");
        }
    }

    TEST(multiple_samplers_get_successive_bindings);
    {
        const char *src =
            "varying vec2 vTextureCoord;\n"
            "uniform sampler2D uSampler;\n"
            "uniform sampler2D bloomTexture;\n"
            "void main() { gl_FragColor = texture2D(uSampler, vTextureCoord)"
            " + texture2D(bloomTexture, vTextureCoord); }\n";
        if (glsl_translate_fragment(src, &t)) {
            CHECK(t.sampler_count == 2 &&
                  glsl_translation_sampler_index(&t, "bloomTexture") == 1 &&
                  strstr(t.source, "binding = 1) uniform sampler2D bloomTexture;") != NULL,
                  "second sampler mis-bound");
            glsl_translation_free(&t);
        } else {
            FAIL("translation failed");
        }
    }

    TEST(bool_uniform_keeps_its_declared_type);
    {
        /* pixi-filters' GlowFilter does exactly this. Emitting the member as
           a float compiles the block fine and then fails on `if (knockout)`. */
        const char *src =
            "varying vec2 vTextureCoord;\n"
            "uniform sampler2D uSampler;\n"
            "uniform bool knockout;\n"
            "void main() {\n"
            "    if (knockout) { gl_FragColor = vec4(0.0); }\n"
            "    else { gl_FragColor = texture2D(uSampler, vTextureCoord); }\n"
            "}\n";
        if (glsl_translate_fragment(src, &t)) {
            const GlslUniform *u = glsl_translation_find(&t, "knockout");
            CHECK(u && strcmp(u->type, "bool") == 0 && u->integer &&
                  strstr(t.source, "bool knockout;") != NULL,
                  "bool was not preserved");
            glsl_translation_free(&t);
        } else {
            FAIL("translation failed");
        }
    }

    TEST(a_parameter_may_shadow_a_uniform);
    {
        /* pixi-filters' PixelateFilter declares `uniform vec2 size` and then
           `vec2 pixelate(vec2 coord, vec2 size)`. Aliasing the uniform with a
           #define would rewrite the parameter too, yielding `vec2 _u.size`. */
        const char *src =
            "varying vec2 vTextureCoord;\n"
            "uniform sampler2D uSampler;\n"
            "uniform vec2 size;\n"
            "vec2 pixelate(vec2 coord, vec2 size)\n"
            "{\n"
            "    return floor(coord / size) * size;\n"
            "}\n"
            "void main() {\n"
            "    gl_FragColor = texture2D(uSampler, pixelate(vTextureCoord, size));\n"
            "}\n";
        if (glsl_translate_fragment(src, &t)) {
            CHECK(strstr(t.source, "vec2 pixelate(vec2 coord, vec2 size)") != NULL &&
                  strstr(t.source, "_rmmz.size") == NULL,
                  "the shadowing parameter was rewritten");
            glsl_translation_free(&t);
        } else {
            FAIL("translation failed");
        }
    }

    TEST(identifiers_reserved_only_in_desktop_glsl_are_renamed);
    {
        /* `sample` is an ordinary name in GLSL ES 1.00 and a keyword in 450;
           ZoomBlurFilter declares `vec4 sample`. */
        const char *src =
            "varying vec2 vTextureCoord;\n"
            "uniform sampler2D uSampler;\n"
            "void main() {\n"
            "    vec4 sample = texture2D(uSampler, vTextureCoord);\n"
            "    gl_FragColor = sample;\n"
            "}\n";
        if (glsl_translate_fragment(src, &t)) {
            CHECK(strstr(t.source, "#define sample _rmmz_sample") != NULL,
                  "reserved identifier not renamed");
            glsl_translation_free(&t);
        } else {
            FAIL("translation failed");
        }
    }

    TEST(sampler2D_is_not_mistaken_for_sample);
    {
        /* The rename must match whole words only. */
        const char *src =
            "varying vec2 vTextureCoord;\n"
            "uniform sampler2D uSampler;\n"
            "void main() { gl_FragColor = texture2D(uSampler, vTextureCoord); }\n";
        if (glsl_translate_fragment(src, &t)) {
            CHECK(strstr(t.source, "#define sample") == NULL,
                  "sampler2D triggered the sample rename");
            glsl_translation_free(&t);
        } else {
            FAIL("translation failed");
        }
    }

    TEST(unknown_type_is_refused_rather_than_mislaid);
    {
        const char *src =
            "varying vec2 vTextureCoord;\n"
            "uniform mat2x3 weird;\n"
            "void main() { gl_FragColor = vec4(0.0); }\n";
        CHECK(!glsl_translate_fragment(src, &t), "unknown type was accepted");
    }
}

int main(void)
{
    printf("GLSL translation tests\n");
    test_translate_basic();
    test_translate_layout();
    test_translate_edges();
    printf("\n%d tests, %d failures\n", g_tests, g_failures);
    return g_failures == 0 ? 0 : 1;
}
