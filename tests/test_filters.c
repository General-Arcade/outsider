/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

/* test_filters.c — Tests for the PIXI filter system: C-level shader sources,
   the __native_filters binding, and the PIXI.Filter shim classes. */

#include "rendering/filters.h"
#include "bindings/bind_filters.h"
#include "bindings/bind_renderer.h"
#include "bindings/bind_io.h"
#include "engine/js_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Test framework macros */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)

#define RUN(name) do { \
    int _prev_failed = tests_failed; \
    tests_run++; \
    printf("  [%d] %s ... ", tests_run, #name); \
    name(); \
    if (tests_failed > _prev_failed) { \
        /* Already printed FAIL inside ASSERT */ \
    } else { \
        tests_passed++; \
        printf("PASS\n"); \
    } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ_INT(a, b) do { \
    int _a = (a), _b = (b); \
    if (_a != _b) { \
        printf("FAIL at %s:%d: %s == %d, expected %d\n", __FILE__, __LINE__, #a, _a, _b); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* C-level tests */

TEST(test_c_init_shutdown)
{
    filters_init();
    filters_shutdown();
    /* Double init/shutdown should be safe. */
    filters_init();
    filters_init();
    filters_shutdown();
    filters_shutdown();
}

TEST(test_c_compile_null_frag)
{
    filters_init();
    uint32_t id = filter_compile_shader(NULL, NULL);
    ASSERT(id == 0);
    filters_shutdown();
}

TEST(test_c_shader_source_not_null)
{
    const char *vert = filter_default_vert_src();
    ASSERT(vert != NULL);
    ASSERT(strlen(vert) > 10);

    const char *cm = filter_color_matrix_frag_src();
    ASSERT(cm != NULL);
    ASSERT(strlen(cm) > 10);

    const char *blur = filter_blur_frag_src();
    ASSERT(blur != NULL);
    ASSERT(strlen(blur) > 10);

    const char *alpha = filter_alpha_frag_src();
    ASSERT(alpha != NULL);
    ASSERT(strlen(alpha) > 10);
}

TEST(test_c_shader_sources_contain_keywords)
{
    ASSERT(strstr(filter_default_vert_src(), "gl_Position") != NULL);
    ASSERT(strstr(filter_default_vert_src(), "v_texcoord") != NULL);

    ASSERT(strstr(filter_color_matrix_frag_src(), "u_colorMatrix") != NULL);
    ASSERT(strstr(filter_color_matrix_frag_src(), "u_colorOffset") != NULL);

    ASSERT(strstr(filter_blur_frag_src(), "u_direction") != NULL);
    ASSERT(strstr(filter_blur_frag_src(), "u_strength") != NULL);

    ASSERT(strstr(filter_alpha_frag_src(), "u_alpha") != NULL);
}

TEST(test_c_uniform_setters_no_crash)
{
    /* Calling uniform setters without GL should be safe no-ops. */
    filters_init();
    filter_set_uniform_1f(0, "test", 1.0f);
    filter_set_uniform_2f(0, "test", 1.0f, 2.0f);
    filter_set_uniform_4f(0, "test", 1.0f, 2.0f, 3.0f, 4.0f);
    float mat[16] = {0};
    filter_set_uniform_mat4(0, "test", mat);
    filter_set_uniform_mat4(0, "test", NULL);
    filter_begin(0, 0, 100, 100);
    filter_draw_quad();
    filter_end();
    filter_delete_shader(0);
    filters_shutdown();
}

/* JS engine helpers */

static JSEngine *_js = NULL;

static void js_setup(void)
{
    _js = js_engine_init();
    bind_io_register(js_engine_get_context(_js));
    bind_filters_register(js_engine_get_context(_js));
    bind_renderer_register(js_engine_get_context(_js));
}

static void js_teardown(void)
{
    js_engine_shutdown(_js);
    _js = NULL;
}

static int js_eval_bool(const char *code)
{
    char *result = js_engine_eval_string(_js, code, "<test>");
    if (!result) return 0;
    int ok = (strcmp(result, "true") == 0);
    if (!ok) printf("(got: %s) ", result);
    js_engine_free_string(result);
    return ok;
}

/* JS binding tests */

TEST(test_js_native_filters_exists)
{
    js_setup();
    ASSERT(js_eval_bool("typeof __native_filters === 'object'"));
    ASSERT(js_eval_bool("typeof __native_filters.init === 'function'"));
    ASSERT(js_eval_bool("typeof __native_filters.shutdown === 'function'"));
    ASSERT(js_eval_bool("typeof __native_filters.compileShader === 'function'"));
    ASSERT(js_eval_bool("typeof __native_filters.deleteShader === 'function'"));
    ASSERT(js_eval_bool("typeof __native_filters.beginFilter === 'function'"));
    ASSERT(js_eval_bool("typeof __native_filters.drawQuad === 'function'"));
    ASSERT(js_eval_bool("typeof __native_filters.endFilter === 'function'"));
    js_teardown();
}

TEST(test_js_native_filters_init)
{
    js_setup();
    /* Without a GL context, init() fails and shader IDs stay at 0. */
    ASSERT(js_eval_bool("__native_filters.init() === false"));
    ASSERT(js_eval_bool("__native_filters.colorMatrixShader === 0"));
    ASSERT(js_eval_bool("__native_filters.blurShader === 0"));
    ASSERT(js_eval_bool("__native_filters.alphaShader === 0"));
    ASSERT(js_eval_bool("__native_filters.shutdown(); true"));
    js_teardown();
}

TEST(test_js_compile_shader_no_gl)
{
    js_setup();
    ASSERT(js_eval_bool("__native_filters.init(); true"));
    /* Without GL, compileShader returns 0. */
    ASSERT(js_eval_bool("__native_filters.compileShader(null, 'void main(){}') === 0"));
    ASSERT(js_eval_bool("__native_filters.shutdown(); true"));
    js_teardown();
}

TEST(test_js_uniform_setters_no_crash)
{
    js_setup();
    ASSERT(js_eval_bool("__native_filters.init(); true"));
    ASSERT(js_eval_bool(
        "__native_filters.setUniform1f(0, 'test', 1.0);"
        "__native_filters.setUniform2f(0, 'test', 1.0, 2.0);"
        "__native_filters.setUniform4f(0, 'test', 1.0, 2.0, 3.0, 4.0);"
        "true"
    ));
    ASSERT(js_eval_bool("__native_filters.shutdown(); true"));
    js_teardown();
}

TEST(test_js_rebind_batch_exists)
{
    js_setup();
    ASSERT(js_eval_bool("typeof __native_renderer.rebindBatch === 'function'"));
    /* Should not crash even without renderer init. */
    ASSERT(js_eval_bool("__native_renderer.rebindBatch(); true"));
    js_teardown();
}

/* JS shim tests (PIXI filter classes) */

static void js_setup_with_shims(void)
{
    _js = js_engine_init();
    bind_io_register(js_engine_get_context(_js));
    bind_renderer_register(js_engine_get_context(_js));
    bind_filters_register(js_engine_get_context(_js));

    js_engine_eval_file(_js, "src/shims/dom_shim.js");
    js_engine_execute_pending_jobs(_js);
    js_engine_eval_file(_js, "src/shims/pixi_shim.js");
    js_engine_execute_pending_jobs(_js);
}

TEST(test_shim_filter_class_exists)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool("typeof PIXI.Filter === 'function'"));
    ASSERT(js_eval_bool("typeof PIXI.filters === 'object'"));
    ASSERT(js_eval_bool("typeof PIXI.filters.ColorMatrixFilter === 'function'"));
    ASSERT(js_eval_bool("typeof PIXI.filters.BlurFilter === 'function'"));
    ASSERT(js_eval_bool("typeof PIXI.filters.AlphaFilter === 'function'"));
    js_teardown();
}

TEST(test_shim_filter_base)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var f = new PIXI.Filter();"
        "f.enabled === true && f.padding === 0 && f._glShader === 0"
    ));
    ASSERT(js_eval_bool(
        "var f = new PIXI.Filter('vert', 'frag', {x: 1});"
        "f.vertexSrc === 'vert' && f.fragmentSrc === 'frag' && f.uniforms.x === 1"
    ));
    js_teardown();
}

TEST(test_shim_color_matrix_identity)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var f = new PIXI.filters.ColorMatrixFilter();"
        "f.matrix.length === 20 && "
        "f.matrix[0] === 1 && f.matrix[6] === 1 && f.matrix[12] === 1 && f.matrix[18] === 1"
    ));
    js_teardown();
}

TEST(test_shim_color_matrix_brightness)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var f = new PIXI.filters.ColorMatrixFilter();"
        "f.brightness(0.5);"
        "f.matrix[0] === 0.5 && f.matrix[6] === 0.5 && f.matrix[12] === 0.5 && f.matrix[18] === 1"
    ));
    js_teardown();
}

TEST(test_shim_color_matrix_reset)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var f = new PIXI.filters.ColorMatrixFilter();"
        "f.brightness(0.5);"
        "f.reset();"
        "f.matrix[0] === 1 && f.matrix[6] === 1 && f.matrix[12] === 1"
    ));
    js_teardown();
}

TEST(test_shim_color_matrix_saturate)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var f = new PIXI.filters.ColorMatrixFilter();"
        "f.saturate(0);"
        "/* At saturate(0) the matrix should be identity-ish */"
        "Math.abs(f.matrix[0] - 1) < 0.01 && Math.abs(f.matrix[6] - 1) < 0.01"
    ));
    js_teardown();
}

TEST(test_shim_color_matrix_hue)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var f = new PIXI.filters.ColorMatrixFilter();"
        "f.hue(180);"
        "/* After 180 degree hue rotation, diagonal should change */"
        "f.matrix[0] !== 1 || f.matrix[6] !== 1 || f.matrix[12] !== 1"
    ));
    js_teardown();
}

TEST(test_shim_color_matrix_contrast)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var f = new PIXI.filters.ColorMatrixFilter();"
        "f.contrast(1);"
        "/* contrast(1) should give diagonal of 2 */"
        "Math.abs(f.matrix[0] - 2) < 0.01 && Math.abs(f.matrix[6] - 2) < 0.01"
    ));
    js_teardown();
}

TEST(test_shim_color_matrix_negative)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var f = new PIXI.filters.ColorMatrixFilter();"
        "f.negative();"
        "f.matrix[0] === -1 && f.matrix[6] === -1 && f.matrix[12] === -1"
    ));
    js_teardown();
}

TEST(test_shim_color_matrix_sepia)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var f = new PIXI.filters.ColorMatrixFilter();"
        "f.sepia();"
        "Math.abs(f.matrix[0] - 0.393) < 0.001"
    ));
    js_teardown();
}

TEST(test_shim_color_matrix_greyscale)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var f = new PIXI.filters.ColorMatrixFilter();"
        "f.greyscale(1);"
        "/* First three rows should be the same luminance weights */"
        "Math.abs(f.matrix[0] - f.matrix[5]) < 0.001 && "
        "Math.abs(f.matrix[0] - f.matrix[10]) < 0.001"
    ));
    js_teardown();
}

TEST(test_shim_color_matrix_tint)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var f = new PIXI.filters.ColorMatrixFilter();"
        "f.tint(0xFF0000);"
        "/* Red channel should be 1, green and blue 0 */"
        "Math.abs(f.matrix[0] - 1) < 0.01 && "
        "Math.abs(f.matrix[6]) < 0.01 && "
        "Math.abs(f.matrix[12]) < 0.01"
    ));
    js_teardown();
}

TEST(test_shim_color_matrix_multiply)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var f = new PIXI.filters.ColorMatrixFilter();"
        "f.brightness(0.5);"
        "f.brightness(0.5, true);"
        "/* Multiplying brightness 0.5 twice gives 0.25 */"
        "Math.abs(f.matrix[0] - 0.25) < 0.01"
    ));
    js_teardown();
}

TEST(test_shim_blur_filter)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var f = new PIXI.filters.BlurFilter();"
        "f.blur === 8 && f.quality === 4 && f.enabled === true"
    ));
    ASSERT(js_eval_bool(
        "var f = new PIXI.filters.BlurFilter(4, 2);"
        "f.blur === 4 && f.quality === 2"
    ));
    ASSERT(js_eval_bool(
        "var f = new PIXI.filters.BlurFilter();"
        "f.blur = 16;"
        "f.blur === 16 && f.blurX === 16 && f.blurY === 16"
    ));
    js_teardown();
}

TEST(test_shim_alpha_filter)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var f = new PIXI.filters.AlphaFilter();"
        "f.alpha === 1.0 && f.enabled === true"
    ));
    ASSERT(js_eval_bool(
        "var f = new PIXI.filters.AlphaFilter(0.5);"
        "f.alpha === 0.5"
    ));
    js_teardown();
}

TEST(test_shim_filter_on_display_object)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var sprite = new PIXI.Sprite();"
        "sprite.filters === null"
    ));
    ASSERT(js_eval_bool(
        "var sprite = new PIXI.Sprite();"
        "var f = new PIXI.filters.ColorMatrixFilter();"
        "sprite.filters = [f];"
        "sprite.filters.length === 1 && sprite.filters[0] === f"
    ));
    js_teardown();
}

TEST(test_shim_filter_on_container)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var container = new PIXI.Container();"
        "var f1 = new PIXI.filters.ColorMatrixFilter();"
        "var f2 = new PIXI.filters.AlphaFilter(0.5);"
        "container.filters = [f1, f2];"
        "container.filters.length === 2 && container.filters[1].alpha === 0.5"
    ));
    js_teardown();
}

TEST(test_shim_color_matrix_apply_method_exists)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var f = new PIXI.filters.ColorMatrixFilter();"
        "typeof f.apply === 'function'"
    ));
    /* apply() should not crash when called without GL. */
    ASSERT(js_eval_bool(
        "__native_filters.init();"
        "var f = new PIXI.filters.ColorMatrixFilter();"
        "f.apply(null, 0, 100, 100);"
        "__native_filters.shutdown();"
        "true"
    ));
    js_teardown();
}

TEST(test_shim_filter_destroy)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var f = new PIXI.Filter('v', 'f');"
        "f.destroy();"
        "f.uniforms === null && f._glShader === 0"
    ));
    js_teardown();
}

TEST(test_shim_render_filtered_exists)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "typeof PIXI.DisplayObject.prototype._renderFiltered === 'function'"
    ));
    js_teardown();
}

TEST(test_shim_color_matrix_extract_uniforms)
{
    js_setup_with_shims();
    /* Mirrors the mat4 + offset extraction done in apply(). */
    ASSERT(js_eval_bool(
        "var f = new PIXI.filters.ColorMatrixFilter();"
        "f.brightness(2);"
        "var m = f.matrix;"
        "/* mat4 column-major: col0=[m0,m5,m10,m15], col1=[m1,m6,m11,m16], etc */"
        "var mat4 = new Float32Array(["
        "    m[0], m[5], m[10], m[15],"
        "    m[1], m[6], m[11], m[16],"
        "    m[2], m[7], m[12], m[17],"
        "    m[3], m[8], m[13], m[18]"
        "]);"
        "var offset = [m[4]/255, m[9]/255, m[14]/255, m[19]/255];"
        "/* Brightness 2: diagonal should be 2, offset 0 */"
        "mat4[0] === 2 && mat4[5] === 2 && mat4[10] === 2 && mat4[15] === 1 &&"
        "offset[0] === 0 && offset[1] === 0 && offset[2] === 0 && offset[3] === 0"
    ));
    js_teardown();
}

/* main */

int main(void)
{
    printf("=== Filter Tests ===\n\n");

    printf("--- C-level tests ---\n");
    RUN(test_c_init_shutdown);
    RUN(test_c_compile_null_frag);
    RUN(test_c_shader_source_not_null);
    RUN(test_c_shader_sources_contain_keywords);
    RUN(test_c_uniform_setters_no_crash);

    printf("\n--- JS binding tests ---\n");
    RUN(test_js_native_filters_exists);
    RUN(test_js_native_filters_init);
    RUN(test_js_compile_shader_no_gl);
    RUN(test_js_uniform_setters_no_crash);
    RUN(test_js_rebind_batch_exists);

    printf("\n--- JS shim tests ---\n");
    RUN(test_shim_filter_class_exists);
    RUN(test_shim_filter_base);
    RUN(test_shim_color_matrix_identity);
    RUN(test_shim_color_matrix_brightness);
    RUN(test_shim_color_matrix_reset);
    RUN(test_shim_color_matrix_saturate);
    RUN(test_shim_color_matrix_hue);
    RUN(test_shim_color_matrix_contrast);
    RUN(test_shim_color_matrix_negative);
    RUN(test_shim_color_matrix_sepia);
    RUN(test_shim_color_matrix_greyscale);
    RUN(test_shim_color_matrix_tint);
    RUN(test_shim_color_matrix_multiply);
    RUN(test_shim_blur_filter);
    RUN(test_shim_alpha_filter);
    RUN(test_shim_filter_on_display_object);
    RUN(test_shim_filter_on_container);
    RUN(test_shim_color_matrix_apply_method_exists);
    RUN(test_shim_filter_destroy);
    RUN(test_shim_render_filtered_exists);
    RUN(test_shim_color_matrix_extract_uniforms);

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
