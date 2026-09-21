/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#include "rendering/image_loader.h"
#include "bindings/bind_image.h"
#include "bindings/bind_io.h"
#include "io/file_io.h"
#include "engine/js_engine.h"

#include <quickjs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Minimal test framework */

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    tests_run++; \
    printf("  [%d] %s ... ", tests_run, #name); \
    name(); \
    printf("PASS\n"); \
    tests_passed++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    assertion failed: %s\n    at %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ_INT(a, b) do { \
    int _a = (a), _b = (b); \
    if (_a != _b) { \
        printf("FAIL\n    expected %d == %d\n    at %s:%d\n", \
               _a, _b, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ_UINT(a, b) do { \
    unsigned int _a = (a), _b = (b); \
    if (_a != _b) { \
        printf("FAIL\n    expected %u == %u\n    at %s:%d\n", \
               _a, _b, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* Test fixture paths */

static const char *TEST_PNG_4x4 = "tests/fixtures/test_4x4.png";
static const char *TEST_PNG_8x2 = "tests/fixtures/test_8x2.png";
static const char *TEST_JPEG_6x4 = "tests/fixtures/test_6x4.jpg";

/* C-level image loader tests */

TEST(init_shutdown)
{
    image_loader_init(false);
    ASSERT_EQ_UINT(image_cache_count(), 0);
    image_loader_shutdown();
}

TEST(load_jpeg_by_content)
{
    /* Browsers decode by content, not by file name, and games do ship JPEG
       data under a .png name; the loader must accept both. */
    image_loader_init(false);
    ImageHandle h = image_load(TEST_JPEG_6x4);
    ASSERT(h != IMAGE_HANDLE_INVALID);
    ImageInfo info;
    ASSERT(image_get_info(h, &info));
    ASSERT_EQ_INT(info.width, 6);
    ASSERT_EQ_INT(info.height, 4);
    const uint8_t *px = image_get_pixels(h);
    ASSERT(px != NULL);
    /* Left half red, right half blue (JPEG is lossy, so allow a wide band). */
    ASSERT(px[0] > 180 && px[2] < 80);
    const uint8_t *right = px + 4 * 4;
    ASSERT(right[2] > 180 && right[0] < 80);
    image_loader_shutdown();
}

TEST(load_png_4x4)
{
    image_loader_init(false);

    ImageHandle h = image_load(TEST_PNG_4x4);
    ASSERT(h != IMAGE_HANDLE_INVALID);

    ImageInfo info;
    ASSERT(image_get_info(h, &info));
    ASSERT_EQ_INT(info.width, 4);
    ASSERT_EQ_INT(info.height, 4);
    ASSERT_EQ_UINT(info.gl_texture, 0); /* No GL in headless mode. */

    image_loader_shutdown();
}

TEST(load_png_8x2)
{
    image_loader_init(false);

    ImageHandle h = image_load(TEST_PNG_8x2);
    ASSERT(h != IMAGE_HANDLE_INVALID);

    ImageInfo info;
    ASSERT(image_get_info(h, &info));
    ASSERT_EQ_INT(info.width, 8);
    ASSERT_EQ_INT(info.height, 2);

    image_loader_shutdown();
}

TEST(load_nonexistent_returns_invalid)
{
    image_loader_init(false);

    ImageHandle h = image_load("tests/fixtures/nonexistent.png");
    ASSERT(h == IMAGE_HANDLE_INVALID);

    image_loader_shutdown();
}

TEST(load_null_path_returns_invalid)
{
    image_loader_init(false);

    ImageHandle h = image_load(NULL);
    ASSERT(h == IMAGE_HANDLE_INVALID);

    image_loader_shutdown();
}

TEST(get_pixels_valid)
{
    image_loader_init(false);

    ImageHandle h = image_load(TEST_PNG_4x4);
    ASSERT(h != IMAGE_HANDLE_INVALID);

    const uint8_t *pixels = image_get_pixels(h);
    ASSERT(pixels != NULL);

    /* Fixture layout: (0,0) red, (2,0) green, (0,2) blue, (3,3) white. */
    ASSERT_EQ_INT(pixels[0], 255);
    ASSERT_EQ_INT(pixels[1], 0);
    ASSERT_EQ_INT(pixels[2], 0);
    ASSERT_EQ_INT(pixels[3], 255);

    int idx = (0 * 4 + 2) * 4;
    ASSERT_EQ_INT(pixels[idx + 0], 0);
    ASSERT_EQ_INT(pixels[idx + 1], 255);
    ASSERT_EQ_INT(pixels[idx + 2], 0);
    ASSERT_EQ_INT(pixels[idx + 3], 255);

    idx = (2 * 4 + 0) * 4;
    ASSERT_EQ_INT(pixels[idx + 0], 0);
    ASSERT_EQ_INT(pixels[idx + 1], 0);
    ASSERT_EQ_INT(pixels[idx + 2], 255);
    ASSERT_EQ_INT(pixels[idx + 3], 255);

    idx = (3 * 4 + 3) * 4;
    ASSERT_EQ_INT(pixels[idx + 0], 255);
    ASSERT_EQ_INT(pixels[idx + 1], 255);
    ASSERT_EQ_INT(pixels[idx + 2], 255);
    ASSERT_EQ_INT(pixels[idx + 3], 255);

    image_loader_shutdown();
}

TEST(get_pixels_invalid_handle)
{
    image_loader_init(false);

    const uint8_t *pixels = image_get_pixels(IMAGE_HANDLE_INVALID);
    ASSERT(pixels == NULL);

    const uint8_t *pixels2 = image_get_pixels(999);
    ASSERT(pixels2 == NULL);

    image_loader_shutdown();
}

TEST(texture_cache_hit)
{
    image_loader_init(false);

    ImageHandle h1 = image_load(TEST_PNG_4x4);
    ASSERT(h1 != IMAGE_HANDLE_INVALID);
    ASSERT_EQ_UINT(image_cache_count(), 1);

    ImageHandle h2 = image_load(TEST_PNG_4x4);
    ASSERT(h2 == h1);
    ASSERT_EQ_UINT(image_cache_count(), 1);

    image_loader_shutdown();
}

TEST(texture_cache_different_paths)
{
    image_loader_init(false);

    ImageHandle h1 = image_load(TEST_PNG_4x4);
    ImageHandle h2 = image_load(TEST_PNG_8x2);
    ASSERT(h1 != IMAGE_HANDLE_INVALID);
    ASSERT(h2 != IMAGE_HANDLE_INVALID);
    ASSERT(h1 != h2);
    ASSERT_EQ_UINT(image_cache_count(), 2);

    image_loader_shutdown();
}

TEST(cache_clear)
{
    image_loader_init(false);

    image_load(TEST_PNG_4x4);
    image_load(TEST_PNG_8x2);
    ASSERT_EQ_UINT(image_cache_count(), 2);

    image_cache_clear();
    ASSERT_EQ_UINT(image_cache_count(), 0);

    image_loader_shutdown();
}

TEST(free_image)
{
    image_loader_init(false);

    ImageHandle h = image_load(TEST_PNG_4x4);
    ASSERT(h != IMAGE_HANDLE_INVALID);
    ASSERT_EQ_UINT(image_cache_count(), 1);

    image_free(h);
    ASSERT_EQ_UINT(image_cache_count(), 0);

    ImageInfo info;
    ASSERT(!image_get_info(h, &info));

    image_loader_shutdown();
}

TEST(free_invalid_handle)
{
    image_loader_init(false);

    /* Should not crash. */
    image_free(IMAGE_HANDLE_INVALID);
    image_free(999);

    image_loader_shutdown();
}

TEST(load_from_memory)
{
    image_loader_init(false);

    size_t file_size = 0;
    uint8_t *data = file_io_read_binary(TEST_PNG_4x4, &file_size);
    ASSERT(data != NULL);
    ASSERT(file_size > 0);

    ImageHandle h = image_load_from_memory("mem:test1", data, file_size);
    free(data);

    ASSERT(h != IMAGE_HANDLE_INVALID);

    ImageInfo info;
    ASSERT(image_get_info(h, &info));
    ASSERT_EQ_INT(info.width, 4);
    ASSERT_EQ_INT(info.height, 4);

    /* Cache hit on same key. */
    ASSERT(image_load_from_memory("mem:test1", NULL, 0) == h);

    image_loader_shutdown();
}

TEST(load_from_memory_invalid)
{
    image_loader_init(false);

    ASSERT(image_load_from_memory(NULL, NULL, 0) == IMAGE_HANDLE_INVALID);
    ASSERT(image_load_from_memory("key", NULL, 0) == IMAGE_HANDLE_INVALID);

    uint8_t garbage[] = {0, 1, 2, 3, 4, 5};
    ASSERT(image_load_from_memory("bad", garbage, sizeof(garbage)) == IMAGE_HANDLE_INVALID);

    image_loader_shutdown();
}

TEST(get_info_invalid)
{
    image_loader_init(false);

    ImageInfo info;
    ASSERT(!image_get_info(IMAGE_HANDLE_INVALID, &info));
    ASSERT(!image_get_info(0, NULL));
    ASSERT(!image_get_info(999, &info));

    image_loader_shutdown();
}

TEST(reload_after_free)
{
    image_loader_init(false);

    ImageHandle h1 = image_load(TEST_PNG_4x4);
    ASSERT(h1 != IMAGE_HANDLE_INVALID);

    image_free(h1);

    /* The handle may be recycled. */
    ImageHandle h2 = image_load(TEST_PNG_4x4);
    ASSERT(h2 != IMAGE_HANDLE_INVALID);

    ImageInfo info;
    ASSERT(image_get_info(h2, &info));
    ASSERT_EQ_INT(info.width, 4);
    ASSERT_EQ_INT(info.height, 4);

    image_loader_shutdown();
}

/* JS-level binding tests */

static JSEngine *g_engine = NULL;

static void setup_js(void)
{
    g_engine = js_engine_init();
    image_loader_init(false);
    bind_io_register(js_engine_get_context(g_engine));
    bind_image_register(js_engine_get_context(g_engine));
}

static void teardown_js(void)
{
    image_loader_shutdown();
    js_engine_shutdown(g_engine);
    g_engine = NULL;
}

static bool js_eval_bool(const char *script)
{
    char *result = js_engine_eval_string(g_engine, script, "<test>");
    if (!result) return false;
    bool ok = strcmp(result, "true") == 0;
    js_engine_free_string(result);
    return ok;
}

/* Caller frees the result. */
static char *js_eval_str(const char *script)
{
    return js_engine_eval_string(g_engine, script, "<test>");
}

TEST(js_native_image_exists)
{
    setup_js();
    ASSERT(js_eval_bool("typeof __native_image !== 'undefined'"));
    ASSERT(js_eval_bool("typeof __native_image.loadImage === 'function'"));
    ASSERT(js_eval_bool("typeof __native_image.loadImageFromMemory === 'function'"));
    ASSERT(js_eval_bool("typeof __native_image.getImageInfo === 'function'"));
    ASSERT(js_eval_bool("typeof __native_image.getImagePixels === 'function'"));
    ASSERT(js_eval_bool("typeof __native_image.freeImage === 'function'"));
    ASSERT(js_eval_bool("typeof __native_image.cacheCount === 'function'"));
    ASSERT(js_eval_bool("typeof __native_image.clearCache === 'function'"));
    teardown_js();
}

TEST(js_load_image)
{
    setup_js();

    ASSERT(js_eval_bool(
        "var img = __native_image.loadImage('tests/fixtures/test_4x4.png');"
        "img !== null && img.width === 4 && img.height === 4 && img.handle > 0"
    ));

    teardown_js();
}

TEST(js_load_image_8x2)
{
    setup_js();

    ASSERT(js_eval_bool(
        "var img = __native_image.loadImage('tests/fixtures/test_8x2.png');"
        "img !== null && img.width === 8 && img.height === 2"
    ));

    teardown_js();
}

TEST(js_load_image_nonexistent)
{
    setup_js();

    ASSERT(js_eval_bool(
        "__native_image.loadImage('tests/fixtures/missing.png') === null"
    ));

    teardown_js();
}

TEST(js_cache_count)
{
    setup_js();

    ASSERT(js_eval_bool("__native_image.cacheCount() === 0"));

    ASSERT(js_eval_bool(
        "__native_image.loadImage('tests/fixtures/test_4x4.png');"
        "__native_image.cacheCount() === 1"
    ));

    /* Cache hit: count stays the same. */
    ASSERT(js_eval_bool(
        "__native_image.loadImage('tests/fixtures/test_4x4.png');"
        "__native_image.cacheCount() === 1"
    ));

    ASSERT(js_eval_bool(
        "__native_image.loadImage('tests/fixtures/test_8x2.png');"
        "__native_image.cacheCount() === 2"
    ));

    teardown_js();
}

TEST(js_clear_cache)
{
    setup_js();

    ASSERT(js_eval_bool(
        "__native_image.loadImage('tests/fixtures/test_4x4.png');"
        "__native_image.loadImage('tests/fixtures/test_8x2.png');"
        "__native_image.cacheCount() === 2"
    ));

    ASSERT(js_eval_bool(
        "__native_image.clearCache();"
        "__native_image.cacheCount() === 0"
    ));

    teardown_js();
}

TEST(js_free_image)
{
    setup_js();

    ASSERT(js_eval_bool(
        "var img = __native_image.loadImage('tests/fixtures/test_4x4.png');"
        "__native_image.cacheCount() === 1"
    ));

    ASSERT(js_eval_bool(
        "__native_image.freeImage(img.handle);"
        "__native_image.cacheCount() === 0"
    ));

    teardown_js();
}

TEST(js_get_image_info)
{
    setup_js();

    ASSERT(js_eval_bool(
        "var img = __native_image.loadImage('tests/fixtures/test_4x4.png');"
        "var info = __native_image.getImageInfo(img.handle);"
        "info !== null && info.width === 4 && info.height === 4"
    ));

    /* Invalid handle returns null. */
    ASSERT(js_eval_bool(
        "__native_image.getImageInfo(99999) === null"
    ));

    teardown_js();
}

TEST(js_get_image_pixels)
{
    setup_js();

    ASSERT(js_eval_bool(
        "var img = __native_image.loadImage('tests/fixtures/test_4x4.png');"
        "var pixels = __native_image.getImagePixels(img.handle);"
        "pixels instanceof ArrayBuffer && pixels.byteLength === 4 * 4 * 4"
    ));

    ASSERT(js_eval_bool(
        "var view = new Uint8Array(pixels);"
        "view[0] === 255 && view[1] === 0 && view[2] === 0 && view[3] === 255"
    ));

    /* Invalid handle returns null. */
    ASSERT(js_eval_bool(
        "__native_image.getImagePixels(99999) === null"
    ));

    teardown_js();
}

TEST(js_load_image_from_memory)
{
    setup_js();

    ASSERT(js_eval_bool(
        "var data = __native_io.readFileBinary('tests/fixtures/test_4x4.png');"
        "var img = __native_image.loadImageFromMemory('mem:js_test', data);"
        "img !== null && img.width === 4 && img.height === 4"
    ));

    teardown_js();
}

TEST(js_load_image_from_memory_invalid)
{
    setup_js();

    ASSERT(js_eval_bool(
        "var bad = new ArrayBuffer(10);"
        "__native_image.loadImageFromMemory('mem:bad', bad) === null"
    ));

    teardown_js();
}

TEST(js_htmlimage_with_native_loader)
{
    setup_js();

    ASSERT(js_engine_eval_file(g_engine, "src/shims/dom_shim.js"));

    /* The src setter loads via __native_image.loadImage in a microtask. */
    ASSERT(js_engine_eval(g_engine,
        "var testImg = new Image();\n"
        "var imgLoaded = false;\n"
        "var imgWidth = 0;\n"
        "var imgHeight = 0;\n"
        "testImg.onload = function() {\n"
        "    imgLoaded = true;\n"
        "    imgWidth = testImg.naturalWidth;\n"
        "    imgHeight = testImg.naturalHeight;\n"
        "};\n"
        "testImg.src = 'tests/fixtures/test_4x4.png';\n",
        "<test>"));

    js_engine_execute_pending_jobs(g_engine);

    ASSERT(js_eval_bool("imgLoaded === true"));
    ASSERT(js_eval_bool("imgWidth === 4"));
    ASSERT(js_eval_bool("imgHeight === 4"));
    ASSERT(js_eval_bool("testImg.complete === true"));
    ASSERT(js_eval_bool("testImg.naturalWidth === 4"));
    ASSERT(js_eval_bool("testImg.naturalHeight === 4"));

    teardown_js();
}

TEST(js_htmlimage_error)
{
    setup_js();

    ASSERT(js_engine_eval_file(g_engine, "src/shims/dom_shim.js"));

    ASSERT(js_engine_eval(g_engine,
        "var errImg = new Image();\n"
        "var errFired = false;\n"
        "errImg.onerror = function() { errFired = true; };\n"
        "errImg.src = 'tests/fixtures/nonexistent.png';\n",
        "<test>"));

    js_engine_execute_pending_jobs(g_engine);

    ASSERT(js_eval_bool("errFired === true"));
    ASSERT(js_eval_bool("errImg.complete === true"));

    teardown_js();
}

TEST(js_htmlimage_blob_url)
{
    setup_js();

    ASSERT(js_engine_eval_file(g_engine, "src/shims/dom_shim.js"));

    ASSERT(js_engine_eval(g_engine,
        "var pngData = __native_io.readFileBinary('tests/fixtures/test_8x2.png');\n"
        "var blob = new Blob([pngData], {type: 'image/png'});\n"
        "var blobUrl = URL.createObjectURL(blob);\n"
        "var blobImg = new Image();\n"
        "var blobLoaded = false;\n"
        "var blobW = 0, blobH = 0;\n"
        "blobImg.onload = function() {\n"
        "    blobLoaded = true;\n"
        "    blobW = blobImg.naturalWidth;\n"
        "    blobH = blobImg.naturalHeight;\n"
        "};\n"
        "blobImg.src = blobUrl;\n",
        "<test>"));

    js_engine_execute_pending_jobs(g_engine);

    ASSERT(js_eval_bool("blobLoaded === true"));
    ASSERT(js_eval_bool("blobW === 8"));
    ASSERT(js_eval_bool("blobH === 2"));
    ASSERT(js_eval_bool("blobImg.complete === true"));

    teardown_js();
}

TEST(js_htmlimage_data_url)
{
    setup_js();

    ASSERT(js_engine_eval_file(g_engine, "src/shims/dom_shim.js"));

    ASSERT(js_engine_eval(g_engine,
        "var dataImg = new Image();\n"
        "var dataLoaded = false;\n"
        "dataImg.onload = function() { dataLoaded = true; };\n"
        "dataImg.src = 'data:image/png;base64,AAAA';\n",
        "<test>"));

    js_engine_execute_pending_jobs(g_engine);

    ASSERT(js_eval_bool("dataLoaded === true"));
    ASSERT(js_eval_bool("dataImg.complete === true"));

    teardown_js();
}

TEST(js_htmlimage_empty_src)
{
    setup_js();

    ASSERT(js_engine_eval_file(g_engine, "src/shims/dom_shim.js"));

    ASSERT(js_engine_eval(g_engine,
        "var emptyImg = new Image();\n"
        "var emptyLoaded = false;\n"
        "emptyImg.onload = function() { emptyLoaded = true; };\n"
        "emptyImg.src = '';\n",
        "<test>"));

    js_engine_execute_pending_jobs(g_engine);

    ASSERT(js_eval_bool("emptyLoaded === true"));

    teardown_js();
}

/* Main */

int main(void)
{
    printf("=== Image Loader Tests ===\n\n");

    printf("-- C-level tests --\n");
    RUN(init_shutdown);
    RUN(load_png_4x4);
    RUN(load_jpeg_by_content);
    RUN(load_png_8x2);
    RUN(load_nonexistent_returns_invalid);
    RUN(load_null_path_returns_invalid);
    RUN(get_pixels_valid);
    RUN(get_pixels_invalid_handle);
    RUN(texture_cache_hit);
    RUN(texture_cache_different_paths);
    RUN(cache_clear);
    RUN(free_image);
    RUN(free_invalid_handle);
    RUN(load_from_memory);
    RUN(load_from_memory_invalid);
    RUN(get_info_invalid);
    RUN(reload_after_free);

    printf("\n-- JS binding tests --\n");
    RUN(js_native_image_exists);
    RUN(js_load_image);
    RUN(js_load_image_8x2);
    RUN(js_load_image_nonexistent);
    RUN(js_cache_count);
    RUN(js_clear_cache);
    RUN(js_free_image);
    RUN(js_get_image_info);
    RUN(js_get_image_pixels);
    RUN(js_load_image_from_memory);
    RUN(js_load_image_from_memory_invalid);

    printf("\n-- HTMLImageElement integration tests --\n");
    RUN(js_htmlimage_with_native_loader);
    RUN(js_htmlimage_error);
    RUN(js_htmlimage_blob_url);
    RUN(js_htmlimage_data_url);
    RUN(js_htmlimage_empty_src);

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
