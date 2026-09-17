/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#include "io/file_io.h"
#include "bindings/bind_io.h"
#include "engine/js_engine.h"

#include <quickjs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_paths.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  TEST %s ... ", #name); \
    } while (0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("PASS\n"); \
    } while (0)

#define FAIL(msg) \
    do { \
        printf("FAIL: %s\n", msg); \
    } while (0)

/* Test directory setup/teardown */

/* Scratch paths under the platform temp directory; filled in by main(). */
static char test_dir[TEST_PATH_MAX];
static char test_subdir[TEST_PATH_MAX];
static char test_nested[TEST_PATH_MAX];
static char test_file[TEST_PATH_MAX];
static char test_bin[TEST_PATH_MAX];

/* Substitute "@DIR@" in a JS snippet with the scratch directory. */
static const char *jsf(const char *src)
{
    static char bufs[4][4096];
    static int next = 0;
    char *out = bufs[next];
    next = (next + 1) % 4;
    size_t o = 0;
    for (const char *c = src; *c && o + 1 < sizeof(bufs[0]);) {
        if (strncmp(c, "@DIR@", 5) == 0) {
            size_t n = strlen(test_dir);
            if (o + n + 1 >= sizeof(bufs[0])) break;
            memcpy(out + o, test_dir, n);
            o += n;
            c += 5;
        } else {
            out[o++] = *c++;
        }
    }
    out[o] = '\0';
    return out;
}

/* Expected absolute form of a POSIX-style path. On Windows the game root is
   resolved with _fullpath(), which maps "/game" to "<current drive>:/game". */
static const char *abs_expect(const char *posix_path)
{
    static char bufs[4][TEST_PATH_MAX];
    static int next = 0;
    char *buf = bufs[next];
    next = (next + 1) % 4;
#ifdef _WIN32
    char drive[3] = "";
    char *full = _fullpath(NULL, "/", 0);
    if (full) {
        drive[0] = full[0];
        drive[1] = ':';
        drive[2] = '\0';
        free(full);
    }
    snprintf(buf, TEST_PATH_MAX, "%s%s", drive, posix_path);
#else
    snprintf(buf, TEST_PATH_MAX, "%s", posix_path);
#endif
    return buf;
}

/* Recursively remove the scratch directory tree. */
static void cleanup_test_dir(void)
{
    test_rm_rf(test_dir);
}

static void setup_test_dir(void)
{
    cleanup_test_dir();
    file_io_mkdir(test_dir);
}

/* C-level file_io tests */

static void test_path_resolution(void)
{
    char buf[4096];

    /* Absolute path within game root is allowed; outside is rejected. */
    TEST(resolve_absolute_path);
    file_io_set_game_root("/some/root");
    if (file_io_resolve_path(abs_expect("/some/root/data/Actors.json"), buf, sizeof(buf)) &&
        strcmp(buf, abs_expect("/some/root/data/Actors.json")) == 0 &&
        !file_io_resolve_path(abs_expect("/abs/path.json"), buf, sizeof(buf))) {
        PASS();
    } else {
        FAIL(buf);
    }

    /* Relative path prepends game root. */
    TEST(resolve_relative_path);
    file_io_set_game_root("/game");
    if (file_io_resolve_path("data/Actors.json", buf, sizeof(buf)) &&
        strcmp(buf, abs_expect("/game/data/Actors.json")) == 0) {
        PASS();
    } else {
        FAIL(buf);
    }

    /* No game root — relative path used as-is. */
    TEST(resolve_no_root);
    file_io_set_game_root(NULL);
    if (file_io_resolve_path("data/Actors.json", buf, sizeof(buf)) &&
        strcmp(buf, "data/Actors.json") == 0) {
        PASS();
    } else {
        FAIL(buf);
    }

    /* Trailing slash on game root is stripped. */
    TEST(resolve_trailing_slash);
    file_io_set_game_root("/game/");
    if (file_io_resolve_path("data/x.json", buf, sizeof(buf)) &&
        strcmp(buf, abs_expect("/game/data/x.json")) == 0) {
        PASS();
    } else {
        FAIL(buf);
    }

    TEST(resolve_null_args);
    if (!file_io_resolve_path(NULL, buf, sizeof(buf)) &&
        !file_io_resolve_path("x", NULL, 0)) {
        PASS();
    } else {
        FAIL("should return false for NULL args");
    }

    /* ".." that stays inside the root is normalized, not rejected. */
    TEST(resolve_dotdot_inside_root);
    file_io_set_game_root("/game");
    if (file_io_resolve_path("data/../dataEx/Groups.json", buf, sizeof(buf)) &&
        strcmp(buf, abs_expect("/game/dataEx/Groups.json")) == 0 &&
        file_io_resolve_path("img/./pictures/a/../b.png", buf, sizeof(buf)) &&
        strcmp(buf, abs_expect("/game/img/pictures/b.png")) == 0) {
        PASS();
    } else {
        FAIL(buf);
    }

    /* ".." that would climb above the root is still rejected. */
    TEST(resolve_dotdot_escapes_root);
    file_io_set_game_root("/game");
    if (!file_io_resolve_path("../secret.json", buf, sizeof(buf)) &&
        !file_io_resolve_path("data/../../secret.json", buf, sizeof(buf)) &&
        !file_io_resolve_path(abs_expect("/game/../secret.json"), buf, sizeof(buf))) {
        PASS();
    } else {
        FAIL(buf);
    }

    /* Percent-encoded URLs from Utils.encodeURI() map to the real file name. */
    TEST(resolve_percent_encoded);
    file_io_set_game_root("/game");
    if (file_io_resolve_path("img/titles1/title%20screen%201.png", buf, sizeof(buf)) &&
        strcmp(buf, abs_expect("/game/img/titles1/title screen 1.png")) == 0 &&
        file_io_resolve_path("img/100%25.png", buf, sizeof(buf)) &&
        strcmp(buf, abs_expect("/game/img/100%.png")) == 0 &&
        file_io_resolve_path("img/odd%zz.png", buf, sizeof(buf)) &&
        strcmp(buf, abs_expect("/game/img/odd%zz.png")) == 0) {
        PASS();
    } else {
        FAIL(buf);
    }

    TEST(game_root_get_set);
    file_io_set_game_root("/test/root");
    if (strcmp(file_io_get_game_root(), abs_expect("/test/root")) == 0) {
        PASS();
    } else {
        FAIL(file_io_get_game_root());
    }

    /* Reset game root for remaining tests. */
    file_io_set_game_root(NULL);
}

static void test_text_read_write(void)
{
    setup_test_dir();

    TEST(write_text);
    if (file_io_write_text(test_file, "Hello, World!")) {
        PASS();
    } else {
        FAIL("write_text failed");
    }

    TEST(read_text_roundtrip);
    size_t size = 0;
    char *data = file_io_read_text(test_file, &size);
    if (data && size == 13 && strcmp(data, "Hello, World!") == 0) {
        PASS();
    } else {
        FAIL(data ? data : "NULL");
    }
    free(data);

    TEST(read_nonexistent);
    data = file_io_read_text(test_tmp_path("rmmz_test_file_io/nonexistent.txt"), &size);
    if (data == NULL) {
        PASS();
    } else {
        FAIL("expected NULL for non-existent file");
        free(data);
    }

    TEST(write_empty_text);
    if (file_io_write_text(test_file, "")) {
        data = file_io_read_text(test_file, &size);
        if (data && size == 0 && data[0] == '\0') {
            PASS();
        } else {
            FAIL("empty text roundtrip failed");
        }
        free(data);
    } else {
        FAIL("write empty text failed");
    }

    TEST(read_write_null_args);
    if (!file_io_write_text(NULL, "x") &&
        !file_io_write_text(test_file, NULL) &&
        file_io_read_text(NULL, &size) == NULL) {
        PASS();
    } else {
        FAIL("should fail with NULL args");
    }

    cleanup_test_dir();
}

static void test_binary_read_write(void)
{
    setup_test_dir();

    /* Write binary data with embedded NULs. */
    uint8_t bin_data[] = {0x00, 0x01, 0x02, 0xFF, 0xFE, 0x00, 0x42, 0x43};
    TEST(write_binary);
    if (file_io_write_binary(test_bin, bin_data, sizeof(bin_data))) {
        PASS();
    } else {
        FAIL("write_binary failed");
    }

    TEST(read_binary_roundtrip);
    size_t size = 0;
    uint8_t *data = file_io_read_binary(test_bin, &size);
    if (data && size == sizeof(bin_data) && memcmp(data, bin_data, size) == 0) {
        PASS();
    } else {
        FAIL("binary roundtrip mismatch");
    }
    free(data);

    TEST(write_zero_length_binary);
    if (file_io_write_binary(test_bin, bin_data, 0)) {
        data = file_io_read_binary(test_bin, &size);
        if (data && size == 0) {
            PASS();
        } else {
            /* malloc(0) is implementation-defined, so data may be NULL. */
            if (size == 0) {
                PASS();
            } else {
                FAIL("zero-length binary roundtrip failed");
            }
        }
        free(data);
    } else {
        FAIL("write zero-length binary failed");
    }

    cleanup_test_dir();
}

static void test_directory_operations(void)
{
    setup_test_dir();

    TEST(exists_directory);
    if (file_io_exists(test_dir)) {
        PASS();
    } else {
        FAIL("test dir should exist");
    }

    TEST(is_directory);
    if (file_io_is_directory(test_dir)) {
        PASS();
    } else {
        FAIL("test dir should be a directory");
    }

    TEST(mkdir_subdir);
    if (file_io_mkdir(test_subdir) && file_io_is_directory(test_subdir)) {
        PASS();
    } else {
        FAIL("mkdir subdir failed");
    }

    TEST(mkdir_nested);
    if (file_io_mkdir(test_nested) && file_io_is_directory(test_nested)) {
        PASS();
    } else {
        FAIL("mkdir nested failed");
    }

    /* mkdir on existing directory succeeds. */
    TEST(mkdir_existing);
    if (file_io_mkdir(test_dir)) {
        PASS();
    } else {
        FAIL("mkdir existing should succeed");
    }

    TEST(readdir_entries);
    file_io_write_text(test_tmp_path("rmmz_test_file_io/file_a.txt"), "a");
    file_io_write_text(test_tmp_path("rmmz_test_file_io/file_b.txt"), "b");
    size_t count = 0;
    char **entries = file_io_readdir(test_dir, &count);
    if (entries) {
        bool has_file_a = false, has_file_b = false, has_subdir = false;
        for (size_t i = 0; i < count; i++) {
            if (strcmp(entries[i], "file_a.txt") == 0) has_file_a = true;
            if (strcmp(entries[i], "file_b.txt") == 0) has_file_b = true;
            if (strcmp(entries[i], "subdir") == 0) has_subdir = true;
        }
        if (has_file_a && has_file_b && has_subdir) {
            PASS();
        } else {
            FAIL("missing expected entries");
        }
        file_io_free_dirlist(entries);
    } else {
        FAIL("readdir returned NULL");
    }

    TEST(readdir_no_dots);
    entries = file_io_readdir(test_dir, &count);
    if (entries) {
        bool has_dot = false;
        for (size_t i = 0; i < count; i++) {
            if (strcmp(entries[i], ".") == 0 || strcmp(entries[i], "..") == 0)
                has_dot = true;
        }
        if (!has_dot) {
            PASS();
        } else {
            FAIL("readdir should not include . or ..");
        }
        file_io_free_dirlist(entries);
    } else {
        FAIL("readdir returned NULL");
    }

    TEST(exists_nonexistent);
    if (!file_io_exists(test_tmp_path("rmmz_test_file_io/no_such_thing"))) {
        PASS();
    } else {
        FAIL("non-existent path should not exist");
    }

    TEST(is_directory_on_file);
    file_io_write_text(test_tmp_path("rmmz_test_file_io/file_a.txt"), "a");
    if (!file_io_is_directory(test_tmp_path("rmmz_test_file_io/file_a.txt"))) {
        PASS();
    } else {
        FAIL("file should not be a directory");
    }

    TEST(unlink_file);
    if (file_io_unlink(test_tmp_path("rmmz_test_file_io/file_a.txt")) &&
        !file_io_exists(test_tmp_path("rmmz_test_file_io/file_a.txt"))) {
        PASS();
    } else {
        FAIL("unlink failed");
    }

    TEST(unlink_nonexistent);
    if (!file_io_unlink(test_tmp_path("rmmz_test_file_io/no_such_file"))) {
        PASS();
    } else {
        FAIL("unlink non-existent should fail");
    }

    TEST(dir_ops_null_safety);
    if (!file_io_exists(NULL) &&
        !file_io_is_directory(NULL) &&
        !file_io_mkdir(NULL) &&
        file_io_readdir(NULL, NULL) == NULL &&
        !file_io_unlink(NULL)) {
        PASS();
    } else {
        FAIL("should handle NULL args");
    }

    TEST(free_dirlist_null);
    file_io_free_dirlist(NULL);
    PASS();

    cleanup_test_dir();
}

/* JS-level binding tests */

/* Console capture for error diagnostics. */
#define CONSOLE_BUF_MAX 4096
#define CONSOLE_MSG_MAX 64

typedef struct {
    char level[CONSOLE_MSG_MAX][16];
    char message[CONSOLE_MSG_MAX][CONSOLE_BUF_MAX];
    int count;
} ConsoleCapture;

static void console_capture_cb(const char *level, const char *message, void *userdata)
{
    ConsoleCapture *cap = userdata;
    if (cap->count < CONSOLE_MSG_MAX) {
        strncpy(cap->level[cap->count], level, 15);
        cap->level[cap->count][15] = '\0';
        strncpy(cap->message[cap->count], message, CONSOLE_BUF_MAX - 1);
        cap->message[cap->count][CONSOLE_BUF_MAX - 1] = '\0';
        cap->count++;
    }
}

static void test_js_bindings(void)
{
    setup_test_dir();

    JSEngine *engine = js_engine_init();
    if (!engine) {
        TEST(js_engine_init);
        FAIL("failed to create JS engine");
        return;
    }

    ConsoleCapture cap = {0};
    js_engine_set_console_callback(engine, console_capture_cb, &cap);

    JSContext *ctx = js_engine_get_context(engine);
    bind_io_register(ctx);

    /* Game root is set at C level; it is not exposed to JS. */
    TEST(js_set_game_root);
    file_io_set_game_root(test_dir);
    PASS();

    TEST(js_get_game_root);
    char *result = js_engine_eval_string(engine,
        "__native_io.getGameRoot()", "test");
    if (result && strcmp(result, test_dir) == 0) {
        PASS();
    } else {
        FAIL(result ? result : "NULL");
    }
    js_engine_free_string(result);

    TEST(js_resolve_path);
    result = js_engine_eval_string(engine,
        "__native_io.resolvePath('data/Actors.json')", "test");
    if (result && strcmp(result, test_tmp_path("rmmz_test_file_io/data/Actors.json")) == 0) {
        PASS();
    } else {
        FAIL(result ? result : "NULL");
    }
    js_engine_free_string(result);

    TEST(js_write_read_text);
    bool ok = js_engine_eval(engine,
        jsf("__native_io.writeFileSync('@DIR@/js_test.txt', 'Hello from JS!');"), "test");
    if (ok) {
        result = js_engine_eval_string(engine,
            jsf("__native_io.readFileSync('@DIR@/js_test.txt')"), "test");
        if (result && strcmp(result, "Hello from JS!") == 0) {
            PASS();
        } else {
            FAIL(result ? result : "NULL");
        }
        js_engine_free_string(result);
    } else {
        FAIL("write eval failed");
    }

    TEST(js_relative_path_roundtrip);
    ok = js_engine_eval(engine,
        "__native_io.writeFileSync('rel_test.txt', 'relative path works');", "test");
    if (ok) {
        result = js_engine_eval_string(engine,
            "__native_io.readFileSync('rel_test.txt')", "test");
        if (result && strcmp(result, "relative path works") == 0) {
            PASS();
        } else {
            FAIL(result ? result : "NULL");
        }
        js_engine_free_string(result);
    } else {
        FAIL("relative write failed");
    }

    TEST(js_write_read_binary);
    ok = js_engine_eval(engine,
        jsf("var buf = new ArrayBuffer(4);\n"
        "var view = new Uint8Array(buf);\n"
        "view[0] = 0xDE; view[1] = 0xAD; view[2] = 0xBE; view[3] = 0xEF;\n"
        "__native_io.writeFileBinary('@DIR@/js_test.bin', buf);\n"), "test");
    if (ok) {
        result = js_engine_eval_string(engine,
            jsf("var rd = __native_io.readFileBinary('@DIR@/js_test.bin');\n"
            "var rv = new Uint8Array(rd);\n"
            "rv[0].toString(16) + rv[1].toString(16) + rv[2].toString(16) + rv[3].toString(16);\n"), "test");
        if (result && strcmp(result, "deadbeef") == 0) {
            PASS();
        } else {
            FAIL(result ? result : "NULL");
        }
        js_engine_free_string(result);
    } else {
        FAIL("binary write eval failed");
    }

    TEST(js_mkdir);
    result = js_engine_eval_string(engine,
        jsf("String(__native_io.mkdirSync('@DIR@/js_dir'));"), "test");
    if (result && strcmp(result, "true") == 0) {
        PASS();
    } else {
        FAIL(result ? result : "NULL");
    }
    js_engine_free_string(result);

    TEST(js_exists);
    result = js_engine_eval_string(engine,
        jsf("String(__native_io.existsSync('@DIR@/js_dir'));"), "test");
    if (result && strcmp(result, "true") == 0) {
        PASS();
    } else {
        FAIL(result ? result : "NULL");
    }
    js_engine_free_string(result);

    TEST(js_exists_nonexistent);
    result = js_engine_eval_string(engine,
        jsf("String(__native_io.existsSync('@DIR@/nope'));"), "test");
    if (result && strcmp(result, "false") == 0) {
        PASS();
    } else {
        FAIL(result ? result : "NULL");
    }
    js_engine_free_string(result);

    TEST(js_is_dir);
    result = js_engine_eval_string(engine,
        jsf("String(__native_io.isDirSync('@DIR@/js_dir'));"), "test");
    if (result && strcmp(result, "true") == 0) {
        PASS();
    } else {
        FAIL(result ? result : "NULL");
    }
    js_engine_free_string(result);

    TEST(js_readdir);
    js_engine_eval(engine,
        jsf("__native_io.writeFileSync('@DIR@/js_dir/a.txt', 'a');"
        "__native_io.writeFileSync('@DIR@/js_dir/b.txt', 'b');"), "test");
    result = js_engine_eval_string(engine,
        jsf("var list = __native_io.readdirSync('@DIR@/js_dir');\n"
        "list.sort().join(',');\n"), "test");
    if (result && strcmp(result, "a.txt,b.txt") == 0) {
        PASS();
    } else {
        FAIL(result ? result : "NULL");
    }
    js_engine_free_string(result);

    TEST(js_unlink);
    result = js_engine_eval_string(engine,
        jsf("String(__native_io.unlinkSync('@DIR@/js_dir/a.txt'));"), "test");
    if (result && strcmp(result, "true") == 0) {
        js_engine_free_string(result);
        result = js_engine_eval_string(engine,
            jsf("String(__native_io.existsSync('@DIR@/js_dir/a.txt'));"), "test");
        if (result && strcmp(result, "false") == 0) {
            PASS();
        } else {
            FAIL("file still exists after unlink");
        }
    } else {
        FAIL(result ? result : "NULL");
    }
    js_engine_free_string(result);

    TEST(js_read_nonexistent_throws);
    cap.count = 0;
    ok = js_engine_eval(engine,
        jsf("try { __native_io.readFileSync('@DIR@/nope.txt'); 'no_error'; }"
        "catch(e) { 'caught'; }"), "test");
    result = js_engine_eval_string(engine,
        jsf("try { __native_io.readFileSync('@DIR@/nope.txt'); 'no_error'; }"
        "catch(e) { 'caught'; }"), "test");
    if (result && strcmp(result, "caught") == 0) {
        PASS();
    } else {
        FAIL(result ? result : "NULL");
    }
    js_engine_free_string(result);

    js_engine_shutdown(engine);
    cleanup_test_dir();
}

/* Main */

int main(void)
{
    snprintf(test_dir, sizeof(test_dir), "%s/rmmz_test_file_io", test_tmp_root());
    snprintf(test_subdir, sizeof(test_subdir), "%s/subdir", test_dir);
    snprintf(test_nested, sizeof(test_nested), "%s/a/b/c", test_dir);
    snprintf(test_file, sizeof(test_file), "%s/test.txt", test_dir);
    snprintf(test_bin, sizeof(test_bin), "%s/test.bin", test_dir);

    printf("=== File I/O Tests ===\n");

    printf("\n-- Path Resolution --\n");
    test_path_resolution();

    printf("\n-- Text Read/Write --\n");
    test_text_read_write();

    printf("\n-- Binary Read/Write --\n");
    test_binary_read_write();

    printf("\n-- Directory Operations --\n");
    test_directory_operations();

    printf("\n-- JS Bindings --\n");
    test_js_bindings();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);

    cleanup_test_dir();

    return (tests_passed == tests_run) ? 0 : 1;
}
