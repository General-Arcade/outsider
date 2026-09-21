/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

/*
 * tests/test_pixi_shim.c — Tests for the PIXI.js compatibility shim and the
 * native renderer/sprite batch beneath it (C level, JS bindings, and shim).
 */

#include "rendering/sprite_batch.h"
#include "rendering/renderer.h"
#include "engine/js_engine.h"
#include "bindings/bind_renderer.h"
#include "bindings/bind_io.h"
#include "io/file_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Tiny test framework */

static int _tests_run = 0;
static int _tests_passed = 0;
static int _tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    _tests_run++; \
    printf("  [%d] %s ... ", _tests_run, #name); \
    name(); \
    if (_tests_failed == _tests_run - _tests_passed - 1) { \
        _tests_passed++; \
        printf("PASS\n"); \
    } \
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

/* C-level sprite batch tests (headless, no GL) */

TEST(test_sprite_batch_create_destroy)
{
    SpriteBatch *sb = sprite_batch_create(256);
    ASSERT(sb != NULL);
    ASSERT_EQ_INT(sprite_batch_get_draw_calls(sb), 0);
    ASSERT_EQ_INT(sprite_batch_get_quad_count(sb), 0);
    sprite_batch_destroy(sb);
}

TEST(test_sprite_batch_null_safety)
{
    /* All functions should handle NULL gracefully. */
    sprite_batch_destroy(NULL);
    sprite_batch_begin(NULL);
    sprite_batch_draw(NULL, 0, 0, 0, 10, 10, 0, 0, 1, 1, 0xFFFFFFFF, 1.0f);
    sprite_batch_flush(NULL);
    sprite_batch_end(NULL);
    sprite_batch_set_blend_mode(NULL, BLEND_MODE_ADD);
    sprite_batch_set_projection(NULL, 800, 600);
    ASSERT_EQ_INT(sprite_batch_get_draw_calls(NULL), 0);
    ASSERT_EQ_INT(sprite_batch_get_quad_count(NULL), 0);
}

TEST(test_sprite_batch_default_max)
{
    /* Passing 0 should use default (4096). */
    SpriteBatch *sb = sprite_batch_create(0);
    ASSERT(sb != NULL);
    sprite_batch_destroy(sb);
}

/* C-level renderer tests (headless, no GL) */

TEST(test_renderer_create_destroy)
{
    /* Works without a GL context: the batch allocates no GL resources. */
    Renderer *r = renderer_create(816, 624);
    ASSERT(r != NULL);

    int w, h;
    renderer_get_size(r, &w, &h);
    ASSERT_EQ_INT(w, 816);
    ASSERT_EQ_INT(h, 624);

    SpriteBatch *batch = renderer_get_batch(r);
    ASSERT(batch != NULL);

    renderer_destroy(r);
}

TEST(test_renderer_resize)
{
    Renderer *r = renderer_create(800, 600);
    ASSERT(r != NULL);

    renderer_resize(r, 1280, 720);
    int w, h;
    renderer_get_size(r, &w, &h);
    ASSERT_EQ_INT(w, 1280);
    ASSERT_EQ_INT(h, 720);

    renderer_destroy(r);
}

TEST(test_renderer_null_safety)
{
    renderer_destroy(NULL);
    renderer_resize(NULL, 100, 100);
    renderer_begin_frame(NULL);
    renderer_end_frame(NULL);
    renderer_bind_fbo(NULL, 0, 0, 0);
    renderer_unbind_fbo(NULL);

    int w = -1, h = -1;
    renderer_get_size(NULL, &w, &h);
    /* Should not crash; w,h remain unchanged. */
    ASSERT_EQ_INT(w, -1);

    ASSERT(renderer_get_batch(NULL) == NULL);
}

/* JS binding tests */

static JSEngine *_js = NULL;

static void js_setup(void)
{
    _js = js_engine_init();
    bind_io_register(js_engine_get_context(_js));
    bind_renderer_register(js_engine_get_context(_js));
}

static void js_teardown(void)
{
    /* Ensure native renderer is shut down between tests. */
    js_engine_eval(_js, "__native_renderer.shutdown();", "<teardown>");
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

TEST(test_js_renderer_binding_exists)
{
    js_setup();
    ASSERT(js_eval_bool("typeof __native_renderer === 'object';"));
    ASSERT(js_eval_bool("typeof __native_renderer.init === 'function';"));
    ASSERT(js_eval_bool("typeof __native_renderer.resize === 'function';"));
    ASSERT(js_eval_bool("typeof __native_renderer.beginFrame === 'function';"));
    ASSERT(js_eval_bool("typeof __native_renderer.endFrame === 'function';"));
    ASSERT(js_eval_bool("typeof __native_renderer.createTexture === 'function';"));
    ASSERT(js_eval_bool("typeof __native_renderer.deleteTexture === 'function';"));
    ASSERT(js_eval_bool("typeof __native_renderer.createRenderTexture === 'function';"));
    ASSERT(js_eval_bool("typeof __native_renderer.deleteRenderTexture === 'function';"));
    ASSERT(js_eval_bool("typeof __native_renderer.drawQuad === 'function';"));
    ASSERT(js_eval_bool("typeof __native_renderer.flush === 'function';"));
    ASSERT(js_eval_bool("typeof __native_renderer.setBlendMode === 'function';"));
    ASSERT(js_eval_bool("typeof __native_renderer.getDrawCalls === 'function';"));
    ASSERT(js_eval_bool("typeof __native_renderer.getQuadCount === 'function';"));
    js_teardown();
}

TEST(test_js_renderer_init_get_size)
{
    js_setup();
    ASSERT(js_eval_bool(
        "__native_renderer.init(816, 624);"
        "var s = __native_renderer.getSize();"
        "s.width === 816 && s.height === 624;"
    ));
    js_teardown();
}

TEST(test_js_renderer_resize)
{
    js_setup();
    ASSERT(js_eval_bool(
        "__native_renderer.init(800, 600);"
        "__native_renderer.resize(1280, 720);"
        "var s = __native_renderer.getSize();"
        "s.width === 1280 && s.height === 720;"
    ));
    js_teardown();
}

TEST(test_js_renderer_draw_calls)
{
    js_setup();
    ASSERT(js_eval_bool(
        "__native_renderer.init(800, 600);"
        "var dc = __native_renderer.getDrawCalls();"
        "var qc = __native_renderer.getQuadCount();"
        "dc === 0 && qc === 0;"
    ));
    js_teardown();
}

/* JS shim-level tests (PIXI namespace) */

static void js_setup_with_shims(void)
{
    _js = js_engine_init();
    bind_io_register(js_engine_get_context(_js));
    bind_renderer_register(js_engine_get_context(_js));

    /* Load shims in order. */
    js_engine_eval_file(_js, "src/shims/dom_shim.js");
    js_engine_eval_file(_js, "src/shims/canvas2d_shim.js");
    js_engine_eval_file(_js, "src/shims/navigator_shim.js");
    js_engine_eval_file(_js, "src/shims/xhr_shim.js");
    js_engine_eval_file(_js, "src/shims/pixi_shim.js");
}

TEST(test_pixi_namespace_exists)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool("typeof PIXI === 'object';"));
    ASSERT(js_eval_bool("typeof PIXI.settings === 'object';"));
    ASSERT(js_eval_bool("typeof PIXI.utils === 'object';"));
    ASSERT(js_eval_bool("typeof PIXI.SCALE_MODES === 'object';"));
    ASSERT(js_eval_bool("typeof PIXI.BLEND_MODES === 'object';"));
    ASSERT(js_eval_bool("typeof PIXI.Rectangle === 'function';"));
    ASSERT(js_eval_bool("typeof PIXI.BaseTexture === 'function';"));
    ASSERT(js_eval_bool("typeof PIXI.Texture === 'function';"));
    ASSERT(js_eval_bool("typeof PIXI.RenderTexture === 'function';"));
    ASSERT(js_eval_bool("typeof PIXI.Matrix === 'function';"));
    ASSERT(js_eval_bool("typeof PIXI.Transform === 'function';"));
    ASSERT(js_eval_bool("typeof PIXI.Point === 'function';"));
    ASSERT(js_eval_bool("typeof PIXI.ObservablePoint === 'function';"));
    js_teardown();
}

TEST(test_pixi_scale_modes)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool("PIXI.SCALE_MODES.NEAREST === 0;"));
    ASSERT(js_eval_bool("PIXI.SCALE_MODES.LINEAR === 1;"));
    js_teardown();
}

TEST(test_pixi_blend_modes)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool("PIXI.BLEND_MODES.NORMAL === 0;"));
    ASSERT(js_eval_bool("PIXI.BLEND_MODES.ADD === 1;"));
    ASSERT(js_eval_bool("PIXI.BLEND_MODES.MULTIPLY === 2;"));
    ASSERT(js_eval_bool("PIXI.BLEND_MODES.SCREEN === 3;"));
    js_teardown();
}

TEST(test_pixi_settings)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool("PIXI.settings.RESOLUTION === 1;"));
    ASSERT(js_eval_bool("PIXI.settings.ROUND_PIXELS === false;"));
    ASSERT(js_eval_bool("PIXI.settings.SCALE_MODE === PIXI.SCALE_MODES.NEAREST;"));
    ASSERT(js_eval_bool("typeof PIXI.settings.GC_MAX_IDLE === 'number';"));
    js_teardown();
}

TEST(test_pixi_utils)
{
    js_setup_with_shims();
    /* skipHello is a no-op function. */
    ASSERT(js_eval_bool("typeof PIXI.utils.skipHello === 'function';"));
    ASSERT(js_eval_bool("PIXI.utils.skipHello(); true;"));

    /* string2hex / hex2string. */
    ASSERT(js_eval_bool("PIXI.utils.string2hex('#ff0000') === 0xff0000;"));
    ASSERT(js_eval_bool("PIXI.utils.hex2string(0xff0000) === '#ff0000';"));
    ASSERT(js_eval_bool("PIXI.utils.hex2string(0x00ff00) === '#00ff00';"));

    /* uid returns unique IDs. */
    ASSERT(js_eval_bool(
        "var a = PIXI.utils.uid();"
        "var b = PIXI.utils.uid();"
        "a !== b;"
    ));

    /* isWebGLSupported. */
    ASSERT(js_eval_bool("PIXI.utils.isWebGLSupported() === true;"));

    /* TextureCache and BaseTextureCache exist. */
    ASSERT(js_eval_bool("typeof PIXI.utils.TextureCache === 'object';"));
    ASSERT(js_eval_bool("typeof PIXI.utils.BaseTextureCache === 'object';"));

    /* EventEmitter exists. */
    ASSERT(js_eval_bool("typeof PIXI.utils.EventEmitter === 'function';"));
    js_teardown();
}

TEST(test_pixi_rectangle)
{
    js_setup_with_shims();
    /* Constructor. */
    ASSERT(js_eval_bool(
        "var r = new PIXI.Rectangle(10, 20, 100, 50);"
        "r.x === 10 && r.y === 20 && r.width === 100 && r.height === 50;"
    ));

    /* Properties. */
    ASSERT(js_eval_bool(
        "var r = new PIXI.Rectangle(10, 20, 100, 50);"
        "r.left === 10 && r.right === 110 && r.top === 20 && r.bottom === 70;"
    ));

    /* Clone. */
    ASSERT(js_eval_bool(
        "var r1 = new PIXI.Rectangle(5, 10, 50, 25);"
        "var r2 = r1.clone();"
        "r2.x === 5 && r2.y === 10 && r2.width === 50 && r2.height === 25 && r1 !== r2;"
    ));

    /* Contains. */
    ASSERT(js_eval_bool(
        "var r = new PIXI.Rectangle(0, 0, 100, 100);"
        "r.contains(50, 50) === true && r.contains(150, 50) === false;"
    ));

    /* Pad. */
    ASSERT(js_eval_bool(
        "var r = new PIXI.Rectangle(10, 10, 20, 20);"
        "r.pad(5);"
        "r.x === 5 && r.y === 5 && r.width === 30 && r.height === 30;"
    ));

    /* EMPTY static. */
    ASSERT(js_eval_bool(
        "PIXI.Rectangle.EMPTY.width === 0 && PIXI.Rectangle.EMPTY.height === 0;"
    ));
    js_teardown();
}

TEST(test_pixi_base_texture)
{
    js_setup_with_shims();
    /* Constructor. */
    ASSERT(js_eval_bool(
        "var bt = new PIXI.BaseTexture();"
        "bt.width === 0 && bt.height === 0 && bt.valid === false;"
    ));

    /* setSize. */
    ASSERT(js_eval_bool(
        "var bt = new PIXI.BaseTexture();"
        "bt.setSize(100, 50);"
        "bt.width === 100 && bt.height === 50 && bt.valid === true;"
    ));

    /* setRealSize. */
    ASSERT(js_eval_bool(
        "var bt = new PIXI.BaseTexture();"
        "bt.setRealSize(200, 100, 2);"
        "bt.realWidth === 200 && bt.realHeight === 100 && bt.width === 100 && bt.height === 50;"
    ));

    /* scaleMode from settings. */
    ASSERT(js_eval_bool(
        "var bt = new PIXI.BaseTexture();"
        "bt.scaleMode === PIXI.settings.SCALE_MODE;"
    ));

    /* Destroy. */
    ASSERT(js_eval_bool(
        "var bt = new PIXI.BaseTexture();"
        "bt.setSize(64, 64);"
        "bt.destroy();"
        "bt.destroyed === true && bt.valid === false;"
    ));

    /* from() caching. */
    ASSERT(js_eval_bool(
        "var bt1 = PIXI.BaseTexture.from('test_cache_key');"
        "var bt2 = PIXI.BaseTexture.from('test_cache_key');"
        "bt1 === bt2;"
    ));

    /* fromCanvas. */
    ASSERT(js_eval_bool(
        "var canvas = { width: 64, height: 32, tagName: 'CANVAS' };"
        "var bt = PIXI.BaseTexture.fromCanvas(canvas);"
        "bt.width === 64 && bt.height === 32;"
    ));
    js_teardown();
}

TEST(test_pixi_texture)
{
    js_setup_with_shims();
    /* Constructor with base texture. */
    ASSERT(js_eval_bool(
        "var bt = new PIXI.BaseTexture();"
        "bt.setSize(100, 50);"
        "var t = new PIXI.Texture(bt);"
        "t.width === 100 && t.height === 50 && t.valid === true;"
    ));

    /* Frame. */
    ASSERT(js_eval_bool(
        "var bt = new PIXI.BaseTexture();"
        "bt.setSize(256, 256);"
        "var frame = new PIXI.Rectangle(10, 20, 32, 32);"
        "var t = new PIXI.Texture(bt, frame);"
        "t.frame.x === 10 && t.frame.y === 20 && t.frame.width === 32;"
    ));

    /* UVs. */
    ASSERT(js_eval_bool(
        "var bt = new PIXI.BaseTexture();"
        "bt.setSize(100, 100);"
        "var frame = new PIXI.Rectangle(25, 25, 50, 50);"
        "var t = new PIXI.Texture(bt, frame);"
        "Math.abs(t._uvs.x0 - 0.25) < 0.01 && Math.abs(t._uvs.y0 - 0.25) < 0.01 &&"
        "Math.abs(t._uvs.x2 - 0.75) < 0.01 && Math.abs(t._uvs.y2 - 0.75) < 0.01;"
    ));

    /* Clone. */
    ASSERT(js_eval_bool(
        "var bt = new PIXI.BaseTexture();"
        "bt.setSize(64, 64);"
        "var t1 = new PIXI.Texture(bt, new PIXI.Rectangle(0, 0, 32, 32));"
        "var t2 = t1.clone();"
        "t2.frame.width === 32 && t2 !== t1 && t2.baseTexture === t1.baseTexture;"
    ));

    /* Texture.EMPTY. */
    ASSERT(js_eval_bool(
        "PIXI.Texture.EMPTY !== undefined && PIXI.Texture.EMPTY !== null;"
    ));

    /* Texture.WHITE. */
    ASSERT(js_eval_bool(
        "PIXI.Texture.WHITE !== undefined && PIXI.Texture.WHITE.baseTexture.valid === true;"
    ));

    /* Destroy. */
    ASSERT(js_eval_bool(
        "var bt = new PIXI.BaseTexture();"
        "bt.setSize(32, 32);"
        "var t = new PIXI.Texture(bt);"
        "t.destroy(false);"
        "t.destroyed === true;"
    ));

    /* Texture.from caching. */
    ASSERT(js_eval_bool(
        "var t1 = PIXI.Texture.from('my_texture_key');"
        "var t2 = PIXI.Texture.from('my_texture_key');"
        "t1 === t2;"
    ));

    /* Texture.addToCache / removeFromCache. */
    ASSERT(js_eval_bool(
        "var bt = new PIXI.BaseTexture();"
        "bt.setSize(16, 16);"
        "var t = new PIXI.Texture(bt);"
        "PIXI.Texture.addToCache(t, 'cached_tex');"
        "PIXI.utils.TextureCache['cached_tex'] === t;"
    ));
    ASSERT(js_eval_bool(
        "var removed = PIXI.Texture.removeFromCache('cached_tex');"
        "removed !== null && PIXI.utils.TextureCache['cached_tex'] === undefined;"
    ));
    js_teardown();
}

TEST(test_pixi_render_texture)
{
    js_setup_with_shims();
    /* RenderTexture.create. */
    ASSERT(js_eval_bool(
        "var rt = PIXI.RenderTexture.create({ width: 256, height: 128 });"
        "rt.width === 256 && rt.height === 128 && rt.valid === true;"
    ));

    /* Resize. */
    ASSERT(js_eval_bool(
        "var rt = PIXI.RenderTexture.create({ width: 100, height: 100 });"
        "rt.resize(200, 150);"
        "rt.baseTexture.width === 200 && rt.baseTexture.height === 150;"
    ));

    /* Inherits from Texture. */
    ASSERT(js_eval_bool(
        "var rt = PIXI.RenderTexture.create({ width: 64, height: 64 });"
        "rt instanceof PIXI.Texture;"
    ));

    /* Legacy create form: create(width, height). */
    ASSERT(js_eval_bool(
        "var rt = PIXI.RenderTexture.create(100, 50);"
        "rt.width === 100 && rt.height === 50;"
    ));
    js_teardown();
}

TEST(test_pixi_matrix)
{
    js_setup_with_shims();
    /* Identity. */
    ASSERT(js_eval_bool(
        "var m = new PIXI.Matrix();"
        "m.a === 1 && m.b === 0 && m.c === 0 && m.d === 1 && m.tx === 0 && m.ty === 0;"
    ));

    /* Translate. */
    ASSERT(js_eval_bool(
        "var m = new PIXI.Matrix();"
        "m.translate(10, 20);"
        "m.tx === 10 && m.ty === 20;"
    ));

    /* Scale. */
    ASSERT(js_eval_bool(
        "var m = new PIXI.Matrix();"
        "m.scale(2, 3);"
        "m.a === 2 && m.d === 3;"
    ));

    /* Non-uniform scale with non-zero b/c (post-multiply: row 0 *= x, row 1 *= y). */
    ASSERT(js_eval_bool(
        "var m = new PIXI.Matrix(1, 2, 3, 4, 0, 0);"
        "m.scale(5, 7);"
        "m.a === 5 && m.b === 14 && m.c === 15 && m.d === 28 && m.tx === 0 && m.ty === 0;"
    ));

    /* Clone. */
    ASSERT(js_eval_bool(
        "var m1 = new PIXI.Matrix(1, 2, 3, 4, 5, 6);"
        "var m2 = m1.clone();"
        "m2.a === 1 && m2.b === 2 && m2.c === 3 && m2.d === 4 && m2.tx === 5 && m2.ty === 6 && m1 !== m2;"
    ));

    /* Apply. */
    ASSERT(js_eval_bool(
        "var m = new PIXI.Matrix();"
        "m.translate(10, 20);"
        "var p = m.apply({x: 5, y: 3});"
        "p.x === 15 && p.y === 23;"
    ));

    /* Invert. */
    ASSERT(js_eval_bool(
        "var m = new PIXI.Matrix();"
        "m.translate(10, 20);"
        "m.invert();"
        "Math.abs(m.tx + 10) < 0.001 && Math.abs(m.ty + 20) < 0.001;"
    ));

    /* Static references. */
    ASSERT(js_eval_bool("PIXI.Matrix.IDENTITY.a === 1;"));
    ASSERT(js_eval_bool("PIXI.Matrix.TEMP_MATRIX !== undefined;"));
    js_teardown();
}

TEST(test_pixi_transform)
{
    js_setup_with_shims();
    /* Default transform. */
    ASSERT(js_eval_bool(
        "var t = new PIXI.Transform();"
        "t.position.x === 0 && t.position.y === 0;"
    ));

    /* Scale. */
    ASSERT(js_eval_bool(
        "var t = new PIXI.Transform();"
        "t.scale.x === 1 && t.scale.y === 1;"
    ));

    /* Rotation. */
    ASSERT(js_eval_bool(
        "var t = new PIXI.Transform();"
        "t.rotation = Math.PI / 2;"
        "Math.abs(t.rotation - Math.PI / 2) < 0.001;"
    ));

    /* Local transform update. */
    ASSERT(js_eval_bool(
        "var t = new PIXI.Transform();"
        "t.position.x = 100;"
        "t.position.y = 50;"
        "t._localID++;"
        "t.updateLocalTransform();"
        "t.localTransform.tx === 100 && t.localTransform.ty === 50;"
    ));

    /* IDENTITY static. */
    ASSERT(js_eval_bool("PIXI.Transform.IDENTITY !== undefined;"));

    /* Skew updates the cached trig terms: _cx/_sx use rotation + skew.y,
       _cy/_sy use rotation - skew.x. */
    ASSERT(js_eval_bool(
        "var t = new PIXI.Transform();"
        "t.skew.set(0.5, 0.3);"
        "Math.abs(t._cx - Math.cos(0.3)) < 0.0001 &&"
        "Math.abs(t._sx - Math.sin(0.3)) < 0.0001 &&"
        "Math.abs(t._cy - Math.sin(0.5)) < 0.0001 &&"
        "Math.abs(t._sy - Math.cos(0.5)) < 0.0001;"
    ));

    /* Skew combined with a non-zero rotation. */
    ASSERT(js_eval_bool(
        "var t = new PIXI.Transform();"
        "t.skew.set(0.2, 0.1);"
        "t.rotation = Math.PI / 4;"
        "var r = Math.PI / 4;"
        "Math.abs(t._cx - Math.cos(r + 0.1)) < 0.0001 &&"
        "Math.abs(t._sx - Math.sin(r + 0.1)) < 0.0001 &&"
        "Math.abs(t._cy - (-Math.sin(r - 0.2))) < 0.0001 &&"
        "Math.abs(t._sy - Math.cos(r - 0.2)) < 0.0001;"
    ));

    /* Setting rotation after skew still incorporates skew values. */
    ASSERT(js_eval_bool(
        "var t = new PIXI.Transform();"
        "t.skew.set(0.3, 0.6);"
        "t.rotation = Math.PI / 3;"
        "var r = Math.PI / 3;"
        "Math.abs(t._cx - Math.cos(r + 0.6)) < 0.0001 &&"
        "Math.abs(t._sx - Math.sin(r + 0.6)) < 0.0001 &&"
        "Math.abs(t._cy - (-Math.sin(r - 0.3))) < 0.0001 &&"
        "Math.abs(t._sy - Math.cos(r - 0.3)) < 0.0001;"
    ));

    js_teardown();
}

TEST(test_pixi_point)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var p = new PIXI.Point(3, 7);"
        "p.x === 3 && p.y === 7;"
    ));
    ASSERT(js_eval_bool(
        "var p1 = new PIXI.Point(3, 7);"
        "var p2 = p1.clone();"
        "p2.x === 3 && p2.y === 7 && p1 !== p2;"
    ));
    ASSERT(js_eval_bool(
        "var p = new PIXI.Point();"
        "p.set(5, 10);"
        "p.x === 5 && p.y === 10;"
    ));
    js_teardown();
}

TEST(test_pixi_observable_point)
{
    js_setup_with_shims();
    /* Callback fires on change. */
    ASSERT(js_eval_bool(
        "var called = 0;"
        "var op = new PIXI.ObservablePoint(function() { called++; }, null, 0, 0);"
        "op.x = 10;"
        "op.y = 20;"
        "called === 2 && op.x === 10 && op.y === 20;"
    ));
    /* No callback on same value. */
    ASSERT(js_eval_bool(
        "var called = 0;"
        "var op = new PIXI.ObservablePoint(function() { called++; }, null, 5, 5);"
        "op.x = 5;"
        "op.y = 5;"
        "called === 0;"
    ));
    js_teardown();
}

TEST(test_pixi_event_emitter)
{
    js_setup_with_shims();
    /* on/emit. */
    ASSERT(js_eval_bool(
        "var ee = new PIXI.utils.EventEmitter();"
        "var val = 0;"
        "ee.on('test', function(v) { val = v; });"
        "ee.emit('test', 42);"
        "val === 42;"
    ));

    /* once fires only once. */
    ASSERT(js_eval_bool(
        "var ee = new PIXI.utils.EventEmitter();"
        "var count = 0;"
        "ee.once('bump', function() { count++; });"
        "ee.emit('bump');"
        "ee.emit('bump');"
        "count === 1;"
    ));

    /* off removes listener. */
    ASSERT(js_eval_bool(
        "var ee = new PIXI.utils.EventEmitter();"
        "var count = 0;"
        "var fn = function() { count++; };"
        "ee.on('evt', fn);"
        "ee.emit('evt');"
        "ee.off('evt', fn);"
        "ee.emit('evt');"
        "count === 1;"
    ));

    /* removeAllListeners. */
    ASSERT(js_eval_bool(
        "var ee = new PIXI.utils.EventEmitter();"
        "var count = 0;"
        "ee.on('a', function() { count++; });"
        "ee.on('b', function() { count++; });"
        "ee.removeAllListeners();"
        "ee.emit('a');"
        "ee.emit('b');"
        "count === 0;"
    ));
    js_teardown();
}

/* PIXI display object tests */

TEST(test_pixi_display_object)
{
    js_setup_with_shims();
    /* DisplayObject exists and can be created. */
    ASSERT(js_eval_bool("typeof PIXI.DisplayObject === 'function';"));
    ASSERT(js_eval_bool(
        "var d = new PIXI.DisplayObject();"
        "d.x === 0 && d.y === 0 && d.alpha === 1 && d.visible === true;"
    ));
    /* Position, scale, rotation. */
    ASSERT(js_eval_bool(
        "var d = new PIXI.DisplayObject();"
        "d.x = 100; d.y = 50;"
        "d.x === 100 && d.y === 50;"
    ));
    ASSERT(js_eval_bool(
        "var d = new PIXI.DisplayObject();"
        "d.scale.x === 1 && d.scale.y === 1;"
    ));
    ASSERT(js_eval_bool(
        "var d = new PIXI.DisplayObject();"
        "d.rotation = Math.PI / 4;"
        "Math.abs(d.rotation - Math.PI / 4) < 0.001;"
    ));
    /* worldTransform and localTransform. */
    ASSERT(js_eval_bool(
        "var d = new PIXI.DisplayObject();"
        "d.worldTransform instanceof PIXI.Matrix && d.localTransform instanceof PIXI.Matrix;"
    ));
    /* blendMode default. */
    ASSERT(js_eval_bool(
        "var d = new PIXI.DisplayObject();"
        "d.blendMode === PIXI.BLEND_MODES.NORMAL;"
    ));
    /* zIndex. */
    ASSERT(js_eval_bool(
        "var d = new PIXI.DisplayObject();"
        "d.zIndex = 5;"
        "d.zIndex === 5;"
    ));
    /* Destroy. */
    ASSERT(js_eval_bool(
        "var d = new PIXI.DisplayObject();"
        "d.destroy();"
        "d._destroyed === true && d.transform === null;"
    ));
    js_teardown();
}

TEST(test_pixi_container)
{
    js_setup_with_shims();
    /* Container exists and extends DisplayObject. */
    ASSERT(js_eval_bool("typeof PIXI.Container === 'function';"));
    ASSERT(js_eval_bool(
        "var c = new PIXI.Container();"
        "c instanceof PIXI.DisplayObject;"
    ));
    /* children array. */
    ASSERT(js_eval_bool(
        "var c = new PIXI.Container();"
        "Array.isArray(c.children) && c.children.length === 0;"
    ));
    js_teardown();
}

TEST(test_pixi_container_add_remove)
{
    js_setup_with_shims();
    /* addChild. */
    ASSERT(js_eval_bool(
        "var parent = new PIXI.Container();"
        "var child = new PIXI.Container();"
        "parent.addChild(child);"
        "parent.children.length === 1 && parent.children[0] === child && child.parent === parent;"
    ));
    /* addChild moves from old parent. */
    ASSERT(js_eval_bool(
        "var p1 = new PIXI.Container();"
        "var p2 = new PIXI.Container();"
        "var c = new PIXI.Container();"
        "p1.addChild(c);"
        "p2.addChild(c);"
        "p1.children.length === 0 && p2.children.length === 1 && c.parent === p2;"
    ));
    /* addChildAt. */
    ASSERT(js_eval_bool(
        "var p = new PIXI.Container();"
        "var c1 = new PIXI.Container();"
        "var c2 = new PIXI.Container();"
        "var c3 = new PIXI.Container();"
        "p.addChild(c1); p.addChild(c3);"
        "p.addChildAt(c2, 1);"
        "p.children.length === 3 && p.children[0] === c1 && p.children[1] === c2 && p.children[2] === c3;"
    ));
    /* removeChild. */
    ASSERT(js_eval_bool(
        "var p = new PIXI.Container();"
        "var c = new PIXI.Container();"
        "p.addChild(c);"
        "p.removeChild(c);"
        "p.children.length === 0 && c.parent === null;"
    ));
    /* removeChildAt. */
    ASSERT(js_eval_bool(
        "var p = new PIXI.Container();"
        "var c1 = new PIXI.Container();"
        "var c2 = new PIXI.Container();"
        "p.addChild(c1); p.addChild(c2);"
        "var removed = p.removeChildAt(0);"
        "removed === c1 && p.children.length === 1 && p.children[0] === c2;"
    ));
    /* removeChildren. */
    ASSERT(js_eval_bool(
        "var p = new PIXI.Container();"
        "for (var i = 0; i < 5; i++) p.addChild(new PIXI.Container());"
        "var removed = p.removeChildren();"
        "removed.length === 5 && p.children.length === 0;"
    ));
    js_teardown();
}

TEST(test_pixi_container_sort)
{
    js_setup_with_shims();
    /* sortChildren by zIndex. */
    ASSERT(js_eval_bool(
        "var p = new PIXI.Container();"
        "p.sortableChildren = true;"
        "var c1 = new PIXI.Container(); c1.zIndex = 3;"
        "var c2 = new PIXI.Container(); c2.zIndex = 1;"
        "var c3 = new PIXI.Container(); c3.zIndex = 2;"
        "p.addChild(c1); p.addChild(c2); p.addChild(c3);"
        "p.sortChildren();"
        "p.children[0] === c2 && p.children[1] === c3 && p.children[2] === c1;"
    ));
    js_teardown();
}

TEST(test_pixi_container_transform_propagation)
{
    js_setup_with_shims();
    /* World transform propagation parent -> child. */
    ASSERT(js_eval_bool(
        "var parent = new PIXI.Container();"
        "parent.x = 100; parent.y = 50;"
        "var child = new PIXI.Container();"
        "child.x = 10; child.y = 20;"
        "parent.addChild(child);"
        "parent.transform._localID++;"
        "child.transform._localID++;"
        "parent.updateTransform();"
        "Math.abs(child.worldTransform.tx - 110) < 0.01 && Math.abs(child.worldTransform.ty - 70) < 0.01;"
    ));
    /* worldAlpha propagation. */
    ASSERT(js_eval_bool(
        "var parent = new PIXI.Container();"
        "parent.alpha = 0.5;"
        "var child = new PIXI.Container();"
        "child.alpha = 0.8;"
        "parent.addChild(child);"
        "parent.updateTransform();"
        "Math.abs(child.worldAlpha - 0.4) < 0.01;"
    ));
    /* Deep hierarchy. */
    ASSERT(js_eval_bool(
        "var root = new PIXI.Container();"
        "root.x = 10;"
        "var mid = new PIXI.Container();"
        "mid.x = 20;"
        "var leaf = new PIXI.Container();"
        "leaf.x = 30;"
        "root.addChild(mid);"
        "mid.addChild(leaf);"
        "root.transform._localID++;"
        "mid.transform._localID++;"
        "leaf.transform._localID++;"
        "root.updateTransform();"
        "Math.abs(leaf.worldTransform.tx - 60) < 0.01;"
    ));
    js_teardown();
}

TEST(test_pixi_sprite)
{
    js_setup_with_shims();
    /* Sprite exists and extends Container. */
    ASSERT(js_eval_bool("typeof PIXI.Sprite === 'function';"));
    ASSERT(js_eval_bool(
        "var s = new PIXI.Sprite();"
        "s instanceof PIXI.Container && s instanceof PIXI.DisplayObject;"
    ));
    /* Default texture is EMPTY. */
    ASSERT(js_eval_bool(
        "var s = new PIXI.Sprite();"
        "s.texture === PIXI.Texture.EMPTY;"
    ));
    /* Sprite with texture. */
    ASSERT(js_eval_bool(
        "var bt = new PIXI.BaseTexture();"
        "bt.setSize(64, 48);"
        "var tex = new PIXI.Texture(bt);"
        "var s = new PIXI.Sprite(tex);"
        "s.texture === tex;"
    ));
    /* Anchor. */
    ASSERT(js_eval_bool(
        "var s = new PIXI.Sprite();"
        "s.anchor.x === 0 && s.anchor.y === 0;"
    ));
    ASSERT(js_eval_bool(
        "var s = new PIXI.Sprite();"
        "s.anchor.set(0.5, 0.5);"
        "s.anchor.x === 0.5 && s.anchor.y === 0.5;"
    ));
    /* Tint. */
    ASSERT(js_eval_bool(
        "var s = new PIXI.Sprite();"
        "s.tint === 0xFFFFFF;"
    ));
    ASSERT(js_eval_bool(
        "var s = new PIXI.Sprite();"
        "s.tint = 0xFF0000;"
        "s.tint === 0xFF0000;"
    ));
    /* isSprite flag. */
    ASSERT(js_eval_bool(
        "var s = new PIXI.Sprite();"
        "s.isSprite === true;"
    ));
    /* Width/height via texture. */
    ASSERT(js_eval_bool(
        "var bt = new PIXI.BaseTexture();"
        "bt.setSize(100, 50);"
        "var tex = new PIXI.Texture(bt);"
        "var s = new PIXI.Sprite(tex);"
        "s.width === 100 && s.height === 50;"
    ));
    /* Width/height setter changes scale. */
    ASSERT(js_eval_bool(
        "var bt = new PIXI.BaseTexture();"
        "bt.setSize(100, 50);"
        "var tex = new PIXI.Texture(bt);"
        "var s = new PIXI.Sprite(tex);"
        "s.width = 200;"
        "Math.abs(s.scale.x - 2) < 0.01;"
    ));
    /* Sprite.from static. */
    ASSERT(js_eval_bool(
        "typeof PIXI.Sprite.from === 'function';"
    ));
    js_teardown();
}

TEST(test_pixi_sprite_vertex_calculation)
{
    js_setup_with_shims();
    /* calculateVertices fills vertexData. */
    ASSERT(js_eval_bool(
        "var bt = new PIXI.BaseTexture();"
        "bt.setSize(100, 50);"
        "var tex = new PIXI.Texture(bt);"
        "var s = new PIXI.Sprite(tex);"
        "s.x = 10; s.y = 20;"
        "s.transform._localID++;"
        "s.updateTransform();"
        "s.calculateVertices();"
        "var vd = s.vertexData;"
        "Math.abs(vd[0] - 10) < 0.01 && Math.abs(vd[1] - 20) < 0.01 &&"
        "Math.abs(vd[4] - 110) < 0.01 && Math.abs(vd[5] - 70) < 0.01;"
    ));
    /* With anchor at center. */
    ASSERT(js_eval_bool(
        "var bt = new PIXI.BaseTexture();"
        "bt.setSize(100, 50);"
        "var tex = new PIXI.Texture(bt);"
        "var s = new PIXI.Sprite(tex);"
        "s.anchor.set(0.5, 0.5);"
        "s.x = 50; s.y = 25;"
        "s.transform._localID++;"
        "s.updateTransform();"
        "s.calculateVertices();"
        "var vd = s.vertexData;"
        "Math.abs(vd[0] - 0) < 0.01 && Math.abs(vd[1] - 0) < 0.01 &&"
        "Math.abs(vd[4] - 100) < 0.01 && Math.abs(vd[5] - 50) < 0.01;"
    ));
    js_teardown();
}

TEST(test_pixi_tiling_sprite)
{
    js_setup_with_shims();
    /* TilingSprite exists. */
    ASSERT(js_eval_bool("typeof PIXI.TilingSprite === 'function';"));
    ASSERT(js_eval_bool(
        "var ts = new PIXI.TilingSprite(PIXI.Texture.EMPTY, 200, 100);"
        "ts instanceof PIXI.Sprite;"
    ));
    /* Width/height. */
    ASSERT(js_eval_bool(
        "var ts = new PIXI.TilingSprite(PIXI.Texture.EMPTY, 200, 100);"
        "ts.width === 200 && ts.height === 100;"
    ));
    /* tilePosition and tileScale. */
    ASSERT(js_eval_bool(
        "var ts = new PIXI.TilingSprite(PIXI.Texture.EMPTY, 200, 100);"
        "ts.tilePosition.x === 0 && ts.tilePosition.y === 0;"
    ));
    ASSERT(js_eval_bool(
        "var ts = new PIXI.TilingSprite(PIXI.Texture.EMPTY, 200, 100);"
        "ts.tileScale.x === 1 && ts.tileScale.y === 1;"
    ));
    /* Width/height setters. */
    ASSERT(js_eval_bool(
        "var ts = new PIXI.TilingSprite(PIXI.Texture.EMPTY, 200, 100);"
        "ts.width = 400;"
        "ts.width === 400;"
    ));
    js_teardown();
}

TEST(test_pixi_graphics)
{
    js_setup_with_shims();
    /* Graphics exists. */
    ASSERT(js_eval_bool("typeof PIXI.Graphics === 'function';"));
    ASSERT(js_eval_bool(
        "var g = new PIXI.Graphics();"
        "g instanceof PIXI.Container;"
    ));
    /* beginFill / drawRect / endFill. */
    ASSERT(js_eval_bool(
        "var g = new PIXI.Graphics();"
        "g.beginFill(0xFF0000, 1);"
        "g.drawRect(10, 20, 100, 50);"
        "g.endFill();"
        "g._commands.length === 1 && g._commands[0].type === 'rect';"
    ));
    /* lineStyle. */
    ASSERT(js_eval_bool(
        "var g = new PIXI.Graphics();"
        "g.lineStyle(2, 0x00FF00, 0.8);"
        "g._lineStyle.width === 2 && g._lineStyle.color === 0x00FF00 &&"
        "Math.abs(g._lineStyle.alpha - 0.8) < 0.01;"
    ));
    /* moveTo / lineTo. */
    ASSERT(js_eval_bool(
        "var g = new PIXI.Graphics();"
        "g.lineStyle(1, 0xFFFFFF);"
        "g.moveTo(0, 0);"
        "g.lineTo(100, 100);"
        "g.lineTo(200, 0);"
        "g.currentPath.points.length === 6;"
    ));
    /* clear resets commands. */
    ASSERT(js_eval_bool(
        "var g = new PIXI.Graphics();"
        "g.beginFill(0xFF0000);"
        "g.drawRect(0, 0, 50, 50);"
        "g.endFill();"
        "g.clear();"
        "g._commands.length === 0;"
    ));
    /* drawCircle. */
    ASSERT(js_eval_bool(
        "var g = new PIXI.Graphics();"
        "g.beginFill(0x0000FF);"
        "g.drawCircle(50, 50, 25);"
        "g._commands.length === 1 && g._commands[0].type === 'circle';"
    ));
    /* drawRoundedRect. */
    ASSERT(js_eval_bool(
        "var g = new PIXI.Graphics();"
        "g.beginFill(0x00FF00);"
        "g.drawRoundedRect(0, 0, 100, 50, 10);"
        "g._commands.length === 1 && g._commands[0].type === 'roundedRect';"
    ));
    js_teardown();
}

TEST(test_pixi_scene_graph_render)
{
    js_setup_with_shims();
    /* _renderSceneGraph exists. */
    ASSERT(js_eval_bool("typeof PIXI._renderSceneGraph === 'function';"));
    /* Create a scene tree and render without crash. */
    ASSERT(js_eval_bool(
        "var root = new PIXI.Container();"
        "var child1 = new PIXI.Container();"
        "var child2 = new PIXI.Sprite();"
        "root.addChild(child1);"
        "root.addChild(child2);"
        "child1.addChild(new PIXI.Container());"
        "PIXI._renderSceneGraph(root);"
        "true;"
    ));
    /* Non-visible children are skipped in updateTransform. */
    ASSERT(js_eval_bool(
        "var root = new PIXI.Container();"
        "var vis = new PIXI.Container();"
        "vis.x = 10; vis.visible = true;"
        "var invis = new PIXI.Container();"
        "invis.x = 20; invis.visible = false;"
        "root.addChild(vis); root.addChild(invis);"
        "root.updateTransform();"
        "vis.worldTransform.tx === 10;"
    ));
    js_teardown();
}

TEST(test_pixi_container_hierarchy_events)
{
    js_setup_with_shims();
    /* added event fires on addChild. */
    ASSERT(js_eval_bool(
        "var p = new PIXI.Container();"
        "var c = new PIXI.Container();"
        "var addedParent = null;"
        "c.on('added', function(parent) { addedParent = parent; });"
        "p.addChild(c);"
        "addedParent === p;"
    ));
    /* removed event fires on removeChild. */
    ASSERT(js_eval_bool(
        "var p = new PIXI.Container();"
        "var c = new PIXI.Container();"
        "var removedParent = null;"
        "c.on('removed', function(parent) { removedParent = parent; });"
        "p.addChild(c);"
        "p.removeChild(c);"
        "removedParent === p;"
    ));
    js_teardown();
}

TEST(test_pixi_container_get_child)
{
    js_setup_with_shims();
    /* getChildAt. */
    ASSERT(js_eval_bool(
        "var p = new PIXI.Container();"
        "var c1 = new PIXI.Container();"
        "var c2 = new PIXI.Container();"
        "p.addChild(c1); p.addChild(c2);"
        "p.getChildAt(0) === c1 && p.getChildAt(1) === c2;"
    ));
    /* getChildByName. */
    ASSERT(js_eval_bool(
        "var p = new PIXI.Container();"
        "var c = new PIXI.Container();"
        "c.name = 'myChild';"
        "p.addChild(c);"
        "p.getChildByName('myChild') === c;"
    ));
    /* getChildIndex. */
    ASSERT(js_eval_bool(
        "var p = new PIXI.Container();"
        "var c1 = new PIXI.Container();"
        "var c2 = new PIXI.Container();"
        "p.addChild(c1); p.addChild(c2);"
        "p.getChildIndex(c1) === 0 && p.getChildIndex(c2) === 1;"
    ));
    js_teardown();
}

TEST(test_pixi_container_destroy)
{
    js_setup_with_shims();
    /* Destroy with children. */
    ASSERT(js_eval_bool(
        "var p = new PIXI.Container();"
        "var c1 = new PIXI.Container();"
        "var c2 = new PIXI.Container();"
        "p.addChild(c1); p.addChild(c2);"
        "p.destroy({ children: true });"
        "c1._destroyed === true && c2._destroyed === true;"
    ));
    /* Destroy without children. */
    ASSERT(js_eval_bool(
        "var p = new PIXI.Container();"
        "var c = new PIXI.Container();"
        "p.addChild(c);"
        "p.destroy(false);"
        "c._destroyed === false;"
    ));
    js_teardown();
}

TEST(test_pixi_bounds)
{
    js_setup_with_shims();
    /* Bounds exists. */
    ASSERT(js_eval_bool("typeof PIXI.Bounds === 'function';"));
    /* Empty bounds. */
    ASSERT(js_eval_bool(
        "var b = new PIXI.Bounds();"
        "b.isEmpty() === true;"
    ));
    /* addPoint. */
    ASSERT(js_eval_bool(
        "var b = new PIXI.Bounds();"
        "b.addPoint({x: 10, y: 20});"
        "b.addPoint({x: 50, y: 40});"
        "b.minX === 10 && b.minY === 20 && b.maxX === 50 && b.maxY === 40;"
    ));
    /* getRectangle. */
    ASSERT(js_eval_bool(
        "var b = new PIXI.Bounds();"
        "b.addPoint({x: 0, y: 0});"
        "b.addPoint({x: 100, y: 80});"
        "var r = b.getRectangle();"
        "r.x === 0 && r.y === 0 && r.width === 100 && r.height === 80;"
    ));
    js_teardown();
}

/* PIXI.Ticker tests */

TEST(test_pixi_ticker_exists)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool("typeof PIXI.Ticker === 'function';"));
    ASSERT(js_eval_bool(
        "var t = new PIXI.Ticker();"
        "t.started === false && t.autoStart === true;"
    ));
    js_teardown();
}

TEST(test_pixi_ticker_add_remove)
{
    js_setup_with_shims();
    /* add() registers a callback. */
    ASSERT(js_eval_bool(
        "var t = new PIXI.Ticker();"
        "var called = 0;"
        "var fn = function(dt) { called++; };"
        "t.add(fn);"
        "t._callbacks.length === 1;"
    ));
    /* Duplicate add is ignored. */
    ASSERT(js_eval_bool(
        "var t = new PIXI.Ticker();"
        "var fn = function() {};"
        "t.add(fn);"
        "t.add(fn);"
        "t._callbacks.length === 1;"
    ));
    /* remove() unregisters a callback. */
    ASSERT(js_eval_bool(
        "var t = new PIXI.Ticker();"
        "var fn = function() {};"
        "t.add(fn);"
        "t.remove(fn);"
        "t._callbacks.length === 0;"
    ));
    js_teardown();
}

TEST(test_pixi_ticker_update)
{
    js_setup_with_shims();
    /* update() calls registered functions with deltaTime. */
    ASSERT(js_eval_bool(
        "var t = new PIXI.Ticker();"
        "var received = -1;"
        "t.add(function(dt) { received = dt; });"
        "t.lastTime = 0;"
        "t.update(16.67);"
        "received > 0;"
    ));
    /* deltaMS is calculated. */
    ASSERT(js_eval_bool(
        "var t = new PIXI.Ticker();"
        "t.add(function() {});"
        "t.lastTime = 0;"
        "t.update(16.67);"
        "Math.abs(t.deltaMS - 16.67) < 0.01;"
    ));
    js_teardown();
}

TEST(test_pixi_ticker_addonce)
{
    js_setup_with_shims();
    /* addOnce fires only once. */
    ASSERT(js_eval_bool(
        "var t = new PIXI.Ticker();"
        "var count = 0;"
        "t.addOnce(function() { count++; });"
        "t.lastTime = 0;"
        "t.update(16.67);"
        "t.update(33.34);"
        "count === 1 && t._callbacks.length === 0;"
    ));
    js_teardown();
}

TEST(test_pixi_ticker_shared)
{
    js_setup_with_shims();
    /* Ticker.shared singleton. */
    ASSERT(js_eval_bool(
        "var t1 = PIXI.Ticker.shared;"
        "var t2 = PIXI.Ticker.shared;"
        "t1 === t2 && t1 instanceof PIXI.Ticker;"
    ));
    /* Ticker.system singleton. */
    ASSERT(js_eval_bool(
        "var t1 = PIXI.Ticker.system;"
        "var t2 = PIXI.Ticker.system;"
        "t1 === t2 && t1 instanceof PIXI.Ticker;"
    ));
    js_teardown();
}

/* PIXI.Renderer tests */

TEST(test_pixi_renderer_exists)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool("typeof PIXI.Renderer === 'function';"));
    ASSERT(js_eval_bool("typeof PIXI.WebGLRenderer === 'function';"));
    ASSERT(js_eval_bool("PIXI.Renderer === PIXI.WebGLRenderer;"));
    js_teardown();
}

TEST(test_pixi_renderer_create)
{
    js_setup_with_shims();
    /* Constructor with options. */
    ASSERT(js_eval_bool(
        "var r = new PIXI.Renderer({ width: 816, height: 624 });"
        "r.width === 816 && r.height === 624;"
    ));
    /* screen property. */
    ASSERT(js_eval_bool(
        "var r = new PIXI.Renderer({ width: 800, height: 600 });"
        "r.screen instanceof PIXI.Rectangle;"
    ));
    /* view property. */
    ASSERT(js_eval_bool(
        "var canvas = document.createElement('canvas');"
        "var r = new PIXI.Renderer({ view: canvas, width: 400, height: 300 });"
        "r.view === canvas;"
    ));
    /* type is WEBGL. */
    ASSERT(js_eval_bool(
        "var r = new PIXI.Renderer({ width: 100, height: 100 });"
        "r.type === 1;"
    ));
    js_teardown();
}

TEST(test_pixi_renderer_resize)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var r = new PIXI.Renderer({ width: 800, height: 600 });"
        "r.resize(1280, 720);"
        "r.width === 1280 && r.height === 720;"
    ));
    js_teardown();
}

TEST(test_pixi_renderer_render_no_crash)
{
    js_setup_with_shims();
    /* render() with a container should not crash (headless). */
    ASSERT(js_eval_bool(
        "var r = new PIXI.Renderer({ width: 800, height: 600 });"
        "var stage = new PIXI.Container();"
        "stage.addChild(new PIXI.Sprite());"
        "r.render(stage);"
        "true;"
    ));
    /* render() with null displayObject is a no-op. */
    ASSERT(js_eval_bool(
        "var r = new PIXI.Renderer({ width: 800, height: 600 });"
        "r.render(null);"
        "true;"
    ));
    js_teardown();
}

TEST(test_pixi_renderer_extract)
{
    js_setup_with_shims();
    /* extract plugin exists. */
    ASSERT(js_eval_bool(
        "var r = new PIXI.Renderer({ width: 100, height: 100 });"
        "typeof r.extract === 'object';"
    ));
    ASSERT(js_eval_bool(
        "var r = new PIXI.Renderer({ width: 100, height: 100 });"
        "typeof r.extract.canvas === 'function';"
    ));
    ASSERT(js_eval_bool(
        "var r = new PIXI.Renderer({ width: 100, height: 100 });"
        "typeof r.plugins.extract.canvas === 'function';"
    ));
    /* extract.canvas returns a canvas element. */
    ASSERT(js_eval_bool(
        "var r = new PIXI.Renderer({ width: 100, height: 100 });"
        "var c = r.extract.canvas();"
        "c && typeof c.getContext === 'function';"
    ));
    js_teardown();
}

TEST(test_pixi_renderer_gl_stub)
{
    js_setup_with_shims();
    /* gl property exists for effekseer compatibility. */
    ASSERT(js_eval_bool(
        "var r = new PIXI.Renderer({ width: 100, height: 100 });"
        "r.gl !== null && r.gl !== undefined;"
    ));
    js_teardown();
}

TEST(test_pixi_renderer_background_color)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var r = new PIXI.Renderer({ width: 100, height: 100, backgroundColor: 0xFF0000 });"
        "r.backgroundColor === 0xFF0000;"
    ));
    js_teardown();
}

/* PIXI.Application tests */

TEST(test_pixi_application_exists)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool("typeof PIXI.Application === 'function';"));
    js_teardown();
}

TEST(test_pixi_application_create)
{
    js_setup_with_shims();
    /* Create with autoStart: false to avoid rAF in headless mode. */
    ASSERT(js_eval_bool(
        "var app = new PIXI.Application({ autoStart: false, width: 816, height: 624 });"
        "app.renderer instanceof PIXI.Renderer;"
    ));
    /* Stage is a Container. */
    ASSERT(js_eval_bool(
        "var app = new PIXI.Application({ autoStart: false });"
        "app.stage instanceof PIXI.Container;"
    ));
    /* Ticker exists. */
    ASSERT(js_eval_bool(
        "var app = new PIXI.Application({ autoStart: false });"
        "app.ticker instanceof PIXI.Ticker;"
    ));
    /* view property. */
    ASSERT(js_eval_bool(
        "var canvas = document.createElement('canvas');"
        "var app = new PIXI.Application({ view: canvas, autoStart: false });"
        "app.view === canvas;"
    ));
    /* screen property. */
    ASSERT(js_eval_bool(
        "var app = new PIXI.Application({ autoStart: false, width: 400, height: 300 });"
        "app.screen.width === 400 && app.screen.height === 300;"
    ));
    js_teardown();
}

TEST(test_pixi_application_start_stop)
{
    js_setup_with_shims();
    /* start/stop control the ticker. */
    ASSERT(js_eval_bool(
        "var app = new PIXI.Application({ autoStart: false });"
        "app.ticker.started === false;"
    ));
    ASSERT(js_eval_bool(
        "var app = new PIXI.Application({ autoStart: false });"
        "app.start();"
        "app.ticker.started === true;"
    ));
    ASSERT(js_eval_bool(
        "var app = new PIXI.Application({ autoStart: false });"
        "app.start();"
        "app.stop();"
        "app.ticker.started === false;"
    ));
    js_teardown();
}

TEST(test_pixi_application_render)
{
    js_setup_with_shims();
    /* render() renders the stage. */
    ASSERT(js_eval_bool(
        "var app = new PIXI.Application({ autoStart: false });"
        "app.stage.addChild(new PIXI.Container());"
        "app.render();"
        "true;"
    ));
    js_teardown();
}

TEST(test_pixi_application_set_stage)
{
    js_setup_with_shims();
    /* stage is settable (used by Graphics.setStage). */
    ASSERT(js_eval_bool(
        "var app = new PIXI.Application({ autoStart: false });"
        "var newStage = new PIXI.Container();"
        "app.stage = newStage;"
        "app.stage === newStage;"
    ));
    js_teardown();
}

TEST(test_pixi_application_ticker_integration)
{
    js_setup_with_shims();
    /* Mirrors Graphics._createPixiApp: swap the default render tick for a
       custom handler. */
    ASSERT(js_eval_bool(
        "var app = new PIXI.Application({ autoStart: false });"
        "app.ticker.remove(app._renderBound, app);"
        "var tickCalled = false;"
        "var customTick = function(dt) { tickCalled = true; };"
        "app.ticker.add(customTick, null);"
        "app.ticker.lastTime = 0;"
        "app.ticker.update(16.67);"
        "tickCalled === true;"
    ));
    js_teardown();
}

TEST(test_pixi_application_destroy)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var app = new PIXI.Application({ autoStart: false });"
        "app.destroy();"
        "app.renderer === null && app.stage === null && app._ticker === null;"
    ));
    js_teardown();
}

/* Render-to-texture tests */

TEST(test_pixi_render_to_texture)
{
    js_setup_with_shims();
    /* renderer.render with RenderTexture should not crash. */
    ASSERT(js_eval_bool(
        "var r = new PIXI.Renderer({ width: 800, height: 600 });"
        "var rt = PIXI.RenderTexture.create({ width: 256, height: 128 });"
        "var container = new PIXI.Container();"
        "container.addChild(new PIXI.Sprite());"
        "r.render(container, rt);"
        "true;"
    ));
    js_teardown();
}

TEST(test_pixi_bitmap_snap_flow)
{
    js_setup_with_shims();
    /* Simulate the Bitmap.snap flow without actual Bitmap class. */
    ASSERT(js_eval_bool(
        "var renderer = new PIXI.Renderer({ width: 816, height: 624 });"
        "var stage = new PIXI.Container();"
        "var renderTexture = PIXI.RenderTexture.create(816, 624);"
        "renderer.render(stage, renderTexture);"
        "stage.worldTransform.identity();"
        "var canvas = renderer.extract.canvas(renderTexture);"
        "canvas && canvas.width === 816 && canvas.height === 624;"
    ));
    js_teardown();
}

TEST(test_pixi_graphics_texture_fill)
{
    js_setup_with_shims();
    /* beginTextureFill records the texture and the inverted matrix; a fill
       with a plain colour drops them again. */
    ASSERT(js_eval_bool(
        "var g = new PIXI.Graphics();"
        "var bt = new PIXI.BaseTexture(null, {}); bt.setSize(64, 32); bt.valid = true; bt._glTexture = 7;"
        "var tex = new PIXI.Texture(bt);"
        "g.beginTextureFill({ texture: tex, matrix: new PIXI.Matrix(1, 0, 0, 1, 10, 20) });"
        "g.drawRect(0, 0, 16, 16);"
        "g.endFill();"
        "g.beginFill(0xff0000); g.drawRect(20, 0, 8, 8); g.endFill();"
        "var a = g._commands[0].fill, b = g._commands[1].fill;"
        "a.texture === tex && a.matrix.tx === -10 && a.matrix.ty === -20 && a.visible === true &&"
        "b.texture === undefined && b.color === 0xff0000;"
    ));
    /* Rendering a textured fill goes through drawQuadVerticesUV with UVs
       derived from the vertex positions (here 16px of a 64x32 texture). */
    ASSERT(js_eval_bool(
        "var calls = [];"
        "__native_renderer.drawQuadVerticesUV = function() { calls.push(Array.prototype.slice.call(arguments)); };"
        "var plain = 0; __native_renderer.drawQuadVertices = function() { plain++; };"
        "__native_renderer._active = true;"
        "g.updateTransform(); g.render({ screen: { width: 800, height: 600 } });"
        "var c = calls[0];"
        "calls.length === 1 && plain === 1 && c[0] === 7 &&"
        "Math.abs(c[9] - (-10 / 64)) < 1e-6 && Math.abs(c[10] - (-20 / 32)) < 1e-6 &&"  /* TL through the inverse */
        "Math.abs(c[11] - 6 / 64) < 1e-6 && Math.abs(c[14] - (-4 / 32)) < 1e-6;"        /* TR u, BR v */
    ));
    js_teardown();
}

TEST(test_sprite_bitmap_deferred_release)
{
    /* tilemap_shim.js frees the canvas of an anonymous Bitmap a sprite lets
       go of, but only once no sprite shows it and a grace period passed:
       DTextPicture draws through a scratch window, nulls its contents and
       keeps the bitmap on the picture sprite. */
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var destroyed = [];"
        "globalThis.__native_canvas2d = { destroy: function(h) { destroyed.push(h); } };"
        "globalThis.Graphics = { frameCount: 0 };"
        "globalThis.Tilemap = function() {};"           /* the shim bails out without it */
        "Tilemap.Layer = function() {}; Tilemap.Renderer = function() {};"
        "function Sprite() { this._bitmap = null; }"
        "Object.defineProperty(Sprite.prototype, 'bitmap', {"
        "  get: function() { return this._bitmap; }, set: function(v) { this._bitmap = v; }, configurable: true });"
        "true;"
    ));
    ASSERT(js_engine_eval_file(_js, "src/shims/tilemap_shim.js"));
    ASSERT(js_eval_bool(
        "function bmp(h) { return { _canvas: { _context2d: { _handle: h } }, _url: '' }; }"
        "var shared = bmp(1), a = new Sprite(), b = new Sprite();"
        "a.bitmap = shared; b.bitmap = shared;"
        "a.bitmap = null;"                              /* still shown by b */
        "Graphics.frameCount = 500; new Sprite().bitmap = bmp(9);"
        "destroyed.length === 0 && shared._canvas._context2d._handle === 1;"
    ));
    ASSERT(js_eval_bool(
        "b.bitmap = null;"                              /* nobody shows it now */
        "Graphics.frameCount = 560; new Sprite().bitmap = bmp(9);"
        "destroyed.length === 0;"                       /* inside the grace period */
    ));
    ASSERT(js_eval_bool(
        "var c = new Sprite(); c.bitmap = shared;"      /* re-attached before release */
        "Graphics.frameCount = 700; new Sprite().bitmap = bmp(9);"
        "destroyed.length === 0 && shared._canvas._context2d._handle === 1;"
    ));
    ASSERT(js_eval_bool(
        "c.bitmap = null;"
        "Graphics.frameCount = 900; new Sprite().bitmap = bmp(9);"
        "destroyed.length === 1 && destroyed[0] === 1 && shared._canvas._context2d._handle === 0;"
    ));
    /* Loaded images (with a URL) are never touched. */
    ASSERT(js_eval_bool(
        "var img = bmp(2); img._url = 'img/pictures/x.png';"
        "var d = new Sprite(); d.bitmap = img; d.bitmap = null;"
        "Graphics.frameCount = 2000; new Sprite().bitmap = bmp(9);"
        "destroyed.length === 1 && img._canvas._context2d._handle === 2;"
    ));
    js_teardown();
}

TEST(test_pixi_sprite_mask)
{
    /* A sprite with a mask is drawn through it: the sprite and the mask are
       each rendered to a texture, combined with the mask shader, and the
       result composited; the mask itself never appears on its own. */
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "var calls = [];"
        "var fbo = 100;"
        "__native_renderer._active = true;"
        "__native_renderer.flush = function() {};"
        "__native_renderer.rebindBatch = function() {};"
        "__native_renderer.setBlendMode = function() {};"
        "__native_renderer.beginFrameTransparent = function() { calls.push('clear'); };"
        "__native_renderer.bindRenderTexture = function(f) { calls.push('bind:' + f); };"
        "__native_renderer.unbindRenderTexture = function() { calls.push('unbind'); };"
        "__native_renderer.createRenderTexture = function() { fbo++; return { fbo: fbo, texture: fbo * 10 }; };"
        "__native_renderer.deleteRenderTexture = function() {};"
        "__native_renderer.drawQuad = function(tex) { calls.push('quad:' + tex); };"
        "__native_renderer.drawQuadVertices = function() { calls.push('verts'); };"
        "globalThis.__native_filters = { maskShader: 7,"
        "  beginFilter: function(shader, tex) { calls.push('filter:' + shader + ':' + tex); },"
        "  setUniformTexture: function(shader, name, tex) { calls.push('maskTex:' + name + ':' + tex); },"
        "  drawQuad: function() { calls.push('filterQuad'); },"
        "  endFilter: function() {} };"
        "true;"
    ));
    ASSERT(js_eval_bool(
        "var bt = new PIXI.BaseTexture(null, {}); bt.setSize(32, 32); bt.valid = true; bt._glTexture = 5;"
        "var sprite = new PIXI.Sprite(new PIXI.Texture(bt));"
        "var maskBt = new PIXI.BaseTexture(null, {}); maskBt.setSize(32, 32); maskBt.valid = true; maskBt._glTexture = 6;"
        "var mask = new PIXI.Sprite(new PIXI.Texture(maskBt));"
        "sprite.mask = mask;"
        "mask.isMask === true && sprite.mask === mask;"
    ));
    ASSERT(js_eval_bool(
        "var renderer = { screen: { width: 64, height: 64 } };"
        "sprite.updateTransform(); sprite.render(renderer);"
        /* the mask shader ran against the two rendered textures, and the
           combined result was composited once */
        "calls.filter(function(c) { return c.indexOf('filter:7:') === 0; }).length === 1 &&"
        "calls.filter(function(c) { return c.indexOf('maskTex:u_mask:') === 0; }).length === 1 &&"
        "calls.filter(function(c) { return c.indexOf('quad:') === 0; }).length === 1;"
    ));
    ASSERT(js_eval_bool(
        /* A mask drawn on its own (as a child of the scene) is skipped. */
        "calls.length = 0;"
        "mask.updateTransform(); mask.render(renderer);"
        "calls.length === 0;"
    ));
    js_teardown();
}

TEST(test_pixi_readpixels_binding)
{
    js_setup_with_shims();
    /* readPixels binding exists. */
    ASSERT(js_eval_bool(
        "typeof __native_renderer.readPixels === 'function';"
    ));
    js_teardown();
}

int main(void)
{
    printf("PIXI shim tests:\n");
    printf("--- C-level sprite batch tests ---\n");
    RUN(test_sprite_batch_create_destroy);
    RUN(test_sprite_batch_null_safety);
    RUN(test_sprite_batch_default_max);

    printf("--- C-level renderer tests ---\n");
    RUN(test_renderer_create_destroy);
    RUN(test_renderer_resize);
    RUN(test_renderer_null_safety);

    printf("--- JS binding tests ---\n");
    RUN(test_js_renderer_binding_exists);
    RUN(test_js_renderer_init_get_size);
    RUN(test_js_renderer_resize);
    RUN(test_js_renderer_draw_calls);

    printf("--- JS PIXI shim tests ---\n");
    RUN(test_pixi_namespace_exists);
    RUN(test_pixi_scale_modes);
    RUN(test_pixi_blend_modes);
    RUN(test_pixi_settings);
    RUN(test_pixi_utils);
    RUN(test_pixi_rectangle);
    RUN(test_pixi_base_texture);
    RUN(test_pixi_texture);
    RUN(test_pixi_render_texture);
    RUN(test_pixi_matrix);
    RUN(test_pixi_transform);
    RUN(test_pixi_point);
    RUN(test_pixi_observable_point);
    RUN(test_pixi_event_emitter);

    printf("--- PIXI display object tests ---\n");
    RUN(test_pixi_display_object);
    RUN(test_pixi_container);
    RUN(test_pixi_container_add_remove);
    RUN(test_pixi_container_sort);
    RUN(test_pixi_container_transform_propagation);
    RUN(test_pixi_sprite);
    RUN(test_pixi_sprite_vertex_calculation);
    RUN(test_pixi_tiling_sprite);
    RUN(test_pixi_graphics);
    RUN(test_pixi_scene_graph_render);
    RUN(test_pixi_container_hierarchy_events);
    RUN(test_pixi_container_get_child);
    RUN(test_pixi_container_destroy);
    RUN(test_pixi_bounds);

    printf("--- PIXI Ticker tests ---\n");
    RUN(test_pixi_ticker_exists);
    RUN(test_pixi_ticker_add_remove);
    RUN(test_pixi_ticker_update);
    RUN(test_pixi_ticker_addonce);
    RUN(test_pixi_ticker_shared);

    printf("--- PIXI Renderer tests ---\n");
    RUN(test_pixi_renderer_exists);
    RUN(test_pixi_renderer_create);
    RUN(test_pixi_renderer_resize);
    RUN(test_pixi_renderer_render_no_crash);
    RUN(test_pixi_renderer_extract);
    RUN(test_pixi_renderer_gl_stub);
    RUN(test_pixi_renderer_background_color);

    printf("--- PIXI Application tests ---\n");
    RUN(test_pixi_application_exists);
    RUN(test_pixi_application_create);
    RUN(test_pixi_application_start_stop);
    RUN(test_pixi_application_render);
    RUN(test_pixi_application_set_stage);
    RUN(test_pixi_application_ticker_integration);
    RUN(test_pixi_application_destroy);

    printf("--- Render-to-texture tests ---\n");
    RUN(test_pixi_render_to_texture);
    RUN(test_pixi_bitmap_snap_flow);
    RUN(test_pixi_readpixels_binding);
    RUN(test_pixi_graphics_texture_fill);
    RUN(test_sprite_bitmap_deferred_release);
    RUN(test_pixi_sprite_mask);

    printf("\n%d/%d tests passed.\n", _tests_passed, _tests_run);
    return _tests_failed > 0 ? 1 : 0;
}
