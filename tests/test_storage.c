/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

#include "bindings/bind_io.h"
#include "io/file_io.h"
#include "engine/js_engine.h"

#include <quickjs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_paths.h"

/* Minimal test framework */

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    tests_run++; \
    printf("  [%d] %s ... ", tests_run, #name); \
    name(); \
    if (tests_failed >= tests_run - tests_passed) { \
        /* test already marked as failed */ \
    } else { \
        printf("PASS\n"); \
        tests_passed++; \
    } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    assertion failed: %s\n    at %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* Test setup / teardown */

static const char *TEST_SAVE_DIR = "tests/fixtures/test_save";

static void setup_test_env(void)
{
    test_rm_rf(TEST_SAVE_DIR);
    test_mkdir_p(TEST_SAVE_DIR);
}

static JSEngine *s_js = NULL;

static void js_setup(void)
{
    setup_test_env();

    s_js = js_engine_init();
    bind_io_register(js_engine_get_context(s_js));
    file_io_set_game_root(TEST_SAVE_DIR);

    /* DOM shim must precede the storage shim (provides window, document). */
    js_engine_eval_file(s_js, "src/shims/dom_shim.js");
    js_engine_execute_pending_jobs(s_js);

    js_engine_eval_file(s_js, "src/shims/storage_shim.js");
    js_engine_execute_pending_jobs(s_js);
}

static void js_teardown(void)
{
    js_engine_shutdown(s_js);
    s_js = NULL;
    test_rm_rf(TEST_SAVE_DIR);
}

static int js_check(const char *script)
{
    char *result = js_engine_eval_string(s_js, script, "<test>");
    if (!result) return 0;
    int ok = (strcmp(result, "true") == 0);
    if (!ok) {
        printf("(got: %s) ", result);
    }
    js_engine_free_string(result);
    return ok;
}

/* Tests: localStorage */

TEST(localstorage_exists)
{
    js_setup();
    ASSERT(js_check("typeof localStorage === 'object'"));
    ASSERT(js_check("typeof localStorage.getItem === 'function'"));
    ASSERT(js_check("typeof localStorage.setItem === 'function'"));
    ASSERT(js_check("typeof localStorage.removeItem === 'function'"));
    ASSERT(js_check("typeof localStorage.clear === 'function'"));
    ASSERT(js_check("typeof localStorage.key === 'function'"));
    js_teardown();
}

TEST(localstorage_set_get)
{
    js_setup();
    ASSERT(js_check(
        "localStorage.setItem('testKey', 'testValue');"
        "localStorage.getItem('testKey') === 'testValue'"
    ));
    js_teardown();
}

TEST(localstorage_get_nonexistent)
{
    js_setup();
    ASSERT(js_check("localStorage.getItem('nonexistent') === null"));
    js_teardown();
}

TEST(localstorage_remove)
{
    js_setup();
    ASSERT(js_check(
        "localStorage.setItem('removeMe', 'value');"
        "localStorage.removeItem('removeMe');"
        "localStorage.getItem('removeMe') === null"
    ));
    js_teardown();
}

TEST(localstorage_clear)
{
    js_setup();
    ASSERT(js_check(
        "localStorage.setItem('a', '1');"
        "localStorage.setItem('b', '2');"
        "localStorage.clear();"
        "localStorage.getItem('a') === null && localStorage.getItem('b') === null"
    ));
    js_teardown();
}

TEST(localstorage_length_and_key)
{
    js_setup();
    ASSERT(js_check(
        "localStorage.clear();"
        "localStorage.setItem('x', '10');"
        "localStorage.setItem('y', '20');"
        "localStorage.length === 2"
    ));
    ASSERT(js_check(
        "var k = localStorage.key(0);"
        "k === 'x' || k === 'y'"
    ));
    ASSERT(js_check("localStorage.key(99) === null"));
    js_teardown();
}

TEST(localstorage_persistence)
{
    js_setup();
    ASSERT(js_check(
        "localStorage.setItem('persist', 'hello');"
        "true"
    ));

    /* The value must be flushed to disk immediately. */
    char save_path[4096];
    snprintf(save_path, sizeof(save_path), "%s/save/localStorage.json", TEST_SAVE_DIR);
    ASSERT(file_io_exists(save_path));

    size_t size = 0;
    char *content = file_io_read_text(save_path, &size);
    ASSERT(content != NULL);
    ASSERT(strstr(content, "persist") != NULL);
    ASSERT(strstr(content, "hello") != NULL);
    free(content);

    js_teardown();
}

TEST(localstorage_coerces_to_string)
{
    js_setup();
    ASSERT(js_check(
        "localStorage.setItem('num', 42);"
        "localStorage.getItem('num') === '42'"
    ));
    js_teardown();
}

/* Tests: localforage */

TEST(localforage_exists)
{
    js_setup();
    ASSERT(js_check("typeof localforage === 'object'"));
    ASSERT(js_check("typeof localforage.setItem === 'function'"));
    ASSERT(js_check("typeof localforage.getItem === 'function'"));
    ASSERT(js_check("typeof localforage.removeItem === 'function'"));
    ASSERT(js_check("typeof localforage.keys === 'function'"));
    js_teardown();
}

TEST(localforage_set_get)
{
    js_setup();
    js_engine_eval(s_js,
        "var forageResult = null;"
        "localforage.setItem('key1', 'val1')"
        "    .then(function() { return localforage.getItem('key1'); })"
        "    .then(function(v) { forageResult = v; });",
        "<test>");
    js_engine_execute_pending_jobs(s_js);

    ASSERT(js_check("forageResult === 'val1'"));
    js_teardown();
}

TEST(localforage_get_nonexistent)
{
    js_setup();
    js_engine_eval(s_js,
        "var forageNull = 'notset';"
        "localforage.getItem('nope')"
        "    .then(function(v) { forageNull = v; });",
        "<test>");
    js_engine_execute_pending_jobs(s_js);

    ASSERT(js_check("forageNull === null"));
    js_teardown();
}

TEST(localforage_remove)
{
    js_setup();
    js_engine_eval(s_js,
        "var forageRemoved = 'not_done';"
        "localforage.setItem('rmkey', 'rmval')"
        "    .then(function() { return localforage.removeItem('rmkey'); })"
        "    .then(function() { return localforage.getItem('rmkey'); })"
        "    .then(function(v) { forageRemoved = v; });",
        "<test>");
    js_engine_execute_pending_jobs(s_js);

    ASSERT(js_check("forageRemoved === null"));
    js_teardown();
}

TEST(localforage_keys)
{
    js_setup();
    js_engine_eval(s_js,
        "var forageKeys = null;"
        "Promise.all(["
        "    localforage.setItem('k1', 'v1'),"
        "    localforage.setItem('k2', 'v2')"
        "]).then(function() { return localforage.keys(); })"
        "  .then(function(keys) { forageKeys = keys; });",
        "<test>");
    js_engine_execute_pending_jobs(s_js);

    ASSERT(js_check("Array.isArray(forageKeys)"));
    ASSERT(js_check("forageKeys.length === 2"));
    js_teardown();
}

TEST(localforage_keys_returns_original_keys)
{
    js_setup();
    /* keys() must return the original keys, not the sanitized filenames. */
    js_engine_eval(s_js,
        "var forageOrigKeys = null;"
        "localforage.setItem('rmmzsave.123456.file0', 'data1')"
        "    .then(function() { return localforage.setItem('key with spaces', 'data2'); })"
        "    .then(function() { return localforage.keys(); })"
        "    .then(function(keys) { forageOrigKeys = keys; });",
        "<test>");
    js_engine_execute_pending_jobs(s_js);

    ASSERT(js_check("Array.isArray(forageOrigKeys)"));
    ASSERT(js_check("forageOrigKeys.length === 2"));
    ASSERT(js_check("forageOrigKeys.indexOf('rmmzsave.123456.file0') >= 0"));
    ASSERT(js_check("forageOrigKeys.indexOf('key with spaces') >= 0"));
    js_teardown();
}

TEST(localforage_no_key_collision)
{
    js_setup();
    /* "a_b" and "a/b" must map to distinct filenames (injective key encoding). */
    js_engine_eval(s_js,
        "var collisionResult = {};"
        "localforage.setItem('a_b', 'underscore')"
        "    .then(function() { return localforage.setItem('a/b', 'slash'); })"
        "    .then(function() { return localforage.getItem('a_b'); })"
        "    .then(function(v) { collisionResult.u = v; })"
        "    .then(function() { return localforage.getItem('a/b'); })"
        "    .then(function(v) { collisionResult.s = v; })"
        "    .then(function() { return localforage.keys(); })"
        "    .then(function(k) { collisionResult.keys = k; });",
        "<test>");
    js_engine_execute_pending_jobs(s_js);

    ASSERT(js_check("collisionResult.u === 'underscore'"));
    ASSERT(js_check("collisionResult.s === 'slash'"));
    ASSERT(js_check("collisionResult.keys.length === 2"));
    ASSERT(js_check("collisionResult.keys.indexOf('a_b') >= 0"));
    ASSERT(js_check("collisionResult.keys.indexOf('a/b') >= 0"));
    js_teardown();
}

TEST(localforage_no_unicode_key_collision)
{
    js_setup();
    /* U+1004 and U+0100 followed by "4" must not collide; fixed-width hex
       encoding keeps them distinct ("_1004" vs "_01004"). */
    js_engine_eval(s_js,
        "var ucResult = {};"
        "localforage.setItem('\\u1004', 'single')"
        "    .then(function() { return localforage.setItem('\\u0100' + '4', 'multi'); })"
        "    .then(function() { return localforage.getItem('\\u1004'); })"
        "    .then(function(v) { ucResult.single = v; })"
        "    .then(function() { return localforage.getItem('\\u0100' + '4'); })"
        "    .then(function(v) { ucResult.multi = v; })"
        "    .then(function() { return localforage.keys(); })"
        "    .then(function(k) { ucResult.keys = k; });",
        "<test>");
    js_engine_execute_pending_jobs(s_js);

    ASSERT(js_check("ucResult.single === 'single'"));
    ASSERT(js_check("ucResult.multi === 'multi'"));
    ASSERT(js_check("ucResult.keys.length === 2"));
    js_teardown();
}

TEST(localforage_keys_after_remove)
{
    js_setup();
    /* removeItem must also drop the key from the key map. */
    js_engine_eval(s_js,
        "var forageKeysAfterRm = null;"
        "localforage.setItem('key.to.keep', 'v1')"
        "    .then(function() { return localforage.setItem('key.to.remove', 'v2'); })"
        "    .then(function() { return localforage.removeItem('key.to.remove'); })"
        "    .then(function() { return localforage.keys(); })"
        "    .then(function(keys) { forageKeysAfterRm = keys; });",
        "<test>");
    js_engine_execute_pending_jobs(s_js);

    ASSERT(js_check("Array.isArray(forageKeysAfterRm)"));
    ASSERT(js_check("forageKeysAfterRm.length === 1"));
    ASSERT(js_check("forageKeysAfterRm[0] === 'key.to.keep'"));
    js_teardown();
}

/* Tests: require("fs") */

TEST(require_fs_exists)
{
    js_setup();
    ASSERT(js_check("typeof require === 'function'"));
    ASSERT(js_check(
        "var fs = require('fs');"
        "typeof fs === 'object'"
    ));
    ASSERT(js_check("typeof fs.existsSync === 'function'"));
    ASSERT(js_check("typeof fs.mkdirSync === 'function'"));
    ASSERT(js_check("typeof fs.readFileSync === 'function'"));
    ASSERT(js_check("typeof fs.writeFileSync === 'function'"));
    ASSERT(js_check("typeof fs.unlinkSync === 'function'"));
    ASSERT(js_check("typeof fs.readdirSync === 'function'"));
    ASSERT(js_check("typeof fs.renameSync === 'function'"));
    js_teardown();
}

TEST(require_fs_write_read_roundtrip)
{
    js_setup();
    ASSERT(js_check(
        "var fs = require('fs');"
        "fs.mkdirSync('save');"
        "fs.writeFileSync('save/test.txt', 'hello world');"
        "fs.readFileSync('save/test.txt') === 'hello world'"
    ));
    js_teardown();
}

TEST(require_fs_write_read_with_encoding)
{
    js_setup();
    ASSERT(js_check(
        "var fs = require('fs');"
        "fs.mkdirSync('save');"
        "fs.writeFileSync('save/test_enc.txt', 'encoded data');"
        "fs.readFileSync('save/test_enc.txt', { encoding: 'utf8' }) === 'encoded data'"
    ));
    js_teardown();
}

TEST(require_fs_exists_sync)
{
    js_setup();
    ASSERT(js_check(
        "var fs = require('fs');"
        "fs.mkdirSync('save');"
        "fs.writeFileSync('save/exists_test.txt', 'data');"
        "fs.existsSync('save/exists_test.txt') === true"
    ));
    ASSERT(js_check("fs.existsSync('save/nonexistent.txt') === false"));
    js_teardown();
}

TEST(require_fs_unlink)
{
    js_setup();
    ASSERT(js_check(
        "var fs = require('fs');"
        "fs.mkdirSync('save');"
        "fs.writeFileSync('save/delete_me.txt', 'data');"
        "fs.unlinkSync('save/delete_me.txt');"
        "fs.existsSync('save/delete_me.txt') === false"
    ));
    js_teardown();
}

TEST(require_fs_readdir)
{
    js_setup();
    ASSERT(js_check(
        "var fs = require('fs');"
        "fs.mkdirSync('save');"
        "fs.writeFileSync('save/a.txt', 'a');"
        "fs.writeFileSync('save/b.txt', 'b');"
        "var files = fs.readdirSync('save');"
        "Array.isArray(files) && files.length >= 2"
    ));
    js_teardown();
}

TEST(require_fs_rename)
{
    js_setup();
    ASSERT(js_check(
        "var fs = require('fs');"
        "fs.mkdirSync('save');"
        "fs.writeFileSync('save/old_name.txt', 'content');"
        "fs.renameSync('save/old_name.txt', 'save/new_name.txt');"
        "fs.existsSync('save/old_name.txt') === false && "
        "fs.readFileSync('save/new_name.txt') === 'content'"
    ));
    js_teardown();
}

/* Tests: require("path") */

TEST(require_path_exists)
{
    js_setup();
    ASSERT(js_check(
        "var path = require('path');"
        "typeof path === 'object'"
    ));
    ASSERT(js_check("typeof path.join === 'function'"));
    ASSERT(js_check("typeof path.dirname === 'function'"));
    ASSERT(js_check("typeof path.basename === 'function'"));
    ASSERT(js_check("typeof path.resolve === 'function'"));
    js_teardown();
}

TEST(require_path_join)
{
    js_setup();
    ASSERT(js_check(
        "var path = require('path');"
        "path.join('a', 'b', 'c') === 'a/b/c'"
    ));
    ASSERT(js_check("path.join('a/', '/b') === 'a/b'"));
    /* Trailing separators survive, as in Node. StorageManager relies on it:
       path.join(base, "save/") + "config.rmmzsave" must land inside save/. */
    ASSERT(js_check("path.join('a', 'b/') === 'a/b/'"));
    ASSERT(js_check("path.join('/game', 'save/') + 'config.rmmzsave' === '/game/save/config.rmmzsave'"));
    js_teardown();
}

TEST(require_path_dirname)
{
    js_setup();
    ASSERT(js_check(
        "var path = require('path');"
        "path.dirname('/foo/bar/baz.js') === '/foo/bar'"
    ));
    ASSERT(js_check("path.dirname('foo.js') === '.'"));
    ASSERT(js_check("path.dirname('/foo') === '/'"));
    js_teardown();
}

TEST(require_path_basename)
{
    js_setup();
    ASSERT(js_check(
        "var path = require('path');"
        "path.basename('/foo/bar/baz.js') === 'baz.js'"
    ));
    ASSERT(js_check("path.basename('/foo/bar/baz.js', '.js') === 'baz'"));
    ASSERT(js_check("path.basename('file.txt') === 'file.txt'"));
    js_teardown();
}

TEST(require_path_resolve)
{
    js_setup();
    ASSERT(js_check(
        "var path = require('path');"
        "path.resolve('/foo', 'bar') === '/foo/bar'"
    ));
    ASSERT(js_check("path.resolve('/foo', '/bar') === '/bar'"));
    js_teardown();
}

/* Tests: require() for unknown modules */

TEST(require_unknown_module)
{
    js_setup();
    /* Unknown modules should return an empty object, not throw. */
    ASSERT(js_check(
        "var mod = require('nw.gui');"
        "typeof mod === 'object'"
    ));
    js_teardown();
}

/* Tests: process object */

TEST(process_exists)
{
    js_setup();
    ASSERT(js_check("typeof process === 'object'"));
    ASSERT(js_check("typeof process.mainModule === 'object'"));
    ASSERT(js_check("typeof process.mainModule.filename === 'string'"));
    js_teardown();
}

TEST(process_mainmodule_filename)
{
    js_setup();
    ASSERT(js_check("process.mainModule.filename.indexOf('index.html') >= 0"));
    js_teardown();
}

/* Tests: Utils.isNwjs() pattern */

TEST(utils_is_nwjs_pattern)
{
    js_setup();
    /* The exact check RPG Maker MZ's Utils.isNwjs() performs. */
    ASSERT(js_check(
        "typeof require === 'function' && typeof process === 'object'"
    ));
    js_teardown();
}

/* Tests: StorageManager simulation (end-to-end) */

TEST(storage_manager_file_directory_path)
{
    js_setup();
    /* Mirrors StorageManager.fileDirectoryPath(). */
    ASSERT(js_check(
        "var path = require('path');"
        "var base = path.dirname(process.mainModule.filename);"
        "var dirPath = path.join(base, 'save/');"
        "typeof dirPath === 'string' && dirPath.indexOf('save') >= 0"
    ));
    js_teardown();
}

TEST(storage_manager_save_load_roundtrip)
{
    js_setup();
    /* Mirrors the StorageManager save/load flow: mkdir, write, read back. */
    ASSERT(js_check(
        "var fs = require('fs');"
        "var path = require('path');"
        "var base = path.dirname(process.mainModule.filename);"
        "var dirPath = path.join(base, 'save/');"
        "if (!fs.existsSync(dirPath)) fs.mkdirSync(dirPath);"
        "var filePath = dirPath + 'file0.rmmzsave';"
        "var saveData = 'compressed-save-data-here';"
        "fs.writeFileSync(filePath, saveData);"
        "var loaded = fs.readFileSync(filePath, { encoding: 'utf8' });"
        "loaded === saveData"
    ));
    js_teardown();
}

TEST(storage_manager_backup_rename_pattern)
{
    js_setup();
    /* Mirrors the backup/rename pattern in StorageManager.saveToLocalFile. */
    ASSERT(js_check(
        "var fs = require('fs');"
        "fs.mkdirSync('save');"
        "var filePath = 'save/test.rmmzsave';"
        "var backupPath = filePath + '_';"
        ""
        "/* Create initial save */"
        "fs.writeFileSync(filePath, 'save-v1');"
        ""
        "/* Simulate backup-rename-write pattern */"
        "if (fs.existsSync(backupPath)) fs.unlinkSync(backupPath);"
        "if (fs.existsSync(filePath)) fs.renameSync(filePath, backupPath);"
        "fs.writeFileSync(filePath, 'save-v2');"
        "if (fs.existsSync(backupPath)) fs.unlinkSync(backupPath);"
        ""
        "fs.readFileSync(filePath) === 'save-v2' && !fs.existsSync(backupPath)"
    ));
    js_teardown();
}

/* Tests: pako verification */

TEST(pako_works_in_quickjs)
{
    js_setup();

    /* pako.min.js is taken from the test game (it ships with every RMMZ game). */
    if (!test_game_dir()) {
        printf("SKIP (RMMZ_TEST_GAME_DIR not set) ");
        js_teardown();
        return;
    }
    char pako_path[TEST_PATH_MAX];
    snprintf(pako_path, sizeof(pako_path), "%s/js/libs/pako.min.js", test_game_dir());
    file_io_set_game_root(test_game_dir());
    bool loaded = js_engine_eval_file(s_js, pako_path);
    js_engine_execute_pending_jobs(s_js);

    if (!loaded) {
        printf("SKIP (pako.min.js not found) ");
        js_teardown();
        return;
    }

    ASSERT(js_check(
        "typeof pako === 'object' && typeof pako.deflate === 'function'"
    ));

    ASSERT(js_check(
        "var input = 'Hello, RPG Maker MZ save system!';"
        "var compressed = pako.deflate(input, { to: 'string', level: 1 });"
        "compressed.length > 0"
    ));

    ASSERT(js_check(
        "var decompressed = pako.inflate(compressed, { to: 'string' });"
        "decompressed === input"
    ));

    js_teardown();
}

/* Tests: nw shim */

TEST(nw_shim_exists)
{
    js_setup();
    ASSERT(js_check("typeof nw === 'object'"));
    ASSERT(js_check("typeof nw.Window === 'object'"));
    ASSERT(js_check("typeof nw.Window.get === 'function'"));
    ASSERT(js_check(
        "var win = nw.Window.get();"
        "typeof win === 'object' && typeof win.on === 'function'"
    ));
    js_teardown();
}

/* Main */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    printf("=== Storage Shim Tests ===\n");

    printf("\n--- localStorage tests ---\n");
    RUN(localstorage_exists);
    RUN(localstorage_set_get);
    RUN(localstorage_get_nonexistent);
    RUN(localstorage_remove);
    RUN(localstorage_clear);
    RUN(localstorage_length_and_key);
    RUN(localstorage_persistence);
    RUN(localstorage_coerces_to_string);

    printf("\n--- localforage tests ---\n");
    RUN(localforage_exists);
    RUN(localforage_set_get);
    RUN(localforage_get_nonexistent);
    RUN(localforage_remove);
    RUN(localforage_keys);
    RUN(localforage_keys_returns_original_keys);
    RUN(localforage_no_key_collision);
    RUN(localforage_no_unicode_key_collision);
    RUN(localforage_keys_after_remove);

    printf("\n--- require('fs') tests ---\n");
    RUN(require_fs_exists);
    RUN(require_fs_write_read_roundtrip);
    RUN(require_fs_write_read_with_encoding);
    RUN(require_fs_exists_sync);
    RUN(require_fs_unlink);
    RUN(require_fs_readdir);
    RUN(require_fs_rename);

    printf("\n--- require('path') tests ---\n");
    RUN(require_path_exists);
    RUN(require_path_join);
    RUN(require_path_dirname);
    RUN(require_path_basename);
    RUN(require_path_resolve);

    printf("\n--- require() unknown modules ---\n");
    RUN(require_unknown_module);

    printf("\n--- process object tests ---\n");
    RUN(process_exists);
    RUN(process_mainmodule_filename);

    printf("\n--- RPG Maker MZ compatibility tests ---\n");
    RUN(utils_is_nwjs_pattern);
    RUN(storage_manager_file_directory_path);
    RUN(storage_manager_save_load_roundtrip);
    RUN(storage_manager_backup_rename_pattern);

    printf("\n--- pako integration tests ---\n");
    RUN(pako_works_in_quickjs);

    printf("\n--- nw shim tests ---\n");
    RUN(nw_shim_exists);

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
