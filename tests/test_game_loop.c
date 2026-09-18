/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

/*
 * Tests for script_loader.c (plugin parsing, load order, shim loading) and
 * the game loop's timer/rAF step cycle. Loop behaviour is exercised through
 * the JS engine so no SDL/OpenGL display is required.
 */

#include "engine/js_engine.h"
#include "engine/script_loader.h"
#include "io/file_io.h"
#include "bindings/bind_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_paths.h"

/* Minimal test framework */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                     \
    static void test_##name(void);                                     \
    static void run_##name(void) {                                     \
        tests_run++;                                                   \
        printf("  [%d] %s ... ", tests_run, #name);                    \
        test_##name();                                                 \
    }                                                                  \
    static void test_##name(void)

#define ASSERT(cond)                                                   \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("FAIL (line %d: %s)\n", __LINE__, #cond);           \
            tests_failed++;                                            \
            return;                                                    \
        }                                                              \
    } while (0)

#define PASS()                                                         \
    do {                                                               \
        printf("ok\n");                                                \
        tests_passed++;                                                \
    } while (0)

/* Helpers */

/* Scratch directory under the platform temp directory; filled in by main(). */
static char TEST_DIR[TEST_PATH_MAX];

static void setup_test_dir(void)
{
    file_io_mkdir(TEST_DIR);
}

static void cleanup_test_dir(void)
{
    test_rm_rf(TEST_DIR);
}

static void write_test_file(const char *relpath, const char *content)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", TEST_DIR, relpath);

    char dir[512];
    snprintf(dir, sizeof(dir), "%s", path);
    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        file_io_mkdir(dir);
    }

    file_io_write_text(path, content);
}

/* Create a fresh JS engine with I/O bindings registered. */
static JSEngine *create_test_engine(void)
{
    JSEngine *engine = js_engine_init();
    if (engine) {
        bind_io_register(js_engine_get_context(engine));
    }
    return engine;
}

/* Eval JS and return true if no exception. */
static bool eval_ok(JSEngine *engine, const char *script)
{
    return js_engine_eval(engine, script, "<test>");
}

/* Eval JS and return the string result. Caller must free. */
static char *eval_str(JSEngine *engine, const char *script)
{
    return js_engine_eval_string(engine, script, "<test>");
}

/* Plugin Parser Tests */

TEST(parse_plugins_basic)
{
    write_test_file("plugins_basic.js",
        "var $plugins = [\n"
        "  {\"name\":\"PluginA\",\"status\":true,\"description\":\"Test A\",\"parameters\":{}},\n"
        "  {\"name\":\"PluginB\",\"status\":false,\"description\":\"Test B\",\"parameters\":{}},\n"
        "  {\"name\":\"PluginC\",\"status\":true,\"description\":\"Test C\",\"parameters\":{}}\n"
        "];\n"
    );

    char path[512];
    snprintf(path, sizeof(path), "%s/plugins_basic.js", TEST_DIR);

    PluginList *list = script_loader_parse_plugins(path);
    ASSERT(list != NULL);
    ASSERT(list->count == 3);
    ASSERT(strcmp(list->entries[0].name, "PluginA") == 0);
    ASSERT(list->entries[0].status == true);
    ASSERT(strcmp(list->entries[1].name, "PluginB") == 0);
    ASSERT(list->entries[1].status == false);
    ASSERT(strcmp(list->entries[2].name, "PluginC") == 0);
    ASSERT(list->entries[2].status == true);

    script_loader_free_plugins(list);
    PASS();
}

TEST(parse_plugins_empty)
{
    write_test_file("plugins_empty.js", "var $plugins = [];\n");

    char path[512];
    snprintf(path, sizeof(path), "%s/plugins_empty.js", TEST_DIR);

    PluginList *list = script_loader_parse_plugins(path);
    ASSERT(list != NULL);
    ASSERT(list->count == 0);

    script_loader_free_plugins(list);
    PASS();
}

TEST(parse_plugins_with_parameters)
{
    write_test_file("plugins_params.js",
        "var $plugins = [\n"
        "  {\"name\":\"ComplexPlugin\",\"status\":true,"
        "\"description\":\"A complex plugin\","
        "\"parameters\":{\"param1\":\"value1\",\"param2\":\"42\",\"param3\":\"true\"}}\n"
        "];\n"
    );

    char path[512];
    snprintf(path, sizeof(path), "%s/plugins_params.js", TEST_DIR);

    PluginList *list = script_loader_parse_plugins(path);
    ASSERT(list != NULL);
    ASSERT(list->count == 1);
    ASSERT(strcmp(list->entries[0].name, "ComplexPlugin") == 0);
    ASSERT(list->entries[0].status == true);

    script_loader_free_plugins(list);
    PASS();
}

TEST(parse_plugins_nonexistent_file)
{
    PluginList *list = script_loader_parse_plugins(test_tmp_path("nonexistent_plugins.js"));
    ASSERT(list == NULL);
    PASS();
}

TEST(parse_plugins_null)
{
    PluginList *list = script_loader_parse_plugins(NULL);
    ASSERT(list == NULL);
    PASS();
}

TEST(parse_plugins_real_format)
{
    /* Mimics the plugins.js layout RPG Maker MZ generates */
    write_test_file("plugins_real.js",
        "// Generated by RPG Maker.\n"
        "// Do not edit this file directly.\n"
        "var $plugins =\n"
        "[\n"
        "{\"name\":\"OrangeGreenworks\",\"status\":true,\"description\":\"\",\"parameters\":{}},\n"
        "{\"name\":\"MUSH_Audio_Engine\",\"status\":true,\"description\":\"Audio engine.\",\"parameters\":{\"AudioVolume\":\"100\"}},\n"
        "{\"name\":\"DisabledPlugin\",\"status\":false,\"description\":\"Disabled.\",\"parameters\":{}}\n"
        "];\n"
    );

    char path[512];
    snprintf(path, sizeof(path), "%s/plugins_real.js", TEST_DIR);

    PluginList *list = script_loader_parse_plugins(path);
    ASSERT(list != NULL);
    ASSERT(list->count == 3);
    ASSERT(strcmp(list->entries[0].name, "OrangeGreenworks") == 0);
    ASSERT(list->entries[0].status == true);
    ASSERT(strcmp(list->entries[1].name, "MUSH_Audio_Engine") == 0);
    ASSERT(list->entries[1].status == true);
    ASSERT(strcmp(list->entries[2].name, "DisabledPlugin") == 0);
    ASSERT(list->entries[2].status == false);

    script_loader_free_plugins(list);
    PASS();
}

/* Load Order Tests */

TEST(plugin_names_with_subfolders)
{
    /* A plugin name is a path under js/plugins/, so a game may group its
       plugins in folders ("Pocket/Pocket_Mirror_BASE"). Only names that
       would escape that folder are rejected. */
    char game_dir[512];
    snprintf(game_dir, sizeof(game_dir), "%s/game_subfolder_plugins", TEST_DIR);
    file_io_mkdir(game_dir);

    char js_dir[512];
    snprintf(js_dir, sizeof(js_dir), "%s/js", game_dir);
    file_io_mkdir(js_dir);
    char plugins_dir[512];
    snprintf(plugins_dir, sizeof(plugins_dir), "%s/js/plugins", game_dir);
    file_io_mkdir(plugins_dir);
    char sub_dir[512];
    snprintf(sub_dir, sizeof(sub_dir), "%s/js/plugins/Sub", game_dir);
    file_io_mkdir(sub_dir);

    char path[512];
    snprintf(path, sizeof(path), "%s/js/plugins/Sub/Nested.js", game_dir);
    file_io_write_text(path, "var nested_loaded = true;\n");
    snprintf(path, sizeof(path), "%s/js/plugins/Flat.js", game_dir);
    file_io_write_text(path, "var flat_loaded = true;\n");
    snprintf(path, sizeof(path), "%s/js/plugins.js", game_dir);
    file_io_write_text(path,
        "var $plugins = [\n"
        "  {\"name\":\"Sub/Nested\",\"status\":true,\"description\":\"\",\"parameters\":{}},\n"
        "  {\"name\":\"Flat\",\"status\":true,\"description\":\"\",\"parameters\":{}},\n"
        "  {\"name\":\"../Escape\",\"status\":true,\"description\":\"\",\"parameters\":{}}\n"
        "];\n");

    size_t count = 0;
    char **order = script_loader_get_load_order("src/shims", game_dir, &count);
    ASSERT(order != NULL);

    bool has_nested = false, has_flat = false, has_escape = false;
    for (size_t i = 0; i < count; i++) {
        if (strstr(order[i], "Sub/Nested.js")) has_nested = true;
        if (strstr(order[i], "Flat.js")) has_flat = true;
        if (strstr(order[i], "Escape")) has_escape = true;
    }
    script_loader_free_path_list(order, count);

    ASSERT(has_nested);
    ASSERT(has_flat);
    ASSERT(!has_escape);
    PASS();
}



TEST(load_order_without_plugins)
{
    char game_dir[512];
    snprintf(game_dir, sizeof(game_dir), "%s/game_no_plugins", TEST_DIR);
    file_io_mkdir(game_dir);

    size_t count = 0;
    char **order = script_loader_get_load_order("src/shims", game_dir, &count);
    ASSERT(order != NULL);
    /* 11 shims + 1 lib + 6 core + 1 post-core shim + 1 main.js = 20 */
    ASSERT(count == 20);

    /* Shims first */
    ASSERT(strstr(order[0], "dom_shim.js") != NULL);
    ASSERT(strstr(order[1], "canvas2d_shim.js") != NULL);
    ASSERT(strstr(order[2], "font_shim.js") != NULL);
    ASSERT(strstr(order[3], "navigator_shim.js") != NULL);
    ASSERT(strstr(order[4], "xhr_shim.js") != NULL);
    ASSERT(strstr(order[5], "storage_shim.js") != NULL);
    ASSERT(strstr(order[6], "pixi_shim.js") != NULL);
    ASSERT(strstr(order[7], "webaudio_shim.js") != NULL);
    ASSERT(strstr(order[8], "effekseer_shim.js") != NULL);
    ASSERT(strstr(order[9], "dom_overlay.js") != NULL);
    ASSERT(strstr(order[10], "plugin_compat.js") != NULL);

    /* Then libs, core scripts, post-core shims, main.js */
    ASSERT(strstr(order[11], "pako.min.js") != NULL);

    ASSERT(strstr(order[12], "rmmz_core.js") != NULL);
    ASSERT(strstr(order[13], "rmmz_managers.js") != NULL);
    ASSERT(strstr(order[14], "rmmz_objects.js") != NULL);
    ASSERT(strstr(order[15], "rmmz_scenes.js") != NULL);
    ASSERT(strstr(order[16], "rmmz_sprites.js") != NULL);
    ASSERT(strstr(order[17], "rmmz_windows.js") != NULL);

    ASSERT(strstr(order[18], "tilemap_shim.js") != NULL);

    ASSERT(strstr(order[19], "main.js") != NULL);

    script_loader_free_path_list(order, count);
    PASS();
}

TEST(load_order_with_plugins)
{
    char game_dir[512];
    snprintf(game_dir, sizeof(game_dir), "%s/game_with_plugins", TEST_DIR);
    file_io_mkdir(game_dir);

    char plugins_dir[512];
    snprintf(plugins_dir, sizeof(plugins_dir), "%s/js", game_dir);
    file_io_mkdir(plugins_dir);

    char plugins_path[512];
    snprintf(plugins_path, sizeof(plugins_path), "%s/js/plugins.js", game_dir);
    file_io_write_text(plugins_path,
        "var $plugins = [\n"
        "  {\"name\":\"Alpha\",\"status\":true,\"description\":\"\",\"parameters\":{}},\n"
        "  {\"name\":\"Beta\",\"status\":false,\"description\":\"\",\"parameters\":{}},\n"
        "  {\"name\":\"Gamma\",\"status\":true,\"description\":\"\",\"parameters\":{}}\n"
        "];\n"
    );

    size_t count = 0;
    char **order = script_loader_get_load_order("src/shims", game_dir, &count);
    ASSERT(order != NULL);
    /* 11 shims + 1 lib + 6 core + 1 post-core shim + 2 enabled plugins + 1 main = 22 */
    ASSERT(count == 22);

    /* Post-core shims come after core, then enabled plugins, then main.js */
    ASSERT(strstr(order[18], "tilemap_shim.js") != NULL);

    ASSERT(strstr(order[19], "Alpha.js") != NULL);
    ASSERT(strstr(order[20], "Gamma.js") != NULL);  /* Beta is disabled */
    ASSERT(strstr(order[21], "main.js") != NULL);

    script_loader_free_path_list(order, count);
    PASS();
}

TEST(load_order_shims_first)
{
    char game_dir[512];
    snprintf(game_dir, sizeof(game_dir), "%s/game_order", TEST_DIR);
    file_io_mkdir(game_dir);

    size_t count = 0;
    char **order = script_loader_get_load_order("src/shims", game_dir, &count);
    ASSERT(order != NULL);
    ASSERT(count >= 4);

    ASSERT(strstr(order[0], "dom_shim") != NULL);
    ASSERT(strstr(order[1], "canvas2d") != NULL);
    ASSERT(strstr(order[2], "font_shim") != NULL);
    ASSERT(strstr(order[3], "navigator") != NULL);
    ASSERT(strstr(order[4], "xhr") != NULL);

    script_loader_free_path_list(order, count);
    PASS();
}

/* Shim Loading Tests */

TEST(load_shims_creates_globals)
{
    JSEngine *engine = create_test_engine();
    ASSERT(engine != NULL);

    bool ok = script_loader_load_shims(engine, "src/shims");
    ASSERT(ok);

    char *result;

    result = eval_str(engine, "typeof window");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "object") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "typeof document");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "object") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "typeof navigator");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "object") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "typeof XMLHttpRequest");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "function") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "typeof setTimeout");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "function") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "typeof requestAnimationFrame");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "function") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "typeof document.createElement");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "function") == 0);
    js_engine_free_string(result);

    js_engine_shutdown(engine);
    PASS();
}

TEST(load_shims_null_dir)
{
    JSEngine *engine = create_test_engine();
    ASSERT(engine != NULL);

    bool ok = script_loader_load_shims(engine, NULL);
    ASSERT(!ok);

    js_engine_shutdown(engine);
    PASS();
}

TEST(load_shims_nonexistent_dir)
{
    JSEngine *engine = create_test_engine();
    ASSERT(engine != NULL);

    bool ok = script_loader_load_shims(engine, test_tmp_path("nonexistent_shim_dir"));
    ASSERT(!ok);

    js_engine_shutdown(engine);
    PASS();
}

/* Timer Integration Tests (via JS engine, no SDL needed) */

TEST(timer_flush_in_loop)
{
    JSEngine *engine = create_test_engine();
    ASSERT(engine != NULL);

    bool ok = script_loader_load_shims(engine, "src/shims");
    ASSERT(ok);

    ok = eval_ok(engine, "var timerFired = false; setTimeout(function() { timerFired = true; }, 0);");
    ASSERT(ok);

    js_engine_execute_pending_jobs(engine);

    /* Simulate a loop step: flush timers */
    ok = eval_ok(engine, "__dom_flushTimers();");
    ASSERT(ok);

    char *result = eval_str(engine, "String(timerFired)");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "true") == 0);
    js_engine_free_string(result);

    js_engine_shutdown(engine);
    PASS();
}

TEST(raf_flush_in_loop)
{
    JSEngine *engine = create_test_engine();
    ASSERT(engine != NULL);

    bool ok = script_loader_load_shims(engine, "src/shims");
    ASSERT(ok);

    ok = eval_ok(engine,
        "var rafCalled = false;\n"
        "var rafTimestamp = -1;\n"
        "requestAnimationFrame(function(ts) { rafCalled = true; rafTimestamp = ts; });\n"
    );
    ASSERT(ok);

    /* Simulate a loop step: flush rAF with a timestamp */
    ok = eval_ok(engine, "__dom_flushAnimationFrames(16.67);");
    ASSERT(ok);

    char *result = eval_str(engine, "String(rafCalled)");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(rafTimestamp)");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "16.67") == 0);
    js_engine_free_string(result);

    js_engine_shutdown(engine);
    PASS();
}

TEST(raf_cleared_after_flush)
{
    JSEngine *engine = create_test_engine();
    ASSERT(engine != NULL);

    bool ok = script_loader_load_shims(engine, "src/shims");
    ASSERT(ok);

    /* rAF should fire once, not repeatedly */
    ok = eval_ok(engine,
        "var rafCount = 0;\n"
        "requestAnimationFrame(function() { rafCount++; });\n"
    );
    ASSERT(ok);

    eval_ok(engine, "__dom_flushAnimationFrames(16.67);");
    eval_ok(engine, "__dom_flushAnimationFrames(33.34);");

    char *result = eval_str(engine, "String(rafCount)");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "1") == 0);
    js_engine_free_string(result);

    js_engine_shutdown(engine);
    PASS();
}

TEST(raf_chained_callbacks)
{
    JSEngine *engine = create_test_engine();
    ASSERT(engine != NULL);

    bool ok = script_loader_load_shims(engine, "src/shims");
    ASSERT(ok);

    /* RPG Maker MZ chains rAF: each callback requests the next frame */
    ok = eval_ok(engine,
        "var frameCount = 0;\n"
        "function gameLoop(ts) {\n"
        "    frameCount++;\n"
        "    if (frameCount < 5) {\n"
        "        requestAnimationFrame(gameLoop);\n"
        "    }\n"
        "}\n"
        "requestAnimationFrame(gameLoop);\n"
    );
    ASSERT(ok);

    for (int i = 0; i < 5; i++) {
        char flush[64];
        snprintf(flush, sizeof(flush), "__dom_flushAnimationFrames(%f);", (i + 1) * 16.67);
        eval_ok(engine, flush);
    }

    char *result = eval_str(engine, "String(frameCount)");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "5") == 0);
    js_engine_free_string(result);

    js_engine_shutdown(engine);
    PASS();
}

TEST(cancel_animation_frame)
{
    JSEngine *engine = create_test_engine();
    ASSERT(engine != NULL);

    bool ok = script_loader_load_shims(engine, "src/shims");
    ASSERT(ok);

    ok = eval_ok(engine,
        "var cancelled = false;\n"
        "var id = requestAnimationFrame(function() { cancelled = true; });\n"
        "cancelAnimationFrame(id);\n"
    );
    ASSERT(ok);

    eval_ok(engine, "__dom_flushAnimationFrames(16.67);");

    char *result = eval_str(engine, "String(cancelled)");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "false") == 0);
    js_engine_free_string(result);

    js_engine_shutdown(engine);
    PASS();
}

TEST(timer_and_raf_combined)
{
    JSEngine *engine = create_test_engine();
    ASSERT(engine != NULL);

    bool ok = script_loader_load_shims(engine, "src/shims");
    ASSERT(ok);

    ok = eval_ok(engine,
        "var order = [];\n"
        "setTimeout(function() { order.push('timer'); }, 0);\n"
        "requestAnimationFrame(function() { order.push('raf'); });\n"
    );
    ASSERT(ok);

    /* Loop step order: timers, microtasks, rAF */
    eval_ok(engine, "__dom_flushTimers();");
    js_engine_execute_pending_jobs(engine);
    eval_ok(engine, "__dom_flushAnimationFrames(16.67);");

    char *result = eval_str(engine, "JSON.stringify(order)");
    ASSERT(result != NULL);
    /* Timer fires before rAF, matching browser behavior */
    ASSERT(strcmp(result, "[\"timer\",\"raf\"]") == 0);
    js_engine_free_string(result);

    js_engine_shutdown(engine);
    PASS();
}

TEST(setInterval_fires_each_flush)
{
    JSEngine *engine = create_test_engine();
    ASSERT(engine != NULL);

    bool ok = script_loader_load_shims(engine, "src/shims");
    ASSERT(ok);

    ok = eval_ok(engine,
        "var intervalCount = 0;\n"
        "var intervalId = setInterval(function() { intervalCount++; }, 0);\n"
    );
    ASSERT(ok);

    for (int i = 0; i < 3; i++) {
        eval_ok(engine, "__dom_flushTimers();");
    }

    char *result = eval_str(engine, "String(intervalCount)");
    ASSERT(result != NULL);
    int count = atoi(result);
    ASSERT(count >= 3);
    js_engine_free_string(result);

    eval_ok(engine, "clearInterval(intervalId);");
    eval_ok(engine, "__dom_flushTimers();");

    char *result2 = eval_str(engine, "String(intervalCount)");
    int count2 = atoi(result2);
    ASSERT(count2 == count);  /* No more increments after clear */
    js_engine_free_string(result2);

    js_engine_shutdown(engine);
    PASS();
}

/* Script Loading Integration Tests */

TEST(load_all_with_test_game)
{
    /* Minimal game directory with stub scripts for every load-order slot */
    char game_dir[512];
    snprintf(game_dir, sizeof(game_dir), "%s/test_game", TEST_DIR);
    file_io_mkdir(game_dir);

    char js_dir[512], libs_dir[512], plugins_dir[512];
    snprintf(js_dir, sizeof(js_dir), "%s/js", game_dir);
    snprintf(libs_dir, sizeof(libs_dir), "%s/js/libs", game_dir);
    snprintf(plugins_dir, sizeof(plugins_dir), "%s/js/plugins", game_dir);
    file_io_mkdir(js_dir);
    file_io_mkdir(libs_dir);
    file_io_mkdir(plugins_dir);

    char path[512];

    snprintf(path, sizeof(path), "%s/js/libs/pako.min.js", game_dir);
    file_io_write_text(path, "var pako = { inflate: function() { return []; }, deflate: function() { return []; } };\n");

    snprintf(path, sizeof(path), "%s/js/rmmz_core.js", game_dir);
    file_io_write_text(path, "var rmmz_core_loaded = true;\n");

    snprintf(path, sizeof(path), "%s/js/rmmz_managers.js", game_dir);
    file_io_write_text(path, "var rmmz_managers_loaded = true;\n");

    snprintf(path, sizeof(path), "%s/js/rmmz_objects.js", game_dir);
    file_io_write_text(path, "var rmmz_objects_loaded = true;\n");

    snprintf(path, sizeof(path), "%s/js/rmmz_scenes.js", game_dir);
    file_io_write_text(path, "var rmmz_scenes_loaded = true;\n");

    snprintf(path, sizeof(path), "%s/js/rmmz_sprites.js", game_dir);
    file_io_write_text(path, "var rmmz_sprites_loaded = true;\n");

    snprintf(path, sizeof(path), "%s/js/rmmz_windows.js", game_dir);
    file_io_write_text(path, "var rmmz_windows_loaded = true;\n");

    snprintf(path, sizeof(path), "%s/js/plugins.js", game_dir);
    file_io_write_text(path,
        "var $plugins = [\n"
        "  {\"name\":\"TestPlugin\",\"status\":true,\"description\":\"\",\"parameters\":{}}\n"
        "];\n"
    );

    snprintf(path, sizeof(path), "%s/js/plugins/TestPlugin.js", game_dir);
    file_io_write_text(path, "var test_plugin_loaded = true;\n");

    snprintf(path, sizeof(path), "%s/js/main.js", game_dir);
    file_io_write_text(path, "var main_loaded = true;\n");

    JSEngine *engine = create_test_engine();
    ASSERT(engine != NULL);

    bool ok = script_loader_load_all(engine, "src/shims", game_dir);
    ASSERT(ok);

    char *result;

    result = eval_str(engine, "String(typeof window !== 'undefined')");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(typeof pako !== 'undefined')");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(rmmz_core_loaded)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(rmmz_managers_loaded)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(rmmz_objects_loaded)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(rmmz_scenes_loaded)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(rmmz_sprites_loaded)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(rmmz_windows_loaded)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(test_plugin_loaded)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(main_loaded)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    js_engine_shutdown(engine);
    PASS();
}

TEST(load_all_missing_scripts_nonfatal)
{
    /* A game directory with no script files: missing scripts only warn. */
    char game_dir[512];
    snprintf(game_dir, sizeof(game_dir), "%s/game_empty", TEST_DIR);
    file_io_mkdir(game_dir);

    JSEngine *engine = create_test_engine();
    ASSERT(engine != NULL);

    bool ok = script_loader_load_all(engine, "src/shims", game_dir);
    ASSERT(ok);

    /* Shims should still be loaded */
    char *result = eval_str(engine, "typeof window");
    ASSERT(result && strcmp(result, "object") == 0);
    js_engine_free_string(result);

    js_engine_shutdown(engine);
    PASS();
}

TEST(load_scripts_order_verified_by_js)
{
    /* Each stub script appends its name to a shared array on load */
    char game_dir[512];
    snprintf(game_dir, sizeof(game_dir), "%s/game_order_test", TEST_DIR);
    file_io_mkdir(game_dir);

    char js_dir[512], libs_dir[512];
    snprintf(js_dir, sizeof(js_dir), "%s/js", game_dir);
    snprintf(libs_dir, sizeof(libs_dir), "%s/js/libs", game_dir);
    file_io_mkdir(js_dir);
    file_io_mkdir(libs_dir);

    char path[512];

    snprintf(path, sizeof(path), "%s/js/libs/pako.min.js", game_dir);
    file_io_write_text(path, "if(!globalThis._loadOrder) globalThis._loadOrder=[];\n_loadOrder.push('pako');\n");

    snprintf(path, sizeof(path), "%s/js/rmmz_core.js", game_dir);
    file_io_write_text(path, "_loadOrder.push('core');\n");

    snprintf(path, sizeof(path), "%s/js/rmmz_managers.js", game_dir);
    file_io_write_text(path, "_loadOrder.push('managers');\n");

    snprintf(path, sizeof(path), "%s/js/rmmz_objects.js", game_dir);
    file_io_write_text(path, "_loadOrder.push('objects');\n");

    snprintf(path, sizeof(path), "%s/js/rmmz_scenes.js", game_dir);
    file_io_write_text(path, "_loadOrder.push('scenes');\n");

    snprintf(path, sizeof(path), "%s/js/rmmz_sprites.js", game_dir);
    file_io_write_text(path, "_loadOrder.push('sprites');\n");

    snprintf(path, sizeof(path), "%s/js/rmmz_windows.js", game_dir);
    file_io_write_text(path, "_loadOrder.push('windows');\n");

    snprintf(path, sizeof(path), "%s/js/main.js", game_dir);
    file_io_write_text(path, "_loadOrder.push('main');\n");

    JSEngine *engine = create_test_engine();
    ASSERT(engine != NULL);

    bool ok = script_loader_load_all(engine, "src/shims", game_dir);
    ASSERT(ok);

    char *result = eval_str(engine, "JSON.stringify(_loadOrder)");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "[\"pako\",\"core\",\"managers\",\"objects\",\"scenes\",\"sprites\",\"windows\",\"main\"]") == 0);
    js_engine_free_string(result);

    js_engine_shutdown(engine);
    PASS();
}

TEST(disabled_plugins_not_loaded)
{
    char game_dir[512];
    snprintf(game_dir, sizeof(game_dir), "%s/game_disabled", TEST_DIR);
    file_io_mkdir(game_dir);

    char js_dir[512], plugins_dir[512];
    snprintf(js_dir, sizeof(js_dir), "%s/js", game_dir);
    snprintf(plugins_dir, sizeof(plugins_dir), "%s/js/plugins", game_dir);
    file_io_mkdir(js_dir);
    file_io_mkdir(plugins_dir);

    char path[512];

    snprintf(path, sizeof(path), "%s/js/plugins.js", game_dir);
    file_io_write_text(path,
        "var $plugins = [\n"
        "  {\"name\":\"EnabledPlugin\",\"status\":true,\"description\":\"\",\"parameters\":{}},\n"
        "  {\"name\":\"DisabledPlugin\",\"status\":false,\"description\":\"\",\"parameters\":{}}\n"
        "];\n"
    );

    snprintf(path, sizeof(path), "%s/js/plugins/EnabledPlugin.js", game_dir);
    file_io_write_text(path, "var enabled_plugin = true;\n");

    snprintf(path, sizeof(path), "%s/js/plugins/DisabledPlugin.js", game_dir);
    file_io_write_text(path, "var disabled_plugin = true;\n");

    JSEngine *engine = create_test_engine();
    ASSERT(engine != NULL);

    bool ok = script_loader_load_all(engine, "src/shims", game_dir);
    ASSERT(ok);

    char *result;
    result = eval_str(engine, "String(typeof enabled_plugin !== 'undefined')");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(typeof disabled_plugin !== 'undefined')");
    ASSERT(result && strcmp(result, "false") == 0);
    js_engine_free_string(result);

    js_engine_shutdown(engine);
    PASS();
}

/* Loop-like Integration Tests */

TEST(simulated_loop_with_shims)
{
    /* Mirror the game loop: load shims, then repeatedly flush timers,
       microtasks and rAF */
    JSEngine *engine = create_test_engine();
    ASSERT(engine != NULL);

    bool ok = script_loader_load_shims(engine, "src/shims");
    ASSERT(ok);

    ok = eval_ok(engine,
        "var updates = 0;\n"
        "var ticks = 0;\n"
        "\n"
        "/* A recurring interval simulating game logic */\n"
        "var logicInterval = setInterval(function() { ticks++; }, 0);\n"
        "\n"
        "/* A rAF-based rendering loop */\n"
        "function render(ts) {\n"
        "    updates++;\n"
        "    if (updates < 10) {\n"
        "        requestAnimationFrame(render);\n"
        "    }\n"
        "}\n"
        "requestAnimationFrame(render);\n"
    );
    ASSERT(ok);

    for (int i = 0; i < 10; i++) {
        eval_ok(engine, "__dom_flushTimers();");
        js_engine_execute_pending_jobs(engine);
        char flush[64];
        snprintf(flush, sizeof(flush), "__dom_flushAnimationFrames(%f);", (i + 1) * 16.67);
        eval_ok(engine, flush);
        js_engine_execute_pending_jobs(engine);
    }

    char *result;
    result = eval_str(engine, "String(updates)");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "10") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(ticks >= 10)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    eval_ok(engine, "clearInterval(logicInterval);");

    js_engine_shutdown(engine);
    PASS();
}

TEST(document_create_canvas_after_shims)
{
    JSEngine *engine = create_test_engine();
    ASSERT(engine != NULL);

    bool ok = script_loader_load_shims(engine, "src/shims");
    ASSERT(ok);

    /* Same canvas setup RPG Maker MZ performs at boot */
    ok = eval_ok(engine,
        "var canvas = document.createElement('canvas');\n"
        "canvas.width = 816;\n"
        "canvas.height = 624;\n"
        "canvas.id = 'gameCanvas';\n"
        "document.body.appendChild(canvas);\n"
        "var ctx = canvas.getContext('2d');\n"
    );
    ASSERT(ok);

    char *result;
    result = eval_str(engine, "String(canvas.width)");
    ASSERT(result && strcmp(result, "816") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(canvas.height)");
    ASSERT(result && strcmp(result, "624") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(ctx !== null)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(document.getElementById('gameCanvas') === canvas)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    js_engine_shutdown(engine);
    PASS();
}

TEST(window_event_dispatch_after_shims)
{
    JSEngine *engine = create_test_engine();
    ASSERT(engine != NULL);

    bool ok = script_loader_load_shims(engine, "src/shims");
    ASSERT(ok);

    ok = eval_ok(engine,
        "var keysDown = [];\n"
        "document.addEventListener('keydown', function(e) {\n"
        "    keysDown.push(e.key);\n"
        "});\n"
        "document.dispatchEvent(Object.assign(new Event('keydown'), { key: 'ArrowUp' }));\n"
        "document.dispatchEvent(Object.assign(new Event('keydown'), { key: 'Enter' }));\n"
    );
    ASSERT(ok);

    char *result = eval_str(engine, "JSON.stringify(keysDown)");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "[\"ArrowUp\",\"Enter\"]") == 0);
    js_engine_free_string(result);

    js_engine_shutdown(engine);
    PASS();
}

TEST(control_shim_replies)
{
    /* control_shim.js turns "<id> eval <json>" commands into
       "@@ctl <id> ok|err <json>" replies, awaiting promises first. */
    JSEngine *engine = create_test_engine();
    ASSERT(engine != NULL);

    bool ok = eval_ok(engine,
        "var sent = [];\n"
        "globalThis.__native_control_send = function(line) { sent.push(line); };\n");
    ASSERT(ok);
    ok = js_engine_eval_file(engine, "src/shims/control_shim.js");
    ASSERT(ok);

    /* Synchronous value */
    ok = eval_ok(engine, "__control_eval(1, JSON.stringify('1 + 1'))");
    ASSERT(ok);
    char *result = eval_str(engine, "sent[0]");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "@@ctl 1 ok 2") == 0);
    js_engine_free_string(result);

    /* undefined becomes null so every reply carries JSON */
    ok = eval_ok(engine, "__control_eval(2, JSON.stringify('void 0'))");
    ASSERT(ok);
    result = eval_str(engine, "sent[1]");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "@@ctl 2 ok null") == 0);
    js_engine_free_string(result);

    /* Thrown errors are reported, not propagated */
    ok = eval_ok(engine, "__control_eval(3, JSON.stringify('missingFn()'))");
    ASSERT(ok);
    result = eval_str(engine, "sent[2].indexOf('@@ctl 3 err ') === 0 && "
                              "sent[2].indexOf('missingFn') > 0 ? 'yes' : sent[2]");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "yes") == 0);
    js_engine_free_string(result);

    /* Promises reply once settled */
    ok = eval_ok(engine, "__control_eval(4, JSON.stringify('Promise.resolve({a: [1, 2]})'))");
    ASSERT(ok);
    result = eval_str(engine, "String(sent.length)");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "3") == 0);
    js_engine_free_string(result);
    js_engine_execute_pending_jobs(engine);
    result = eval_str(engine, "sent[3]");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "@@ctl 4 ok {\"a\":[1,2]}") == 0);
    js_engine_free_string(result);

    /* Multi-line source survives the JSON envelope */
    ok = eval_ok(engine, "__control_eval(5, JSON.stringify('var x = 2;\\nx * 21'))");
    ASSERT(ok);
    result = eval_str(engine, "sent[4]");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "@@ctl 5 ok 42") == 0);
    js_engine_free_string(result);

    js_engine_shutdown(engine);
    PASS();
}

TEST(pixi_errors_captured_gracefully)
{
    /* A core script that guards its PIXI usage must load cleanly and leave
       the engine usable for subsequent scripts. */
    char game_dir[512];
    snprintf(game_dir, sizeof(game_dir), "%s/game_pixi_test", TEST_DIR);
    file_io_mkdir(game_dir);

    char js_dir[512];
    snprintf(js_dir, sizeof(js_dir), "%s/js", game_dir);
    file_io_mkdir(js_dir);

    char path[512];

    snprintf(path, sizeof(path), "%s/js/rmmz_core.js", game_dir);
    file_io_write_text(path,
        "/* Pre-PIXI code that should work */\n"
        "var Graphics = {};\n"
        "Graphics._width = 816;\n"
        "Graphics._height = 624;\n"
        "\n"
        "/* This would reference PIXI which causes an error */\n"
        "try {\n"
        "    if (typeof PIXI !== 'undefined') {\n"
        "        Graphics._app = new PIXI.Application();\n"
        "    }\n"
        "} catch(e) {\n"
        "    /* Graceful handling */\n"
        "}\n"
        "\n"
        "var core_loaded_ok = true;\n"
    );

    snprintf(path, sizeof(path), "%s/js/rmmz_managers.js", game_dir);
    file_io_write_text(path,
        "var DataManager = {};\n"
        "DataManager._loaded = true;\n"
        "var managers_loaded_ok = true;\n"
    );

    JSEngine *engine = create_test_engine();
    ASSERT(engine != NULL);

    bool ok = script_loader_load_all(engine, "src/shims", game_dir);
    ASSERT(ok);

    char *result;
    result = eval_str(engine, "String(core_loaded_ok)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(Graphics._width)");
    ASSERT(result && strcmp(result, "816") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(managers_loaded_ok)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    /* Engine still usable afterwards */
    result = eval_str(engine, "String(1 + 1)");
    ASSERT(result && strcmp(result, "2") == 0);
    js_engine_free_string(result);

    js_engine_shutdown(engine);
    PASS();
}

/* Main */

int main(void)
{
    snprintf(TEST_DIR, sizeof(TEST_DIR), "%s/rmmz_test_gameloop", test_tmp_root());

    printf("=== Game Loop & Script Loader Tests ===\n");

    setup_test_dir();

    printf("\n-- Plugin Parser --\n");
    run_parse_plugins_basic();
    run_parse_plugins_empty();
    run_parse_plugins_with_parameters();
    run_parse_plugins_nonexistent_file();
    run_parse_plugins_null();
    run_parse_plugins_real_format();

    printf("\n-- Load Order --\n");
    run_plugin_names_with_subfolders();
    run_load_order_without_plugins();
    run_load_order_with_plugins();
    run_load_order_shims_first();

    printf("\n-- Shim Loading --\n");
    run_load_shims_creates_globals();
    run_load_shims_null_dir();
    run_load_shims_nonexistent_dir();

    printf("\n-- Timer/rAF Integration --\n");
    run_timer_flush_in_loop();
    run_raf_flush_in_loop();
    run_raf_cleared_after_flush();
    run_raf_chained_callbacks();
    run_cancel_animation_frame();
    run_timer_and_raf_combined();
    run_setInterval_fires_each_flush();

    printf("\n-- Script Loading Integration --\n");
    run_load_all_with_test_game();
    run_load_all_missing_scripts_nonfatal();
    run_load_scripts_order_verified_by_js();
    run_disabled_plugins_not_loaded();

    printf("\n-- Loop Integration --\n");
    run_simulated_loop_with_shims();
    run_document_create_canvas_after_shims();
    run_window_event_dispatch_after_shims();
    run_control_shim_replies();
    run_pixi_errors_captured_gracefully();

    cleanup_test_dir();

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
