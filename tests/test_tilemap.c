/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

/*
 * tests/test_tilemap.c — Tilemap rendering: C-level tilemap_draw_tiles,
 * the __native_tilemap JS binding, and the PIXI stubs Tilemap.Layer relies on.
 */

#include "rendering/tilemap.h"
#include "rendering/sprite_batch.h"
#include "rendering/renderer.h"
#include "engine/js_engine.h"
#include "bindings/bind_renderer.h"
#include "bindings/bind_tilemap.h"
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

/* C-level tilemap_draw_tiles tests (headless — no GL) */

TEST(test_tilemap_draw_tiles_null_safety)
{
    /* All NULL/zero combos should not crash. */
    tilemap_draw_tiles(NULL, 0, NULL, 0, 0, 0, NULL);
    tilemap_draw_tiles(NULL, 10, NULL, 0, 0, 0, NULL);

    SpriteBatch *sb = sprite_batch_create(256);
    ASSERT(sb != NULL);
    tilemap_draw_tiles(NULL, 0, NULL, 0, 0, 0, sb);
    tilemap_draw_tiles(NULL, 10, NULL, 0, 0, 0, sb);

    float elements[7] = {0, 0, 0, 10, 20, 48, 48};
    tilemap_draw_tiles(elements, 0, NULL, 0, 0, 0, sb);
    tilemap_draw_tiles(elements, -1, NULL, 0, 0, 0, sb);

    sprite_batch_destroy(sb);
}

TEST(test_tilemap_draw_tiles_normal)
{
    SpriteBatch *sb = sprite_batch_create(256);
    ASSERT(sb != NULL);

    TilesetInfo tilesets[1] = {{
        .gl_texture = 1,  /* fake texture ID */
        .width = 256,
        .height = 256
    }};

    /* Single tile: setNumber=0, sx=0, sy=0, dx=10, dy=20, w=48, h=48 */
    float elements[7] = {0, 0, 0, 10, 20, 48, 48};

    /* Headless: sprite_batch_draw only accumulates quads; verify no crash. */
    tilemap_draw_tiles(elements, 1, tilesets, 1, 0, 0, sb);

    sprite_batch_destroy(sb);
}

TEST(test_tilemap_draw_tiles_with_offset)
{
    SpriteBatch *sb = sprite_batch_create(256);
    ASSERT(sb != NULL);

    TilesetInfo tilesets[1] = {{
        .gl_texture = 1,
        .width = 256,
        .height = 256
    }};

    float elements[7] = {0, 0, 0, 10, 20, 48, 48};

    tilemap_draw_tiles(elements, 1, tilesets, 1, -100.0f, -50.0f, sb);

    sprite_batch_destroy(sb);
}

TEST(test_tilemap_draw_tiles_shadow)
{
    SpriteBatch *sb = sprite_batch_create(256);
    ASSERT(sb != NULL);

    /* Shadow tile: setNumber = -1 */
    float elements[7] = {-1, 0, 0, 10, 20, 24, 24};

    /* No tilesets needed for shadow — they use the white texture. */
    tilemap_draw_tiles(elements, 1, NULL, 0, 0, 0, sb);

    sprite_batch_destroy(sb);
}

TEST(test_tilemap_draw_tiles_invalid_set)
{
    SpriteBatch *sb = sprite_batch_create(256);
    ASSERT(sb != NULL);

    TilesetInfo tilesets[1] = {{
        .gl_texture = 1,
        .width = 256,
        .height = 256
    }};

    /* setNumber 5 — out of range for 1 tileset. Should skip gracefully. */
    float elements[7] = {5, 0, 0, 10, 20, 48, 48};
    tilemap_draw_tiles(elements, 1, tilesets, 1, 0, 0, sb);

    sprite_batch_destroy(sb);
}

TEST(test_tilemap_draw_tiles_missing_texture)
{
    SpriteBatch *sb = sprite_batch_create(256);
    ASSERT(sb != NULL);

    /* Tileset with gl_texture = 0 (not loaded yet). Should skip. */
    TilesetInfo tilesets[1] = {{
        .gl_texture = 0,
        .width = 256,
        .height = 256
    }};

    float elements[7] = {0, 0, 0, 10, 20, 48, 48};
    tilemap_draw_tiles(elements, 1, tilesets, 1, 0, 0, sb);

    sprite_batch_destroy(sb);
}

TEST(test_tilemap_draw_tiles_multiple)
{
    SpriteBatch *sb = sprite_batch_create(256);
    ASSERT(sb != NULL);

    TilesetInfo tilesets[2] = {
        {.gl_texture = 1, .width = 256, .height = 256},
        {.gl_texture = 2, .width = 512, .height = 512}
    };

    /* 4 tiles: 2 normal, 1 shadow, 1 from second tileset */
    float elements[7 * 4] = {
        0, 0, 0, 0, 0, 48, 48,      /* Tileset 0, top-left */
        0, 48, 0, 48, 0, 48, 48,    /* Tileset 0, next tile */
        -1, 0, 0, 0, 48, 24, 24,    /* Shadow */
        1, 0, 0, 96, 0, 48, 48      /* Tileset 1 */
    };

    tilemap_draw_tiles(elements, 4, tilesets, 2, 0, 0, sb);

    sprite_batch_destroy(sb);
}

TEST(test_tilemap_draw_tiles_zero_size)
{
    SpriteBatch *sb = sprite_batch_create(256);
    ASSERT(sb != NULL);

    TilesetInfo tilesets[1] = {{
        .gl_texture = 1,
        .width = 256,
        .height = 256
    }};

    /* Zero-size tile should be skipped. */
    float elements[7] = {0, 0, 0, 10, 20, 0, 0};
    tilemap_draw_tiles(elements, 1, tilesets, 1, 0, 0, sb);

    sprite_batch_destroy(sb);
}

/* JS binding tests */

static JSEngine *_js = NULL;

static void js_setup(void)
{
    _js = js_engine_init();
    bind_io_register(js_engine_get_context(_js));
    bind_renderer_register(js_engine_get_context(_js));
    bind_tilemap_register(js_engine_get_context(_js));
}

static void js_teardown(void)
{
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

TEST(test_js_tilemap_binding_exists)
{
    js_setup();
    ASSERT(js_eval_bool("typeof __native_tilemap === 'object';"));
    ASSERT(js_eval_bool("typeof __native_tilemap.drawTiles === 'function';"));
    js_teardown();
}

TEST(test_js_tilemap_draw_tiles_basic)
{
    js_setup();
    ASSERT(js_eval_bool(
        "__native_renderer.init(816, 624);"
        "var buf = new Float32Array([0, 0, 0, 10, 20, 48, 48]);"
        "var texIds = [0];"
        "var texW = [256];"
        "var texH = [256];"
        "__native_tilemap.drawTiles(buf.buffer, 1, texIds, texW, texH, 1, 0, 0);"
        "true;"
    ));
    js_teardown();
}

TEST(test_js_tilemap_draw_tiles_shadow)
{
    js_setup();
    ASSERT(js_eval_bool(
        "__native_renderer.init(816, 624);"
        "var buf = new Float32Array([-1, 0, 0, 10, 20, 24, 24]);"
        "__native_tilemap.drawTiles(buf.buffer, 1, [], [], [], 0, 0, 0);"
        "true;"
    ));
    js_teardown();
}

TEST(test_js_tilemap_draw_tiles_with_offset)
{
    js_setup();
    ASSERT(js_eval_bool(
        "__native_renderer.init(816, 624);"
        "var buf = new Float32Array([0, 0, 0, 10, 20, 48, 48]);"
        "__native_tilemap.drawTiles(buf.buffer, 1, [0], [256], [256], 1, -100.5, -50.5);"
        "true;"
    ));
    js_teardown();
}

TEST(test_js_tilemap_draw_tiles_empty)
{
    js_setup();
    ASSERT(js_eval_bool(
        "__native_renderer.init(816, 624);"
        "var buf = new Float32Array(0);"
        "__native_tilemap.drawTiles(buf.buffer, 0, [], [], [], 0, 0, 0);"
        "true;"
    ));
    js_teardown();
}

/* JS shim-level tests (PIXI stubs for Tilemap compatibility) */

static void js_setup_with_shims(void)
{
    _js = js_engine_init();
    bind_io_register(js_engine_get_context(_js));
    bind_renderer_register(js_engine_get_context(_js));
    bind_tilemap_register(js_engine_get_context(_js));

    /* Load shims in order. */
    js_engine_eval_file(_js, "src/shims/dom_shim.js");
    js_engine_eval_file(_js, "src/shims/canvas2d_shim.js");
    js_engine_eval_file(_js, "src/shims/navigator_shim.js");
    js_engine_eval_file(_js, "src/shims/xhr_shim.js");
    js_engine_eval_file(_js, "src/shims/pixi_shim.js");
}

TEST(test_pixi_state_stub)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool("typeof PIXI.State === 'function';"));
    ASSERT(js_eval_bool("typeof PIXI.State.for2d === 'function';"));
    ASSERT(js_eval_bool(
        "var s = PIXI.State.for2d();"
        "s.blend === true;"
    ));
    js_teardown();
}

TEST(test_pixi_buffer_stub)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool("typeof PIXI.Buffer === 'function';"));
    ASSERT(js_eval_bool(
        "var buf = new PIXI.Buffer(null, true, true);"
        "buf.static === true && buf.index === true;"
    ));
    ASSERT(js_eval_bool(
        "var buf = new PIXI.Buffer(null, true, false);"
        "buf.update(new Float32Array(10));"
        "buf.destroy();"
        "buf.data === null;"
    ));
    js_teardown();
}

TEST(test_pixi_geometry_stub)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool("typeof PIXI.Geometry === 'function';"));
    ASSERT(js_eval_bool(
        "var g = new PIXI.Geometry();"
        "var ib = new PIXI.Buffer(null, true, true);"
        "var vb = new PIXI.Buffer(null, true, false);"
        "var result = g.addIndex(ib).addAttribute('pos', vb, 2, false, 5126, 8, 0);"
        "result === g && g.indexBuffer === ib;"
    ));
    ASSERT(js_eval_bool(
        "var g = new PIXI.Geometry();"
        "g.destroy();"
        "g.indexBuffer === null;"
    ));
    js_teardown();
}

TEST(test_pixi_program_shader_stub)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool("typeof PIXI.Program === 'function';"));
    ASSERT(js_eval_bool("typeof PIXI.Program.from === 'function';"));
    ASSERT(js_eval_bool("typeof PIXI.Shader === 'function';"));
    ASSERT(js_eval_bool(
        "var prog = PIXI.Program.from('vert', 'frag');"
        "prog.vertexSrc === 'vert' && prog.fragmentSrc === 'frag';"
    ));
    ASSERT(js_eval_bool(
        "var shader = new PIXI.Shader(PIXI.Program.from('v', 'f'), {uTest: 1});"
        "shader.uniforms.uTest === 1;"
    ));
    js_teardown();
}

TEST(test_pixi_object_renderer_stub)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool("typeof PIXI.ObjectRenderer === 'function';"));
    ASSERT(js_eval_bool(
        "var or1 = new PIXI.ObjectRenderer({});"
        "or1.flush(); or1.start(); or1.stop();"
        "or1.destroy();"
        "or1.renderer === null;"
    ));
    js_teardown();
}

TEST(test_pixi_base_render_texture_stub)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool("typeof PIXI.BaseRenderTexture === 'function';"));
    ASSERT(js_eval_bool(
        "var brt = new PIXI.BaseRenderTexture();"
        "brt.resize(2048, 2048);"
        "brt.width === 2048 && brt.height === 2048 && brt.valid === true;"
    ));
    ASSERT(js_eval_bool(
        "var brt = new PIXI.BaseRenderTexture();"
        "brt.destroy();"
        "brt.valid === false;"
    ));
    js_teardown();
}

TEST(test_pixi_create_indices_for_quads)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool("typeof PIXI.utils.createIndicesForQuads === 'function';"));
    ASSERT(js_eval_bool(
        "var indices = PIXI.utils.createIndicesForQuads(2);"
        "indices.length === 12 && "
        "indices[0] === 0 && indices[1] === 1 && indices[2] === 2 && "
        "indices[3] === 0 && indices[4] === 2 && indices[5] === 3 && "
        "indices[6] === 4 && indices[7] === 5 && indices[8] === 6 && "
        "indices[9] === 4 && indices[10] === 6 && indices[11] === 7;"
    ));
    js_teardown();
}

TEST(test_pixi_renderer_register_plugin)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool("typeof PIXI.Renderer.registerPlugin === 'function';"));
    ASSERT(js_eval_bool(
        "function TestPlugin(renderer) { this.renderer = renderer; }"
        "PIXI.Renderer.registerPlugin('testPlugin', TestPlugin);"
        "true;"
    ));
    js_teardown();
}

TEST(test_pixi_renderer_subsystems)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "__native_renderer.init(816, 624);"
        "var app = new PIXI.Application({width: 816, height: 624});"
        "var r = app.renderer;"
        "typeof r.gl === 'object' && "
        "typeof r.batch === 'object' && "
        "typeof r.projection === 'object' && "
        "typeof r.shader === 'object' && "
        "typeof r.geometry === 'object' && "
        "typeof r.texture === 'object' && "
        "typeof r.state === 'object';"
    ));
    js_teardown();
}

TEST(test_pixi_renderer_gl_constants)
{
    js_setup_with_shims();
    ASSERT(js_eval_bool(
        "__native_renderer.init(816, 624);"
        "var app = new PIXI.Application({width: 816, height: 624});"
        "var gl = app.renderer.gl;"
        "gl.TRIANGLES === 4 && "
        "gl.RGBA === 6408 && "
        "gl.UNSIGNED_BYTE === 5121 && "
        "gl.TEXTURE_2D === 3553;"
    ));
    js_teardown();
}

/* Tilemap.Layer simulation test */

TEST(test_tilemap_layer_simulation)
{
    js_setup_with_shims();

    /* Mirror rmmz_core.js: a Tilemap.Layer-like object exercising addRect/clear/render. */
    ASSERT(js_eval_bool(
        "__native_renderer.init(816, 624);"
        "__native_renderer._active = true;"
        "\n"
        "/* Create a Layer-like object that mirrors Tilemap.Layer */\n"
        "var layer = Object.create(PIXI.Container.prototype);\n"
        "PIXI.Container.call(layer);\n"
        "layer._elements = [];\n"
        "layer._images = [];\n"
        "layer._tilesetTextures = [];\n"
        "layer._nativeBuffer = null;\n"
        "layer._needsTexturesUpdate = false;\n"
        "layer.worldTransform = new PIXI.Matrix();\n"
        "\n"
        "/* Add some tile rects */\n"
        "layer._elements.push([0, 0, 0, 0, 0, 48, 48]);\n"
        "layer._elements.push([0, 48, 0, 48, 0, 48, 48]);\n"
        "layer._elements.push([-1, 0, 0, 0, 48, 24, 24]);\n"
        "\n"
        "/* Set up tileset textures (fake - GL texture 0 means no real texture) */\n"
        "layer._tilesetTextures = [{glTexture: 0, width: 256, height: 256}];\n"
        "\n"
        "/* Test clear */\n"
        "layer._elements.length === 3;\n"
    ));

    ASSERT(js_eval_bool(
        "/* Verify the element data is correct */\n"
        "layer._elements[0][0] === 0 && layer._elements[0][3] === 0 && layer._elements[0][4] === 0;\n"
    ));

    ASSERT(js_eval_bool(
        "/* Test that packing into Float32Array works */\n"
        "var numElements = layer._elements.length;\n"
        "var buf = new Float32Array(numElements * 7);\n"
        "var idx = 0;\n"
        "for (var i = 0; i < numElements; i++) {\n"
        "    var item = layer._elements[i];\n"
        "    buf[idx++] = item[0];\n"
        "    buf[idx++] = item[1];\n"
        "    buf[idx++] = item[2];\n"
        "    buf[idx++] = item[3];\n"
        "    buf[idx++] = item[4];\n"
        "    buf[idx++] = item[5];\n"
        "    buf[idx++] = item[6];\n"
        "}\n"
        "buf[0] === 0 && buf[3] === 0 && buf[5] === 48 && buf[7] === 0 && buf[14] === -1;\n"
    ));

    ASSERT(js_eval_bool(
        "/* Test native draw call (shadow tiles still work with no tilesets) */\n"
        "var shadowBuf = new Float32Array([-1, 0, 0, 10, 20, 24, 24]);\n"
        "__native_tilemap.drawTiles(shadowBuf.buffer, 1, [], [], [], 0, 0, 0);\n"
        "__native_renderer._active = false;\n"
        "true;\n"
    ));

    js_teardown();
}

/* Tilemap.CombinedLayer addRect distribution test */

TEST(test_tilemap_combined_layer_pattern)
{
    js_setup_with_shims();

    ASSERT(js_eval_bool(
        "/* Simulate CombinedLayer with 2 child layers */\n"
        "var MAX_SIZE = 16000;\n"
        "var children = [\n"
        "    {_elements: [], addRect: function(s,sx,sy,dx,dy,w,h) { this._elements.push([s,sx,sy,dx,dy,w,h]); }, size: function() { return this._elements.length; }, clear: function() { this._elements.length = 0; }},\n"
        "    {_elements: [], addRect: function(s,sx,sy,dx,dy,w,h) { this._elements.push([s,sx,sy,dx,dy,w,h]); }, size: function() { return this._elements.length; }, clear: function() { this._elements.length = 0; }}\n"
        "];\n"
        "\n"
        "function addRect(setNumber, sx, sy, dx, dy, w, h) {\n"
        "    for (var i = 0; i < children.length; i++) {\n"
        "        if (children[i].size() < MAX_SIZE) {\n"
        "            children[i].addRect(setNumber, sx, sy, dx, dy, w, h);\n"
        "            break;\n"
        "        }\n"
        "    }\n"
        "}\n"
        "\n"
        "/* Add some tiles */\n"
        "addRect(0, 0, 0, 0, 0, 48, 48);\n"
        "addRect(1, 0, 0, 48, 0, 48, 48);\n"
        "addRect(-1, 0, 0, 0, 48, 24, 24);\n"
        "\n"
        "/* All should go to first child */\n"
        "children[0].size() === 3 && children[1].size() === 0;\n"
    ));

    js_teardown();
}

int main(void)
{
    printf("=== Tilemap rendering tests ===\n\n");

    printf("--- C-level tilemap_draw_tiles tests ---\n");
    RUN(test_tilemap_draw_tiles_null_safety);
    RUN(test_tilemap_draw_tiles_normal);
    RUN(test_tilemap_draw_tiles_with_offset);
    RUN(test_tilemap_draw_tiles_shadow);
    RUN(test_tilemap_draw_tiles_invalid_set);
    RUN(test_tilemap_draw_tiles_missing_texture);
    RUN(test_tilemap_draw_tiles_multiple);
    RUN(test_tilemap_draw_tiles_zero_size);

    printf("\n--- JS binding tests ---\n");
    RUN(test_js_tilemap_binding_exists);
    RUN(test_js_tilemap_draw_tiles_basic);
    RUN(test_js_tilemap_draw_tiles_shadow);
    RUN(test_js_tilemap_draw_tiles_with_offset);
    RUN(test_js_tilemap_draw_tiles_empty);

    printf("\n--- PIXI stub tests ---\n");
    RUN(test_pixi_state_stub);
    RUN(test_pixi_buffer_stub);
    RUN(test_pixi_geometry_stub);
    RUN(test_pixi_program_shader_stub);
    RUN(test_pixi_object_renderer_stub);
    RUN(test_pixi_base_render_texture_stub);
    RUN(test_pixi_create_indices_for_quads);
    RUN(test_pixi_renderer_register_plugin);
    RUN(test_pixi_renderer_subsystems);
    RUN(test_pixi_renderer_gl_constants);

    printf("\n--- Tilemap integration tests ---\n");
    RUN(test_tilemap_layer_simulation);
    RUN(test_tilemap_combined_layer_pattern);

    printf("\n=== Results: %d/%d passed ===\n", _tests_passed, _tests_run);
    return (_tests_passed == _tests_run) ? 0 : 1;
}
