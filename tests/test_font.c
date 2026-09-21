/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

/* test_font.c — Tests for font loading and text rendering: font_manager,
   the __native_font binding, the FontFace/document.fonts shim, and
   Canvas2D text integration. */

#include "rendering/font_manager.h"
#include "rendering/canvas2d.h"
#include "engine/js_engine.h"
#include "bindings/bind_font.h"
#include "bindings/bind_canvas2d.h"
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

/* System font paths; tests that need a font file skip when it is missing. */

#ifdef _WIN32
#define FONT_PATH "C:/Windows/Fonts/arial.ttf"
#define FONT_MONO_PATH "C:/Windows/Fonts/consola.ttf"
#elif defined(__APPLE__)
#define FONT_PATH "/System/Library/Fonts/Supplemental/Arial.ttf"
#define FONT_MONO_PATH "/System/Library/Fonts/Supplemental/Courier New.ttf"
#else
#define FONT_PATH "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
#define FONT_MONO_PATH "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
#endif

/* The C-level tests skip themselves when load_font_data() comes back empty;
   the JS ones pass the path into the engine, so they check here instead. */
static int font_file_present(void)
{
    FILE *f = fopen(FONT_PATH, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

#define SKIP_WITHOUT_FONT() do { \
    if (!font_file_present()) { printf("SKIP (no font file) "); return; } \
} while (0)

static uint8_t *load_font_data(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)sz);
    if (!data) { fclose(f); return NULL; }
    fread(data, 1, (size_t)sz, f);
    fclose(f);
    *out_size = (size_t)sz;
    return data;
}

/* C-level font_manager tests */

TEST(test_fm_init_shutdown)
{
    font_manager_init();
    ASSERT_EQ_INT(font_manager_font_count(), 0);
    font_manager_shutdown();
}

TEST(test_fm_load_from_memory)
{
    font_manager_init();
    size_t sz = 0;
    uint8_t *data = load_font_data(FONT_PATH, &sz);
    if (!data) {
        printf("SKIP (no font file) ");
        font_manager_shutdown();
        return;
    }

    ASSERT(font_manager_load("TestFont", data, sz));
    ASSERT_EQ_INT(font_manager_font_count(), 1);
    ASSERT(font_manager_has_font("TestFont"));
    ASSERT(!font_manager_has_font("OtherFont"));

    free(data);
    font_manager_shutdown();
}

TEST(test_fm_load_from_file)
{
    font_manager_init();
    FILE *f = fopen(FONT_PATH, "r");
    if (!f) {
        printf("SKIP (no font file) ");
        font_manager_shutdown();
        return;
    }
    fclose(f);

    ASSERT(font_manager_load_file("FileFont", FONT_PATH));
    ASSERT(font_manager_has_font("FileFont"));
    ASSERT_EQ_INT(font_manager_font_count(), 1);

    font_manager_shutdown();
}

/* Big-endian helpers for building a WOFF container in the test. */
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}
static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

/* Wrap a raw sfnt (TTF) as a WOFF 1.0 file with every table stored
   uncompressed. Exercises directory parsing and sfnt reconstruction. */
static uint8_t *wrap_as_woff(const uint8_t *sfnt, size_t sfnt_size, size_t *out_size)
{
    uint16_t num_tables = get16(sfnt + 4);
    size_t dir_bytes = (size_t)num_tables * 20;
    size_t total = 44 + dir_bytes;
    for (uint16_t i = 0; i < num_tables; i++) {
        uint32_t len = get32(sfnt + 12 + i * 16 + 12);
        total += (len + 3u) & ~3u;
    }
    uint8_t *woff = calloc(total, 1);
    memcpy(woff, "wOFF", 4);
    put32(woff + 4, get32(sfnt));            /* flavor */
    put32(woff + 8, (uint32_t)total);
    woff[12] = (uint8_t)(num_tables >> 8); woff[13] = (uint8_t)num_tables;
    put32(woff + 16, (uint32_t)sfnt_size);   /* totalSfntSize */

    size_t data_off = 44 + dir_bytes;
    for (uint16_t i = 0; i < num_tables; i++) {
        const uint8_t *rec = sfnt + 12 + i * 16;
        uint32_t off = get32(rec + 8), len = get32(rec + 12);
        uint8_t *e = woff + 44 + i * 20;
        memcpy(e, rec, 4);                       /* tag */
        put32(e + 4, (uint32_t)data_off);
        put32(e + 8, len);                       /* compLength == origLength */
        put32(e + 12, len);
        put32(e + 16, get32(rec + 4));           /* checksum */
        memcpy(woff + data_off, sfnt + off, len);
        data_off += (len + 3u) & ~3u;
    }
    *out_size = total;
    return woff;
}

TEST(test_fm_load_woff_uncompressed)
{
    font_manager_init();
    size_t sz = 0;
    uint8_t *ttf = load_font_data(FONT_PATH, &sz);
    if (!ttf) {
        printf("SKIP (no font file) ");
        font_manager_shutdown();
        return;
    }
    size_t woff_size = 0;
    uint8_t *woff = wrap_as_woff(ttf, sz, &woff_size);

    /* The wrapper alone is not a font; the loader must unwrap it. */
    ASSERT(font_manager_load("WoffFont", woff, woff_size));
    ASSERT(font_manager_has_font("WoffFont"));
    float w_woff = font_manager_measure_text("WoffFont", 24.0f, "Hello");
    ASSERT(font_manager_load("TtfFont", ttf, sz));
    float w_ttf = font_manager_measure_text("TtfFont", 24.0f, "Hello");
    ASSERT(fabsf(w_woff - w_ttf) < 0.001f);

    free(woff);
    free(ttf);
    font_manager_shutdown();
}

TEST(test_fm_load_woff_compressed_fixture)
{
    /* RPG Maker MZ's default number font: zlib-compressed tables. */
    font_manager_init();
    const char *path = "tests/fixtures/mplus-2p-bold-sub.woff";
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("SKIP (fixture not found; run from project root) ");
        font_manager_shutdown();
        return;
    }
    fclose(f);

    ASSERT(font_manager_load_file("NumberFont", path));
    ASSERT(font_manager_has_font("NumberFont"));
    FontMetrics m;
    ASSERT(font_manager_get_metrics("NumberFont", 24.0f, &m));
    ASSERT(m.ascent > 0.0f);
    ASSERT(font_manager_measure_text("NumberFont", 24.0f, "0123456789") > 0.0f);

    /* Sizes are em sizes as in CSS. This font's ascent-to-descent span is
       1.4 em, so at 100px it must measure about 140px, not be squeezed to 100. */
    ASSERT(font_manager_get_metrics("NumberFont", 100.0f, &m));
    ASSERT(m.ascent - m.descent > 130.0f && m.ascent - m.descent < 150.0f);

    /* The same data must also load through the canvas2d font table. */
    size_t sz = 0;
    uint8_t *data = load_font_data(path, &sz);
    ASSERT(data != NULL);
    canvas2d_init();
    ASSERT(canvas2d_load_font("NumberFont", data, sz));
    canvas2d_shutdown();
    free(data);

    font_manager_shutdown();
}

TEST(test_fm_load_invalid)
{
    font_manager_init();
    ASSERT(!font_manager_load(NULL, NULL, 0));
    ASSERT(!font_manager_load("x", NULL, 0));

    uint8_t garbage[] = {1, 2, 3, 4};
    ASSERT(!font_manager_load("Bad", garbage, sizeof(garbage)));
    ASSERT_EQ_INT(font_manager_font_count(), 0);

    ASSERT(!font_manager_load_file("Missing", "/nonexistent/font.ttf"));

    font_manager_shutdown();
}

TEST(test_fm_replace_font)
{
    font_manager_init();
    size_t sz = 0;
    uint8_t *data = load_font_data(FONT_PATH, &sz);
    if (!data) {
        printf("SKIP (no font file) ");
        font_manager_shutdown();
        return;
    }

    ASSERT(font_manager_load("Replaceable", data, sz));
    ASSERT_EQ_INT(font_manager_font_count(), 1);

    /* Reloading under the same name replaces rather than adds. */
    ASSERT(font_manager_load("Replaceable", data, sz));
    ASSERT_EQ_INT(font_manager_font_count(), 1);

    free(data);
    font_manager_shutdown();
}

TEST(test_fm_get_metrics)
{
    font_manager_init();
    size_t sz = 0;
    uint8_t *data = load_font_data(FONT_PATH, &sz);
    if (!data) {
        printf("SKIP (no font file) ");
        font_manager_shutdown();
        return;
    }

    ASSERT(font_manager_load("MetricFont", data, sz));
    free(data);

    FontMetrics m;
    ASSERT(font_manager_get_metrics("MetricFont", 20.0f, &m));

    ASSERT(m.ascent > 0.0f);
    ASSERT(m.descent < 0.0f);
    /* line_height = ascent - descent + lineGap, so it exceeds the font size. */
    ASSERT(m.line_height > 15.0f);

    ASSERT(!font_manager_get_metrics("NoSuchFont", 20.0f, &m));

    font_manager_shutdown();
}

TEST(test_fm_measure_text)
{
    font_manager_init();
    size_t sz = 0;
    uint8_t *data = load_font_data(FONT_PATH, &sz);
    if (!data) {
        printf("SKIP (no font file) ");
        font_manager_shutdown();
        return;
    }

    ASSERT(font_manager_load("MeasureFont", data, sz));
    free(data);

    float w = font_manager_measure_text("MeasureFont", 20.0f, "Hello");
    ASSERT(w > 10.0f);

    float w2 = font_manager_measure_text("MeasureFont", 20.0f, "Hello World!");
    ASSERT(w2 > w);

    float w3 = font_manager_measure_text("MeasureFont", 20.0f, "");
    ASSERT(w3 < 0.01f);

    float w4 = font_manager_measure_text("MeasureFont", 40.0f, "Hello");
    ASSERT(w4 > w);

    font_manager_shutdown();
}

TEST(test_fm_measure_text_no_font)
{
    font_manager_init();

    /* With no font loaded, a fallback estimate is returned. */
    float w = font_manager_measure_text("NonExistent", 20.0f, "Hello");
    ASSERT(w > 0.0f);

    font_manager_shutdown();
}

TEST(test_fm_font_enumeration)
{
    font_manager_init();
    size_t sz = 0;
    uint8_t *data = load_font_data(FONT_PATH, &sz);
    if (!data) {
        printf("SKIP (no font file) ");
        font_manager_shutdown();
        return;
    }

    ASSERT(font_manager_load("Alpha", data, sz));
    ASSERT(font_manager_load("Beta", data, sz));
    free(data);

    ASSERT_EQ_INT(font_manager_font_count(), 2);
    ASSERT(font_manager_font_name(0) != NULL);
    ASSERT(font_manager_font_name(1) != NULL);
    ASSERT(strcmp(font_manager_font_name(0), "Alpha") == 0);
    ASSERT(strcmp(font_manager_font_name(1), "Beta") == 0);
    ASSERT(font_manager_font_name(2) == NULL);
    ASSERT(font_manager_font_name(-1) == NULL);

    font_manager_shutdown();
}

/* Canvas2D + font integration tests */

TEST(test_canvas2d_font_render)
{
    font_manager_init();
    canvas2d_init();

    size_t sz = 0;
    uint8_t *data = load_font_data(FONT_PATH, &sz);
    if (!data) {
        printf("SKIP (no font file) ");
        canvas2d_shutdown();
        font_manager_shutdown();
        return;
    }

    ASSERT(font_manager_load("RenderFont", data, sz));
    ASSERT(canvas2d_load_font("RenderFont", data, sz));
    free(data);

    Canvas2DHandle h = canvas2d_create(200, 50);
    ASSERT(h != CANVAS2D_HANDLE_INVALID);

    canvas2d_set_font(h, "RenderFont", 20.0f);
    canvas2d_set_fill_color(h, 255, 255, 255, 255);
    canvas2d_fill_text(h, "Test", 10.0f, 30.0f);

    uint8_t *pixels = canvas2d_get_pixels(h);
    int non_zero = 0;
    for (int i = 0; i < 200 * 50 * 4; i += 4) {
        if (pixels[i + 3] > 0) non_zero++;
    }
    ASSERT(non_zero > 5);

    canvas2d_shutdown();
    font_manager_shutdown();
}

TEST(test_canvas2d_measure_text)
{
    font_manager_init();
    canvas2d_init();

    size_t sz = 0;
    uint8_t *data = load_font_data(FONT_PATH, &sz);
    if (!data) {
        printf("SKIP (no font file) ");
        canvas2d_shutdown();
        font_manager_shutdown();
        return;
    }

    ASSERT(canvas2d_load_font("C2DFont", data, sz));
    free(data);

    Canvas2DHandle h = canvas2d_create(200, 50);
    canvas2d_set_font(h, "C2DFont", 24.0f);

    float w = canvas2d_measure_text(h, "Hello");
    ASSERT(w > 10.0f);

    /* font_manager and canvas2d should agree on width for the same font and size. */
    ASSERT(font_manager_load_file("C2DFont", FONT_PATH));
    float w2 = font_manager_measure_text("C2DFont", 24.0f, "Hello");
    float diff = fabsf(w - w2);
    ASSERT(diff < 5.0f);

    canvas2d_shutdown();
    font_manager_shutdown();
}

/* JS binding tests */

static JSEngine *_js = NULL;

static void js_setup(void)
{
    _js = js_engine_init();
    font_manager_init();
    canvas2d_init();
    bind_io_register(js_engine_get_context(_js));
    bind_font_register(js_engine_get_context(_js));
    bind_canvas2d_register(js_engine_get_context(_js));
}

static void js_teardown(void)
{
    canvas2d_shutdown();
    font_manager_shutdown();
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

TEST(test_js_font_count_empty)
{
    js_setup();
    ASSERT(js_eval_bool(
        "__native_font.fontCount() === 0;"
    ));
    js_teardown();
}

TEST(test_js_load_font_file)
{
    SKIP_WITHOUT_FONT();
    js_setup();
    ASSERT(js_eval_bool(
        "var ok = __native_font.loadFontFile('JsFont', '" FONT_PATH "');"
        "ok && __native_font.hasFont('JsFont') && __native_font.fontCount() === 1;"
    ));
    js_teardown();
}

TEST(test_js_load_nonexistent)
{
    js_setup();
    ASSERT(js_eval_bool(
        "var ok = __native_font.loadFontFile('Missing', '/nonexistent/font.ttf');"
        "!ok && !__native_font.hasFont('Missing');"
    ));
    js_teardown();
}

TEST(test_js_get_metrics)
{
    SKIP_WITHOUT_FONT();
    js_setup();
    ASSERT(js_eval_bool(
        "__native_font.loadFontFile('MetricJs', '" FONT_PATH "');"
        "var m = __native_font.getMetrics('MetricJs', 20);"
        "m !== null && m.ascent > 0 && m.descent < 0 && m.lineHeight > 15;"
    ));
    js_teardown();
}

TEST(test_js_get_metrics_missing_font)
{
    js_setup();
    ASSERT(js_eval_bool(
        "var m = __native_font.getMetrics('NoSuch', 20);"
        "m === null;"
    ));
    js_teardown();
}

TEST(test_js_measure_text)
{
    SKIP_WITHOUT_FONT();
    js_setup();
    ASSERT(js_eval_bool(
        "__native_font.loadFontFile('MeasureJs', '" FONT_PATH "');"
        "var w = __native_font.measureText('MeasureJs', 20, 'Hello');"
        "w > 10;"
    ));
    js_teardown();
}

TEST(test_js_font_names)
{
    SKIP_WITHOUT_FONT();
    js_setup();
    ASSERT(js_eval_bool(
        "__native_font.loadFontFile('Alpha', '" FONT_PATH "');"
        "__native_font.loadFontFile('Beta', '" FONT_PATH "');"
        "var names = __native_font.fontNames();"
        "names.length === 2 && names[0] === 'Alpha' && names[1] === 'Beta';"
    ));
    js_teardown();
}

/* JS shim tests (FontFace, FontFaceSet, document.fonts) */

static void js_setup_with_shims(void)
{
    _js = js_engine_init();
    font_manager_init();
    canvas2d_init();
    bind_io_register(js_engine_get_context(_js));
    bind_font_register(js_engine_get_context(_js));
    bind_canvas2d_register(js_engine_get_context(_js));

    js_engine_eval_file(_js, "src/shims/dom_shim.js");
    js_engine_eval_file(_js, "src/shims/canvas2d_shim.js");
    js_engine_eval_file(_js, "src/shims/font_shim.js");
}

TEST(test_shim_fontface_constructor)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "typeof FontFace === 'function';"
    ));
    js_teardown();
}

TEST(test_shim_fontface_load_url)
{
    SKIP_WITHOUT_FONT();
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var ff = new FontFace('ShimFont', 'url(\"" FONT_PATH "\")');"
        "ff.family === 'ShimFont' && ff.status === 'loaded';"
    ));
    js_teardown();
}

TEST(test_shim_fontface_load_direct_path)
{
    SKIP_WITHOUT_FONT();
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var ff = new FontFace('DirectFont', 'url(" FONT_PATH ")');"
        "ff.family === 'DirectFont' && ff.status === 'loaded';"
    ));
    js_teardown();
}

TEST(test_shim_fontface_load_failure)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var ff = new FontFace('BadFont', 'url(/nonexistent/font.ttf)');"
        "ff.status === 'error';"
    ));
    js_teardown();
}

TEST(test_shim_fontfaceset_add_check)
{
    SKIP_WITHOUT_FONT();
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var ff = new FontFace('CheckFont', 'url(\"" FONT_PATH "\")');"
        "document.fonts.add(ff);"
        "document.fonts.size === 1 && document.fonts.check('20px CheckFont');"
    ));
    js_teardown();
}

TEST(test_shim_fontfaceset_ready)
{
    SKIP_WITHOUT_FONT();
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var ff = new FontFace('ReadyFont', 'url(\"" FONT_PATH "\")');"
        "document.fonts.add(ff);"
        "typeof document.fonts.ready === 'object' && typeof document.fonts.ready.then === 'function';"
    ));
    js_teardown();
}

TEST(test_shim_fontfaceset_foreach)
{
    SKIP_WITHOUT_FONT();
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var ff1 = new FontFace('ForEachA', 'url(\"" FONT_PATH "\")');"
        "var ff2 = new FontFace('ForEachB', 'url(\"" FONT_PATH "\")');"
        "document.fonts.add(ff1);"
        "document.fonts.add(ff2);"
        "var count = 0;"
        "document.fonts.forEach(function() { count++; });"
        "count === 2;"
    ));
    js_teardown();
}

TEST(test_shim_fontfaceset_delete)
{
    SKIP_WITHOUT_FONT();
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var ff = new FontFace('DelFont', 'url(\"" FONT_PATH "\")');"
        "document.fonts.add(ff);"
        "var before = document.fonts.size;"
        "document.fonts.delete(ff);"
        "before === 1 && document.fonts.size === 0;"
    ));
    js_teardown();
}

TEST(test_shim_canvas2d_with_font)
{
    SKIP_WITHOUT_FONT();
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var ff = new FontFace('CanvasFont', 'url(\"" FONT_PATH "\")');"
        "document.fonts.add(ff);"
        "var canvas = document.createElement('canvas');"
        "canvas.width = 200; canvas.height = 50;"
        "var ctx = canvas.getContext('2d');"
        "ctx.font = '20px CanvasFont';"
        "var m = ctx.measureText('Hello');"
        "m.width > 10;"
    ));
    js_teardown();
}

/* Main */

int main(void)
{
    printf("Font tests:\n");
    printf("--- C-level font_manager tests ---\n");

    RUN(test_fm_init_shutdown);
    RUN(test_fm_load_from_memory);
    RUN(test_fm_load_from_file);
    RUN(test_fm_load_woff_uncompressed);
    RUN(test_fm_load_woff_compressed_fixture);
    RUN(test_fm_load_invalid);
    RUN(test_fm_replace_font);
    RUN(test_fm_get_metrics);
    RUN(test_fm_measure_text);
    RUN(test_fm_measure_text_no_font);
    RUN(test_fm_font_enumeration);

    printf("--- C-level Canvas2D + font integration tests ---\n");
    RUN(test_canvas2d_font_render);
    RUN(test_canvas2d_measure_text);

    printf("--- JS binding tests ---\n");
    RUN(test_js_font_count_empty);
    RUN(test_js_load_font_file);
    RUN(test_js_load_nonexistent);
    RUN(test_js_get_metrics);
    RUN(test_js_get_metrics_missing_font);
    RUN(test_js_measure_text);
    RUN(test_js_font_names);

    printf("--- JS shim tests (FontFace, FontFaceSet, document.fonts) ---\n");
    RUN(test_shim_fontface_constructor);
    RUN(test_shim_fontface_load_url);
    RUN(test_shim_fontface_load_direct_path);
    RUN(test_shim_fontface_load_failure);
    RUN(test_shim_fontfaceset_add_check);
    RUN(test_shim_fontfaceset_ready);
    RUN(test_shim_fontfaceset_foreach);
    RUN(test_shim_fontfaceset_delete);
    RUN(test_shim_canvas2d_with_font);

    printf("\n%d/%d tests passed.\n", _tests_passed, _tests_run);
    return _tests_failed > 0 ? 1 : 0;
}
