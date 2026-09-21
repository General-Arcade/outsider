/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

/*
 * Tests for the Canvas2D rendering context: C-level API, __native_canvas2d
 * bindings, and the CanvasRenderingContext2D shim.
 */

#include "rendering/canvas2d.h"
#include "rendering/image_loader.h"
#include "engine/js_engine.h"
#include "bindings/bind_canvas2d.h"
#include "bindings/bind_image.h"
#include "bindings/bind_io.h"
#include "io/file_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Tiny test framework */

static int _tests_run = 0;
static int _tests_passed = 0;
static int _tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    _tests_run++; \
    printf("  [%d] %s ... ", _tests_run, #name); \
    name(); \
    _tests_passed++; \
    printf("PASS\n"); \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        _tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_EQ_INT(a, b) do { \
    int _a = (a), _b = (b); \
    if (_a != _b) { \
        printf("FAIL at %s:%d: %s == %d, expected %d\n", __FILE__, __LINE__, #a, _a, _b); \
        _tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_EQ_UINT(a, b) do { \
    unsigned int _a = (a), _b = (b); \
    if (_a != _b) { \
        printf("FAIL at %s:%d: %s == %u, expected %u\n", __FILE__, __LINE__, #a, _a, _b); \
        _tests_failed++; \
        return; \
    } \
} while (0)

/* C-level Canvas2D tests */

TEST(test_create_destroy)
{
    canvas2d_init();
    Canvas2DHandle h = canvas2d_create(100, 50);
    ASSERT(h != CANVAS2D_HANDLE_INVALID);
    ASSERT(canvas2d_context_count() == 1);

    int w, h2;
    ASSERT(canvas2d_get_size(h, &w, &h2));
    ASSERT_EQ_INT(w, 100);
    ASSERT_EQ_INT(h2, 50);

    canvas2d_destroy(h);
    ASSERT(canvas2d_context_count() == 0);
    canvas2d_shutdown();
}

TEST(test_create_invalid)
{
    canvas2d_init();
    ASSERT(canvas2d_create(0, 10) == CANVAS2D_HANDLE_INVALID);
    ASSERT(canvas2d_create(10, 0) == CANVAS2D_HANDLE_INVALID);
    ASSERT(canvas2d_create(-1, 10) == CANVAS2D_HANDLE_INVALID);
    ASSERT(canvas2d_context_count() == 0);
    canvas2d_shutdown();
}

TEST(test_pixels_initialized_to_zero)
{
    canvas2d_init();
    Canvas2DHandle h = canvas2d_create(4, 4);
    ASSERT(h != CANVAS2D_HANDLE_INVALID);

    uint8_t *pixels = canvas2d_get_pixels(h);
    ASSERT(pixels != NULL);

    /* All pixels should be transparent black (0,0,0,0). */
    for (int i = 0; i < 4 * 4 * 4; i++) {
        ASSERT_EQ_UINT(pixels[i], 0);
    }

    canvas2d_shutdown();
}

TEST(test_fill_rect)
{
    canvas2d_init();
    Canvas2DHandle h = canvas2d_create(4, 4);

    canvas2d_set_fill_color(h, 255, 0, 0, 255);
    canvas2d_fill_rect(h, 1, 1, 2, 2);

    uint8_t *pixels = canvas2d_get_pixels(h);

    /* (0,0) transparent */
    ASSERT_EQ_UINT(pixels[0], 0);
    ASSERT_EQ_UINT(pixels[3], 0);

    /* (1,1) red */
    int idx = (1 * 4 + 1) * 4;
    ASSERT_EQ_UINT(pixels[idx + 0], 255);
    ASSERT_EQ_UINT(pixels[idx + 1], 0);
    ASSERT_EQ_UINT(pixels[idx + 2], 0);
    ASSERT_EQ_UINT(pixels[idx + 3], 255);

    /* (2,2) red */
    idx = (2 * 4 + 2) * 4;
    ASSERT_EQ_UINT(pixels[idx + 0], 255);
    ASSERT_EQ_UINT(pixels[idx + 3], 255);

    /* (3,3) transparent */
    idx = (3 * 4 + 3) * 4;
    ASSERT_EQ_UINT(pixels[idx + 3], 0);

    canvas2d_shutdown();
}

TEST(test_clear_rect)
{
    canvas2d_init();
    Canvas2DHandle h = canvas2d_create(4, 4);

    canvas2d_set_fill_color(h, 0, 0, 255, 255);
    canvas2d_fill_rect(h, 0, 0, 4, 4);

    canvas2d_clear_rect(h, 1, 1, 2, 2);

    uint8_t *pixels = canvas2d_get_pixels(h);

    /* (0,0) still blue */
    ASSERT_EQ_UINT(pixels[2], 255);
    ASSERT_EQ_UINT(pixels[3], 255);

    /* (1,1) cleared */
    int idx = (1 * 4 + 1) * 4;
    ASSERT_EQ_UINT(pixels[idx + 0], 0);
    ASSERT_EQ_UINT(pixels[idx + 1], 0);
    ASSERT_EQ_UINT(pixels[idx + 2], 0);
    ASSERT_EQ_UINT(pixels[idx + 3], 0);

    canvas2d_shutdown();
}

TEST(test_draw_image)
{
    canvas2d_init();
    Canvas2DHandle h = canvas2d_create(4, 4);

    /* 2x2 source: red, green, blue, white. */
    uint8_t src[2 * 2 * 4] = {
        255,   0,   0, 255,
          0, 255,   0, 255,
          0,   0, 255, 255,
        255, 255, 255, 255,
    };

    /* Draw the full 2x2 image at (1,1). */
    canvas2d_draw_image(h, src, 2, 2,
                         0, 0, 2, 2,
                         1, 1, 2, 2);

    uint8_t *pixels = canvas2d_get_pixels(h);

    /* (1,1) red */
    int idx = (1 * 4 + 1) * 4;
    ASSERT_EQ_UINT(pixels[idx + 0], 255);
    ASSERT_EQ_UINT(pixels[idx + 1], 0);
    ASSERT_EQ_UINT(pixels[idx + 2], 0);

    /* (2,1) green */
    idx = (1 * 4 + 2) * 4;
    ASSERT_EQ_UINT(pixels[idx + 0], 0);
    ASSERT_EQ_UINT(pixels[idx + 1], 255);

    /* (1,2) blue */
    idx = (2 * 4 + 1) * 4;
    ASSERT_EQ_UINT(pixels[idx + 2], 255);

    /* (2,2) white */
    idx = (2 * 4 + 2) * 4;
    ASSERT_EQ_UINT(pixels[idx + 0], 255);
    ASSERT_EQ_UINT(pixels[idx + 1], 255);
    ASSERT_EQ_UINT(pixels[idx + 2], 255);

    canvas2d_shutdown();
}

TEST(test_draw_image_scaled)
{
    canvas2d_init();
    Canvas2DHandle h = canvas2d_create(4, 4);

    uint8_t src[4] = { 255, 0, 0, 255 };

    /* Scale 1x1 source to 2x2 destination. */
    canvas2d_draw_image(h, src, 1, 1,
                         0, 0, 1, 1,
                         0, 0, 2, 2);

    uint8_t *pixels = canvas2d_get_pixels(h);

    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 2; x++) {
            int idx = (y * 4 + x) * 4;
            ASSERT_EQ_UINT(pixels[idx + 0], 255);
            ASSERT_EQ_UINT(pixels[idx + 1], 0);
            ASSERT_EQ_UINT(pixels[idx + 2], 0);
            ASSERT_EQ_UINT(pixels[idx + 3], 255);
        }
    }

    canvas2d_shutdown();
}

TEST(test_get_put_image_data)
{
    canvas2d_init();
    Canvas2DHandle h = canvas2d_create(4, 4);

    canvas2d_set_fill_color(h, 0, 255, 0, 255);
    canvas2d_fill_rect(h, 0, 0, 4, 4);

    uint8_t buf[2 * 2 * 4];
    canvas2d_get_image_data(h, 1, 1, 2, 2, buf);

    ASSERT_EQ_UINT(buf[0], 0);
    ASSERT_EQ_UINT(buf[1], 255);
    ASSERT_EQ_UINT(buf[2], 0);
    ASSERT_EQ_UINT(buf[3], 255);

    /* Rewrite the buffer as red and put it back at (0,0). */
    for (int i = 0; i < 2 * 2; i++) {
        buf[i * 4 + 0] = 255;
        buf[i * 4 + 1] = 0;
        buf[i * 4 + 2] = 0;
        buf[i * 4 + 3] = 255;
    }

    canvas2d_put_image_data(h, buf, 0, 0, 2, 2);

    uint8_t *pixels = canvas2d_get_pixels(h);

    /* (0,0) red */
    ASSERT_EQ_UINT(pixels[0], 255);
    ASSERT_EQ_UINT(pixels[1], 0);
    ASSERT_EQ_UINT(pixels[2], 0);
    ASSERT_EQ_UINT(pixels[3], 255);

    /* (2,0) still green */
    int idx = (0 * 4 + 2) * 4;
    ASSERT_EQ_UINT(pixels[idx + 0], 0);
    ASSERT_EQ_UINT(pixels[idx + 1], 255);

    canvas2d_shutdown();
}

TEST(test_save_restore)
{
    canvas2d_init();
    Canvas2DHandle h = canvas2d_create(4, 4);

    canvas2d_set_fill_color(h, 255, 0, 0, 255);
    canvas2d_save(h);

    canvas2d_set_fill_color(h, 0, 0, 255, 255);
    canvas2d_fill_rect(h, 0, 0, 2, 2);

    /* Restore brings back the red fill. */
    canvas2d_restore(h);
    canvas2d_fill_rect(h, 2, 0, 2, 2);

    uint8_t *pixels = canvas2d_get_pixels(h);

    /* (0,0) blue */
    ASSERT_EQ_UINT(pixels[2], 255);

    /* (2,0) red */
    int idx = (0 * 4 + 2) * 4;
    ASSERT_EQ_UINT(pixels[idx + 0], 255);
    ASSERT_EQ_UINT(pixels[idx + 2], 0);

    canvas2d_shutdown();
}

TEST(test_global_alpha)
{
    canvas2d_init();
    Canvas2DHandle h = canvas2d_create(4, 4);

    canvas2d_set_fill_color(h, 255, 255, 255, 255);
    canvas2d_set_global_alpha(h, 0.5f);
    canvas2d_fill_rect(h, 0, 0, 4, 4);

    uint8_t *pixels = canvas2d_get_pixels(h);

    /* Alpha should be approximately 0.5 * 255. */
    ASSERT(pixels[3] >= 126 && pixels[3] <= 130);

    canvas2d_shutdown();
}

TEST(test_resize)
{
    canvas2d_init();
    Canvas2DHandle h = canvas2d_create(4, 4);

    canvas2d_set_fill_color(h, 255, 0, 0, 255);
    canvas2d_fill_rect(h, 0, 0, 4, 4);

    /* Resize clears the pixels. */
    canvas2d_resize(h, 8, 8);

    int w, h2;
    ASSERT(canvas2d_get_size(h, &w, &h2));
    ASSERT_EQ_INT(w, 8);
    ASSERT_EQ_INT(h2, 8);

    uint8_t *pixels = canvas2d_get_pixels(h);
    ASSERT_EQ_UINT(pixels[0], 0);
    ASSERT_EQ_UINT(pixels[3], 0);

    canvas2d_shutdown();
}

TEST(test_composite_copy)
{
    canvas2d_init();
    Canvas2DHandle h = canvas2d_create(4, 4);

    canvas2d_set_fill_color(h, 255, 0, 0, 255);
    canvas2d_fill_rect(h, 0, 0, 4, 4);

    canvas2d_set_composite_op(h, CANVAS2D_COMP_COPY);
    canvas2d_set_fill_color(h, 0, 0, 255, 128);
    canvas2d_fill_rect(h, 0, 0, 2, 2);

    uint8_t *pixels = canvas2d_get_pixels(h);

    /* Copy mode replaces the pixel; no blending with the red underneath. */
    ASSERT_EQ_UINT(pixels[0], 0);
    ASSERT_EQ_UINT(pixels[1], 0);
    ASSERT_EQ_UINT(pixels[2], 255);
    ASSERT_EQ_UINT(pixels[3], 128);

    canvas2d_shutdown();
}

TEST(test_stroke_rect)
{
    canvas2d_init();
    Canvas2DHandle h = canvas2d_create(10, 10);

    canvas2d_set_stroke_color(h, 255, 0, 0, 255);
    canvas2d_set_line_width(h, 1.0f);
    canvas2d_stroke_rect(h, 2, 2, 6, 6);

    uint8_t *pixels = canvas2d_get_pixels(h);

    /* Top edge at (2,2) is red. */
    int idx = (2 * 10 + 2) * 4;
    ASSERT_EQ_UINT(pixels[idx + 0], 255);
    ASSERT_EQ_UINT(pixels[idx + 3], 255);

    /* Interior at (4,4) is untouched. */
    idx = (4 * 10 + 4) * 4;
    ASSERT_EQ_UINT(pixels[idx + 3], 0);

    canvas2d_shutdown();
}

TEST(test_font_load_and_measure)
{
    canvas2d_init();
    Canvas2DHandle h = canvas2d_create(200, 50);

    FILE *f = fopen("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", "rb");
    if (!f) {
        printf("SKIP (no font file) ");
        canvas2d_shutdown();
        return;
    }
    fseek(f, 0, SEEK_END);
    size_t sz = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(sz);
    fread(data, 1, sz, f);
    fclose(f);

    ASSERT(canvas2d_load_font("DejaVuSansMono", data, sz));
    free(data);

    canvas2d_set_font(h, "DejaVuSansMono", 20.0f);

    float w = canvas2d_measure_text(h, "Hello");
    ASSERT(w > 10.0f);

    /* Longer text is wider. */
    float w2 = canvas2d_measure_text(h, "Hello, World!");
    ASSERT(w2 > w);

    /* Empty text measures zero. */
    float w3 = canvas2d_measure_text(h, "");
    ASSERT(w3 == 0.0f || w3 < 0.01f);

    canvas2d_shutdown();
}

TEST(test_fill_text_renders_pixels)
{
    canvas2d_init();
    Canvas2DHandle h = canvas2d_create(200, 50);

    FILE *f = fopen("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", "rb");
    if (!f) {
        printf("SKIP (no font file) ");
        canvas2d_shutdown();
        return;
    }
    fseek(f, 0, SEEK_END);
    size_t sz = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(sz);
    fread(data, 1, sz, f);
    fclose(f);

    ASSERT(canvas2d_load_font("TestFont", data, sz));
    free(data);

    canvas2d_set_font(h, "TestFont", 20.0f);
    canvas2d_set_fill_color(h, 255, 255, 255, 255);
    canvas2d_fill_text(h, "Hello", 10.0f, 30.0f);

    /* Some pixels must have been rendered. */
    uint8_t *pixels = canvas2d_get_pixels(h);
    int non_zero = 0;
    for (int i = 0; i < 200 * 50 * 4; i += 4) {
        if (pixels[i + 3] > 0) non_zero++;
    }
    ASSERT(non_zero > 10);

    canvas2d_shutdown();
}

TEST(test_measure_text_no_font)
{
    canvas2d_init();
    Canvas2DHandle h = canvas2d_create(100, 50);

    /* Without a loaded font, measureText falls back to an estimate. */
    canvas2d_set_font(h, "NonExistent", 20.0f);
    float w = canvas2d_measure_text(h, "Hello");
    ASSERT(w > 0.0f);

    canvas2d_shutdown();
}

TEST(test_multiple_contexts)
{
    canvas2d_init();
    Canvas2DHandle h1 = canvas2d_create(10, 10);
    Canvas2DHandle h2 = canvas2d_create(20, 20);
    ASSERT(h1 != h2);
    ASSERT(canvas2d_context_count() == 2);

    canvas2d_set_fill_color(h1, 255, 0, 0, 255);
    canvas2d_fill_rect(h1, 0, 0, 10, 10);
    canvas2d_set_fill_color(h2, 0, 0, 255, 255);
    canvas2d_fill_rect(h2, 0, 0, 20, 20);

    uint8_t *p1 = canvas2d_get_pixels(h1);
    ASSERT_EQ_UINT(p1[0], 255);
    ASSERT_EQ_UINT(p1[2], 0);

    uint8_t *p2 = canvas2d_get_pixels(h2);
    ASSERT_EQ_UINT(p2[0], 0);
    ASSERT_EQ_UINT(p2[2], 255);

    canvas2d_shutdown();
}

/* JS binding tests */

static JSEngine *_js = NULL;

static void js_setup(void)
{
    _js = js_engine_init();
    canvas2d_init();
    image_loader_init(false);
    bind_io_register(js_engine_get_context(_js));
    bind_image_register(js_engine_get_context(_js));
    bind_canvas2d_register(js_engine_get_context(_js));
}

static void js_teardown(void)
{
    canvas2d_shutdown();
    image_loader_shutdown();
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

TEST(test_js_create_destroy)
{
    js_setup();
    ASSERT(js_eval_bool(
        "var h = __native_canvas2d.create(100, 50);"
        "var ok = h > 0;"
        "__native_canvas2d.destroy(h);"
        "ok;"
    ));
    js_teardown();
}

TEST(test_js_get_size)
{
    js_setup();
    ASSERT(js_eval_bool(
        "var h = __native_canvas2d.create(200, 150);"
        "var s = __native_canvas2d.getSize(h);"
        "var ok = s.width === 200 && s.height === 150;"
        "__native_canvas2d.destroy(h);"
        "ok;"
    ));
    js_teardown();
}

TEST(test_js_fill_rect_and_get_image_data)
{
    js_setup();
    ASSERT(js_eval_bool(
        "var h = __native_canvas2d.create(4, 4);"
        "__native_canvas2d.setFillColor(h, 255, 0, 0, 255);"
        "__native_canvas2d.fillRect(h, 1, 1, 2, 2);"
        "var ab = __native_canvas2d.getImageData(h, 1, 1, 1, 1);"
        "var view = new Uint8Array(ab);"
        "var ok = view[0] === 255 && view[1] === 0 && view[2] === 0 && view[3] === 255;"
        "__native_canvas2d.destroy(h);"
        "ok;"
    ));
    js_teardown();
}

TEST(test_js_clear_rect)
{
    js_setup();
    ASSERT(js_eval_bool(
        "var h = __native_canvas2d.create(4, 4);"
        "__native_canvas2d.setFillColor(h, 0, 255, 0, 255);"
        "__native_canvas2d.fillRect(h, 0, 0, 4, 4);"
        "__native_canvas2d.clearRect(h, 1, 1, 2, 2);"
        "var ab = __native_canvas2d.getImageData(h, 1, 1, 1, 1);"
        "var view = new Uint8Array(ab);"
        "var ok = view[0] === 0 && view[1] === 0 && view[2] === 0 && view[3] === 0;"
        "__native_canvas2d.destroy(h);"
        "ok;"
    ));
    js_teardown();
}

TEST(test_js_put_image_data)
{
    js_setup();
    ASSERT(js_eval_bool(
        "var h = __native_canvas2d.create(4, 4);"
        "var ab = new ArrayBuffer(4);"
        "var view = new Uint8Array(ab);"
        "view[0] = 128; view[1] = 64; view[2] = 32; view[3] = 255;"
        "__native_canvas2d.putImageData(h, ab, 2, 2, 1, 1);"
        "var out = __native_canvas2d.getImageData(h, 2, 2, 1, 1);"
        "var check = new Uint8Array(out);"
        "var ok = check[0] === 128 && check[1] === 64 && check[2] === 32 && check[3] === 255;"
        "__native_canvas2d.destroy(h);"
        "ok;"
    ));
    js_teardown();
}

TEST(test_js_save_restore)
{
    js_setup();
    ASSERT(js_eval_bool(
        "var h = __native_canvas2d.create(4, 4);"
        "__native_canvas2d.setFillColor(h, 255, 0, 0, 255);"
        "__native_canvas2d.save(h);"
        "__native_canvas2d.setFillColor(h, 0, 0, 255, 255);"
        "__native_canvas2d.fillRect(h, 0, 0, 2, 2);"
        "__native_canvas2d.restore(h);"
        "__native_canvas2d.fillRect(h, 2, 0, 2, 2);"
        "var ab1 = __native_canvas2d.getImageData(h, 0, 0, 1, 1);"
        "var v1 = new Uint8Array(ab1);"
        "var ab2 = __native_canvas2d.getImageData(h, 2, 0, 1, 1);"
        "var v2 = new Uint8Array(ab2);"
        "var ok = v1[0] === 0 && v1[2] === 255 && v2[0] === 255 && v2[2] === 0;"
        "__native_canvas2d.destroy(h);"
        "ok;"
    ));
    js_teardown();
}

TEST(test_js_draw_image)
{
    js_setup();
    ASSERT(js_eval_bool(
        "var h = __native_canvas2d.create(4, 4);"
        "var src = new ArrayBuffer(4);"
        "var sv = new Uint8Array(src);"
        "sv[0] = 255; sv[1] = 0; sv[2] = 0; sv[3] = 255;"
        "__native_canvas2d.drawImage(h, src, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1);"
        "var ab = __native_canvas2d.getImageData(h, 0, 0, 1, 1);"
        "var v = new Uint8Array(ab);"
        "var ok = v[0] === 255 && v[1] === 0 && v[2] === 0 && v[3] === 255;"
        "__native_canvas2d.destroy(h);"
        "ok;"
    ));
    js_teardown();
}

TEST(test_js_resize)
{
    js_setup();
    ASSERT(js_eval_bool(
        "var h = __native_canvas2d.create(4, 4);"
        "__native_canvas2d.resize(h, 8, 6);"
        "var s = __native_canvas2d.getSize(h);"
        "var ok = s.width === 8 && s.height === 6;"
        "__native_canvas2d.destroy(h);"
        "ok;"
    ));
    js_teardown();
}

TEST(test_js_measure_text_no_font)
{
    js_setup();
    ASSERT(js_eval_bool(
        "var h = __native_canvas2d.create(100, 50);"
        "var w = __native_canvas2d.measureText(h, 'Hello');"
        "var ok = w > 0;"
        "__native_canvas2d.destroy(h);"
        "ok;"
    ));
    js_teardown();
}

/* JS shim-level tests (Canvas2D via dom_shim + canvas2d_shim) */

static void js_setup_with_shims(void)
{
    _js = js_engine_init();
    canvas2d_init();
    image_loader_init(false);
    bind_io_register(js_engine_get_context(_js));
    bind_image_register(js_engine_get_context(_js));
    bind_canvas2d_register(js_engine_get_context(_js));

    js_engine_eval_file(_js, "src/shims/dom_shim.js");
    js_engine_eval_file(_js, "src/shims/canvas2d_shim.js");
    js_engine_eval_file(_js, "src/shims/navigator_shim.js");
}

TEST(test_shim_canvas_get_context)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var canvas = document.createElement('canvas');"
        "canvas.width = 100;"
        "canvas.height = 50;"
        "var ctx = canvas.getContext('2d');"
        "ctx !== null && ctx._handle > 0;"
    ));
    js_teardown();
}

TEST(test_shim_fill_style_and_rect)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var canvas = document.createElement('canvas');"
        "canvas.width = 4;"
        "canvas.height = 4;"
        "var ctx = canvas.getContext('2d');"
        "ctx.fillStyle = '#ff0000';"
        "ctx.fillRect(0, 0, 2, 2);"
        "var imgData = ctx.getImageData(0, 0, 1, 1);"
        "imgData.data[0] === 255 && imgData.data[1] === 0 && imgData.data[2] === 0 && imgData.data[3] === 255;"
    ));
    js_teardown();
}

TEST(test_shim_clear_rect)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var canvas = document.createElement('canvas');"
        "canvas.width = 4;"
        "canvas.height = 4;"
        "var ctx = canvas.getContext('2d');"
        "ctx.fillStyle = 'rgb(0, 255, 0)';"
        "ctx.fillRect(0, 0, 4, 4);"
        "ctx.clearRect(1, 1, 2, 2);"
        "var imgData = ctx.getImageData(1, 1, 1, 1);"
        "imgData.data[0] === 0 && imgData.data[1] === 0 && imgData.data[2] === 0 && imgData.data[3] === 0;"
    ));
    js_teardown();
}

TEST(test_shim_put_image_data)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var canvas = document.createElement('canvas');"
        "canvas.width = 4;"
        "canvas.height = 4;"
        "var ctx = canvas.getContext('2d');"
        "var id = ctx.createImageData(1, 1);"
        "id.data[0] = 128; id.data[1] = 64; id.data[2] = 32; id.data[3] = 255;"
        "ctx.putImageData(id, 2, 2);"
        "var read = ctx.getImageData(2, 2, 1, 1);"
        "read.data[0] === 128 && read.data[1] === 64 && read.data[2] === 32 && read.data[3] === 255;"
    ));
    js_teardown();
}

TEST(test_shim_save_restore)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var canvas = document.createElement('canvas');"
        "canvas.width = 4;"
        "canvas.height = 4;"
        "var ctx = canvas.getContext('2d');"
        "ctx.fillStyle = '#ff0000';"
        "ctx.save();"
        "ctx.fillStyle = '#0000ff';"
        "ctx.fillRect(0, 0, 2, 2);"
        "ctx.restore();"
        "ctx.fillRect(2, 0, 2, 2);"
        "var blue = ctx.getImageData(0, 0, 1, 1);"
        "var red = ctx.getImageData(2, 0, 1, 1);"
        "blue.data[0] === 0 && blue.data[2] === 255 && red.data[0] === 255 && red.data[2] === 0;"
    ));
    js_teardown();
}

TEST(test_shim_global_alpha)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var canvas = document.createElement('canvas');"
        "canvas.width = 4;"
        "canvas.height = 4;"
        "var ctx = canvas.getContext('2d');"
        "ctx.globalAlpha = 0.5;"
        "ctx.fillStyle = '#ffffff';"
        "ctx.fillRect(0, 0, 4, 4);"
        "var imgData = ctx.getImageData(0, 0, 1, 1);"
        "imgData.data[3] >= 126 && imgData.data[3] <= 130;"
    ));
    js_teardown();
}

TEST(test_shim_measure_text)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var canvas = document.createElement('canvas');"
        "canvas.width = 200;"
        "canvas.height = 50;"
        "var ctx = canvas.getContext('2d');"
        "ctx.font = '20px sans-serif';"
        "var m = ctx.measureText('Hello');"
        "m.width > 0;"
    ));
    js_teardown();
}

TEST(test_shim_color_parsing)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var c1 = __parseColor('#ff0000');"
        "c1[0] === 255 && c1[1] === 0 && c1[2] === 0 && c1[3] === 255;"
    ));
    ASSERT(js_eval_bool(
        "var c2 = __parseColor('rgb(0, 128, 255)');"
        "c2[0] === 0 && c2[1] === 128 && c2[2] === 255 && c2[3] === 255;"
    ));
    ASSERT(js_eval_bool(
        "var c3 = __parseColor('rgba(255, 0, 0, 0.5)');"
        "c3[0] === 255 && c3[1] === 0 && c3[2] === 0 && c3[3] === 128;"
    ));
    ASSERT(js_eval_bool(
        "var c4 = __parseColor('#f00');"
        "c4[0] === 255 && c4[1] === 0 && c4[2] === 0 && c4[3] === 255;"
    ));
    ASSERT(js_eval_bool(
        "var c5 = __parseColor('white');"
        "c5[0] === 255 && c5[1] === 255 && c5[2] === 255 && c5[3] === 255;"
    ));
    js_teardown();
}

TEST(test_shim_font_parsing)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var f1 = __parseFont('28px GameFont');"
        "f1.size === 28 && f1.family === 'GameFont';"
    ));
    ASSERT(js_eval_bool(
        "var f2 = __parseFont('bold 16px sans-serif');"
        "f2.size === 16 && f2.family === 'sans-serif';"
    ));
    ASSERT(js_eval_bool(
        "var f3 = __parseFont('italic bold 12.5px \"Times New Roman\"');"
        "f3.size === 12.5 && f3.family === 'Times New Roman';"
    ));
    js_teardown();
}

TEST(test_shim_canvas_resize)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var canvas = document.createElement('canvas');"
        "canvas.width = 4;"
        "canvas.height = 4;"
        "var ctx = canvas.getContext('2d');"
        "ctx.fillStyle = '#ff0000';"
        "ctx.fillRect(0, 0, 4, 4);"
        /* Setting width/height resizes the native backing store. */
        "canvas.width = 8;"
        "canvas.height = 8;"
        "var s = __native_canvas2d.getSize(ctx._handle);"
        "s.width === 8 && s.height === 8;"
    ));
    js_teardown();
}

TEST(test_shim_draw_canvas_to_canvas)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var src = document.createElement('canvas');"
        "src.width = 2;"
        "src.height = 2;"
        "var srcCtx = src.getContext('2d');"
        "srcCtx.fillStyle = '#ff0000';"
        "srcCtx.fillRect(0, 0, 2, 2);"
        "var dst = document.createElement('canvas');"
        "dst.width = 4;"
        "dst.height = 4;"
        "var dstCtx = dst.getContext('2d');"
        "dstCtx.drawImage(src, 1, 1);"
        "var imgData = dstCtx.getImageData(1, 1, 1, 1);"
        "imgData.data[0] === 255 && imgData.data[1] === 0 && imgData.data[2] === 0 && imgData.data[3] === 255;"
    ));
    js_teardown();
}

/* Main */

/* HTML overlay (dom_overlay.js): layout and input for plugin dialogs */

static void js_setup_with_overlay(void)
{
    js_setup_with_shims();
    js_engine_eval_file(_js, "src/shims/dom_overlay.js");
    /* A dialog like InputDialog_Custom.js builds: form > label, input, buttons. */
    js_engine_eval(_js,
        "function makeDialog() {"
        "  var form = document.createElement('form'); form.id = 'inputForm';"
        "  var label = document.createElement('div'); label.innerText = 'Your name';"
        "  var input = document.createElement('input'); input.id = 'textInput'; input.type = 'text'; input.value = 'Ann'; input.maxLength = 5;"
        "  var row = document.createElement('div');"
        "  var ok = document.createElement('button'); ok.innerText = 'OK'; ok.type = 'button';"
        "  var cancel = document.createElement('button'); cancel.innerText = 'CANCEL'; cancel.type = 'button';"
        "  form.style.position = 'absolute'; form.style.width = '330px'; form.style.padding = '20px';"
        "  form.style.border = '3px solid #f1d2bd'; form.style.boxSizing = 'border-box';"
        "  form.style.display = 'flex'; form.style.flexDirection = 'column'; form.style.alignItems = 'center';"
        "  form.style.left = '50%'; form.style.top = '50%'; form.style.transform = 'translate(-50%, -50%)';"
        "  label.style.fontSize = '24px'; label.style.marginBottom = '10px';"
        "  input.style.width = '100%'; input.style.height = '50px'; input.style.border = '3px solid #fff'; input.style.padding = '5px'; input.style.boxSizing = 'border-box';"
        "  row.style.display = 'flex'; row.style.justifyContent = 'space-around'; row.style.width = '100%'; row.style.margin = '15px 0 0 0';"
        "  ok.style.width = '100px'; ok.style.height = '50px'; cancel.style.width = '100px'; cancel.style.height = '50px'; cancel.style.marginLeft = '10px';"
        "  row.appendChild(ok); row.appendChild(cancel);"
        "  form.appendChild(label); form.appendChild(input); form.appendChild(row);"
        "  return form;"
        "}", "<test>");
}

TEST(test_overlay_tracks_body_children)
{
    js_setup_with_overlay();
    ASSERT(js_eval_bool(
        "var form = makeDialog();"
        "var before = __dom_overlay.roots.length;"
        "document.body.appendChild(form);"
        "var during = __dom_overlay.roots.length;"
        "document.body.removeChild(form);"
        "before === 0 && during === 1 && __dom_overlay.roots.length === 0 &&"
        "form.elements.length === 3 && form.elements['textInput'].value === 'Ann' && form.elements[1].innerText === 'OK';"
    ));
    /* Canvas and video elements are not overlay widgets. */
    ASSERT(js_eval_bool(
        "var c = document.createElement('canvas'); document.body.appendChild(c);"
        "var n = __dom_overlay.roots.length; document.body.removeChild(c); n === 0;"
    ));
    js_teardown();
}

TEST(test_overlay_layout_boxes)
{
    js_setup_with_overlay();
    ASSERT(js_eval_bool(
        "var form = makeDialog(); document.body.appendChild(form);"
        "var n = __dom_overlay.layout(form, 816, 624);"
        "var label = n.children[0], input = n.children[1], row = n.children[2];"
        "var ok = row.children[0], cancel = row.children[1];"
        /* border-box form: 330 wide, content 330 - 2*20 - 2*3 = 284 */
        "n.w === 330 && n.contentW === 284 &&"
        /* centred on screen: translate(-50%,-50%) from 50%/50% */
        "n.screenX === Math.round(408 - 165) && n.screenY === Math.round(312 - n.h / 2) &&"
        /* input fills the content width, 50px tall; row too */
        "input.w === 284 && input.h === 50 && row.w === 284 &&"
        /* buttons keep their own size and sit inside the row */
        "ok.w === 100 && ok.h === 50 && cancel.w === 100 && row.h === 50 &&"
        "ok.x >= row.bd.width && cancel.x > ok.x + ok.w + 10 && cancel.x + cancel.w <= row.w &&"
        /* column: label, then input, then row (margins applied) */
        "label.y < input.y && input.y + input.h + 15 <= row.y + 0.01 &&"
        /* label is shrink-to-fit and centred */
        "label.w < 284 && Math.abs((label.x + label.w / 2) - (n.bd.width + n.pad.l + 142)) < 1;"
    ));
    /* transform scale multiplies every length. */
    ASSERT(js_eval_bool(
        "form.style.transform = 'translate(-50%, -50%) scale(2)';"
        "var m = __dom_overlay.layout(form, 1920, 1080);"
        "m.w === 660 && m.children[1].h === 100 && m.screenX === Math.round(960 - 330);"
    ));
    ASSERT(js_eval_bool(
        "form.style.transform = ''; form.style.left = '10px'; form.style.top = '20px';"
        "var k = __dom_overlay.layout(form, 816, 624);"
        "k.screenX === 10 && k.screenY === 20;"
    ));
    js_teardown();
}

TEST(test_overlay_style_rules_and_hidden)
{
    js_setup_with_overlay();
    ASSERT(js_eval_bool(
        "var style = document.createElement('style');"
        "style.textContent = 'form, input, button { font-family: \\'customFont\\'; } #textInput { font-size: 30px }';"
        "document.head.appendChild(style);"
        "var form = makeDialog(); document.body.appendChild(form);"
        "var n = __dom_overlay.layout(form, 816, 624);"
        "var ok = n.children[2].children[0], input = n.children[1];"
        "ok.font.family === \"'customFont'\" && input.font.size === 30 && n.children[0].font.size === 24;"
    ));
    ASSERT(js_eval_bool(
        "form.children[0].style.display = 'none';"
        "var m = __dom_overlay.layout(form, 816, 624);"
        "m.children.length === 2 && m.children[0].el.tagName === 'INPUT';"
    ));
    js_teardown();
}

TEST(test_overlay_input_events)
{
    js_setup_with_overlay();
    /* Focus, typing through text events, plugin key handlers with
       preventDefault, Enter submitting the form, clicks on buttons. */
    ASSERT(js_eval_bool(
        "var form = makeDialog(); document.body.appendChild(form);"
        "var input = form.elements['textInput'], ok = form.elements[1];"
        "var log = [];"
        "document.addEventListener('keydown', function(e) { log.push('doc:' + e.key); });"
        "form.addEventListener('submit', function(e) { log.push('submit'); e.preventDefault(); });"
        "input.addEventListener('keydown', function(e) { if (e.key === 'Backspace') { e.preventDefault(); log.push('bs'); } });"
        "ok.onclick = function() { log.push('ok'); };"
        "var consumedBeforeFocus = __dom_overlay.handleRawEvent({ type: 'keydown', key: 'a', code: 'KeyA', keyCode: 65 });"
        "input.focus();"
        "var r1 = __dom_overlay.handleRawEvent({ type: 'textinput', text: 'ie' });"
        "var r2 = __dom_overlay.handleRawEvent({ type: 'keydown', key: 'Backspace', code: 'Backspace', keyCode: 8 });"
        "var r3 = __dom_overlay.handleRawEvent({ type: 'keydown', key: 'ArrowLeft', code: 'ArrowLeft', keyCode: 37 });"
        "__dom_overlay.handleRawEvent({ type: 'textinput', text: 'X' });"
        "__dom_overlay.handleRawEvent({ type: 'textinput', text: 'toolong' });"
        "var r4 = __dom_overlay.handleRawEvent({ type: 'keydown', key: 'Enter', code: 'Enter', keyCode: 13 });"
        "consumedBeforeFocus === false && document.activeElement === input &&"
        "r1 === true && r2 === true && r3 === true && r4 === true &&"
        /* 'Ann' + 'ie' = 'Annie'; the plugin's Backspace handler prevented the
           default delete; ArrowLeft moved the caret; maxLength 5 blocked the
           rest. */
        "input.value === 'Annie' && input.selectionStart === 4;"
    ));
    ASSERT(js_eval_bool(
        "log.indexOf('bs') >= 0 && log.indexOf('submit') >= 0 && log.indexOf('doc:Backspace') >= 0 && log.indexOf('doc:Enter') >= 0;"
    ));
    /* Mouse: press and release on the OK button clicks it; elsewhere passes through. */
    ASSERT(js_eval_bool(
        "typeof __dom_overlay.render === 'function' && __dom_overlay.roots.length === 1;"
    ));
    ASSERT(js_eval_bool(
        "var n = __dom_overlay.layout(form, 816, 624);"
        "var row = n.children[2], okBox = row.children[0];"
        "var cx = n.screenX + row.x + okBox.x + okBox.w / 2, cy = n.screenY + row.y + okBox.y + okBox.h / 2;"
        "__dom_overlay.updateLayouts(816, 624);"  /* refreshes the hit-test boxes */
        "var d = __dom_overlay.handleRawEvent({ type: 'mousedown', clientX: cx, clientY: cy, button: 0 });"
        "var u = __dom_overlay.handleRawEvent({ type: 'mouseup', clientX: cx, clientY: cy, button: 0 });"
        "var outside = __dom_overlay.handleRawEvent({ type: 'mousedown', clientX: 1, clientY: 1, button: 0 });"
        "d === true && u === true && outside === false && log.indexOf('ok') >= 0 && document.activeElement === ok;"
    ));
    js_teardown();
}

int main(void)
{
    printf("Canvas2D tests:\n");
    printf("--- C-level tests ---\n");

    RUN(test_create_destroy);
    RUN(test_create_invalid);
    RUN(test_pixels_initialized_to_zero);
    RUN(test_fill_rect);
    RUN(test_clear_rect);
    RUN(test_draw_image);
    RUN(test_draw_image_scaled);
    RUN(test_get_put_image_data);
    RUN(test_save_restore);
    RUN(test_global_alpha);
    RUN(test_resize);
    RUN(test_composite_copy);
    RUN(test_stroke_rect);
    RUN(test_font_load_and_measure);
    RUN(test_fill_text_renders_pixels);
    RUN(test_measure_text_no_font);
    RUN(test_multiple_contexts);

    printf("--- JS binding tests ---\n");
    RUN(test_js_create_destroy);
    RUN(test_js_get_size);
    RUN(test_js_fill_rect_and_get_image_data);
    RUN(test_js_clear_rect);
    RUN(test_js_put_image_data);
    RUN(test_js_save_restore);
    RUN(test_js_draw_image);
    RUN(test_js_resize);
    RUN(test_js_measure_text_no_font);

    printf("--- JS shim tests ---\n");
    RUN(test_shim_canvas_get_context);
    RUN(test_shim_fill_style_and_rect);
    RUN(test_shim_clear_rect);
    RUN(test_shim_put_image_data);
    RUN(test_shim_save_restore);
    RUN(test_shim_global_alpha);
    RUN(test_shim_measure_text);
    RUN(test_shim_color_parsing);
    RUN(test_shim_font_parsing);
    RUN(test_shim_canvas_resize);
    RUN(test_shim_draw_canvas_to_canvas);

    printf("--- HTML overlay tests ---\n");
    RUN(test_overlay_tracks_body_children);
    RUN(test_overlay_layout_boxes);
    RUN(test_overlay_style_rules_and_hidden);
    RUN(test_overlay_input_events);

    printf("\n%d/%d tests passed.\n", _tests_passed, _tests_run);
    return _tests_failed > 0 ? 1 : 0;
}
