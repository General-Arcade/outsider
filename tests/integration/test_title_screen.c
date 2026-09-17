/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

/*
 * tests/integration/test_title_screen.c — Integration test for the RPG Maker MZ
 * boot sequence: shims load, the database and title images load through the XHR
 * and image shims, and Scene_Boot transitions to a populated Scene_Title.
 */

#include "../test_paths.h"
#include "engine/js_engine.h"
#include "engine/script_loader.h"
#include "io/file_io.h"
#include "bindings/bind_io.h"
#include "bindings/bind_image.h"
#include "bindings/bind_canvas2d.h"
#include "bindings/bind_renderer.h"
#include "bindings/bind_font.h"
#include "bindings/bind_tilemap.h"
#include "rendering/image_loader.h"
#include "rendering/canvas2d.h"
#include "rendering/font_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* Console capture */

#define MAX_CONSOLE_MSGS 256
#define MAX_MSG_LEN 1024

typedef struct {
    char level[16];
    char message[MAX_MSG_LEN];
} ConsoleMsg;

static ConsoleMsg captured_msgs[MAX_CONSOLE_MSGS];
static int captured_count = 0;

static void console_capture_cb(const char *level, const char *message, void *userdata)
{
    (void)userdata;
    if (captured_count < MAX_CONSOLE_MSGS) {
        snprintf(captured_msgs[captured_count].level, sizeof(captured_msgs[0].level), "%s", level);
        snprintf(captured_msgs[captured_count].message, sizeof(captured_msgs[0].message), "%s", message);
        captured_count++;
    }
}

static void console_capture_reset(void)
{
    captured_count = 0;
}

static int console_count_level(const char *level)
{
    int count = 0;
    for (int i = 0; i < captured_count; i++) {
        if (strcmp(captured_msgs[i].level, level) == 0) count++;
    }
    return count;
}

static bool console_has_message(const char *level, const char *substring)
{
    for (int i = 0; i < captured_count; i++) {
        if (strcmp(captured_msgs[i].level, level) == 0) {
            if (strstr(captured_msgs[i].message, substring) != NULL) {
                return true;
            }
        }
    }
    return false;
}

/* Mock game directory setup */

/* Scratch directory under the platform temp directory; filled in by main(). */
static char MOCK_GAME_DIR[TEST_PATH_MAX];

static void setup_mock_game(void)
{
    test_rm_rf(MOCK_GAME_DIR);

    file_io_mkdir(MOCK_GAME_DIR);

    /* Create directory structure matching RPG Maker MZ */
    char path[512];
    snprintf(path, sizeof(path), "%s/data", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/img", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/img/titles1", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/img/titles2", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/img/system", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/fonts", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/js", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/js/libs", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/js/plugins", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/audio", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/audio/bgm", MOCK_GAME_DIR); file_io_mkdir(path);

    /* Valid 4x4 RGBA PNG from test fixtures (stb_image compatible) */
    static const unsigned char tiny_png[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04,
        0x08, 0x06, 0x00, 0x00, 0x00, 0xa9, 0xf1, 0x9e, 0x7e, 0x00, 0x00, 0x00,
        0x17, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
        0x1f, 0x84, 0x91, 0x20, 0x9a, 0x00, 0x94, 0x0f, 0x07, 0x18, 0x02, 0x00,
        0x97, 0xe4, 0x27, 0xd9, 0xe0, 0x08, 0x96, 0xdd, 0x00, 0x00, 0x00, 0x00,
        0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
    };

    snprintf(path, sizeof(path), "%s/img/titles1/Castle.png", MOCK_GAME_DIR);
    file_io_write_binary(path, tiny_png, sizeof(tiny_png));

    snprintf(path, sizeof(path), "%s/img/titles2/Stakes.png", MOCK_GAME_DIR);
    file_io_write_binary(path, tiny_png, sizeof(tiny_png));

    snprintf(path, sizeof(path), "%s/img/system/Window.png", MOCK_GAME_DIR);
    file_io_write_binary(path, tiny_png, sizeof(tiny_png));

    /* ---- Data JSON files ---- */

    snprintf(path, sizeof(path), "%s/data/System.json", MOCK_GAME_DIR);
    file_io_write_text(path,
        "{\n"
        "  \"gameTitle\": \"Test Game\",\n"
        "  \"locale\": \"en_US\",\n"
        "  \"advanced\": { \"screenWidth\": 816, \"screenHeight\": 624 },\n"
        "  \"title1Name\": \"Castle\",\n"
        "  \"title2Name\": \"Stakes\",\n"
        "  \"titleBgm\": { \"name\": \"Theme1\", \"volume\": 90, \"pitch\": 100, \"pan\": 0 },\n"
        "  \"windowTone\": [0, 0, 0, 0],\n"
        "  \"sounds\": [\n"
        "    { \"name\": \"Cursor2\", \"volume\": 90, \"pitch\": 100, \"pan\": 0 },\n"
        "    { \"name\": \"Decision2\", \"volume\": 90, \"pitch\": 100, \"pan\": 0 }\n"
        "  ],\n"
        "  \"startMapId\": 1,\n"
        "  \"startX\": 8,\n"
        "  \"startY\": 6,\n"
        "  \"versionId\": 1\n"
        "}\n"
    );

    snprintf(path, sizeof(path), "%s/data/Actors.json", MOCK_GAME_DIR);
    file_io_write_text(path,
        "[null, {\"id\":1,\"name\":\"Harold\",\"nickname\":\"\",\"classId\":1}]\n"
    );

    snprintf(path, sizeof(path), "%s/data/Classes.json", MOCK_GAME_DIR);
    file_io_write_text(path, "[null, {\"id\":1,\"name\":\"Hero\"}]\n");

    snprintf(path, sizeof(path), "%s/data/CommonEvents.json", MOCK_GAME_DIR);
    file_io_write_text(path, "[null]\n");

    snprintf(path, sizeof(path), "%s/data/Items.json", MOCK_GAME_DIR);
    file_io_write_text(path, "[null, {\"id\":1,\"name\":\"Potion\"}]\n");

    snprintf(path, sizeof(path), "%s/data/Skills.json", MOCK_GAME_DIR);
    file_io_write_text(path, "[null, {\"id\":1,\"name\":\"Attack\"}]\n");

    snprintf(path, sizeof(path), "%s/data/Weapons.json", MOCK_GAME_DIR);
    file_io_write_text(path, "[null, {\"id\":1,\"name\":\"Sword\"}]\n");

    snprintf(path, sizeof(path), "%s/data/Armors.json", MOCK_GAME_DIR);
    file_io_write_text(path, "[null, {\"id\":1,\"name\":\"Shield\"}]\n");

    snprintf(path, sizeof(path), "%s/data/Enemies.json", MOCK_GAME_DIR);
    file_io_write_text(path, "[null, {\"id\":1,\"name\":\"Slime\"}]\n");

    snprintf(path, sizeof(path), "%s/data/Troops.json", MOCK_GAME_DIR);
    file_io_write_text(path, "[null, {\"id\":1,\"name\":\"Slime*2\"}]\n");

    snprintf(path, sizeof(path), "%s/data/States.json", MOCK_GAME_DIR);
    file_io_write_text(path, "[null, {\"id\":1,\"name\":\"Death\"}]\n");

    snprintf(path, sizeof(path), "%s/data/Animations.json", MOCK_GAME_DIR);
    file_io_write_text(path, "[null]\n");

    snprintf(path, sizeof(path), "%s/data/Tilesets.json", MOCK_GAME_DIR);
    file_io_write_text(path, "[null, {\"id\":1,\"name\":\"World\"}]\n");

    snprintf(path, sizeof(path), "%s/data/MapInfos.json", MOCK_GAME_DIR);
    file_io_write_text(path, "[null, {\"id\":1,\"name\":\"Map001\"}]\n");

    snprintf(path, sizeof(path), "%s/data/Map001.json", MOCK_GAME_DIR);
    file_io_write_text(path,
        "{ \"width\": 17, \"height\": 13, \"data\": [], "
        "\"events\": [null], \"tilesetId\": 1 }\n"
    );

    /* ---- Stub game scripts ---- */

    /* pako.min.js - minimal inflate/deflate stubs */
    snprintf(path, sizeof(path), "%s/js/libs/pako.min.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "var pako = {\n"
        "  inflate: function(data) { return data; },\n"
        "  deflate: function(data) { return data; }\n"
        "};\n"
    );

    /* plugins.js with test plugins */
    snprintf(path, sizeof(path), "%s/js/plugins.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "var $plugins = [\n"
        "  {\"name\":\"GoodPlugin\",\"status\":true,\"description\":\"\",\"parameters\":{}},\n"
        "  {\"name\":\"DisabledPlugin\",\"status\":false,\"description\":\"\",\"parameters\":{}},\n"
        "  {\"name\":\"ErrorPlugin\",\"status\":true,\"description\":\"\",\"parameters\":{}}\n"
        "];\n"
    );

    /* GoodPlugin - works fine */
    snprintf(path, sizeof(path), "%s/js/plugins/GoodPlugin.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* GoodPlugin: adds a custom command */\n"
        "var GoodPlugin = { loaded: true };\n"
        "console.log('[Plugin] GoodPlugin loaded successfully');\n"
    );

    /* ErrorPlugin - generates a non-fatal error */
    snprintf(path, sizeof(path), "%s/js/plugins/ErrorPlugin.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* ErrorPlugin: references missing API */\n"
        "try {\n"
        "    /* Attempt to use a browser API that doesn't exist */\n"
        "    var result = typeof SomeNonexistentBrowserAPI;\n"
        "    console.log('[Plugin] ErrorPlugin loaded (non-fatal)');\n"
        "} catch(e) {\n"
        "    console.error('[Plugin] ErrorPlugin error: ' + e.message);\n"
        "}\n"
    );

    /* ---- Mock RPG Maker MZ core scripts ---- */
    /* Minimal stand-ins for the real MZ classes that exercise the same shim
       code paths during the boot/title sequence. */

    /* rmmz_core.js: Graphics, Bitmap, Sprite base */
    snprintf(path, sizeof(path), "%s/js/rmmz_core.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock rmmz_core.js */\n"
        "\n"
        "/* ---- Utils ---- */\n"
        "function Utils() {}\n"
        "Utils.RPGMAKER_NAME = 'MZ';\n"
        "Utils.RPGMAKER_VERSION = '1.6.0';\n"
        "Utils.isNwjs = function() { return false; };\n"
        "Utils.isMobileDevice = function() { return false; };\n"
        "Utils.canPlayOgg = function() { return true; };\n"
        "Utils.canPlayWebm = function() { return true; };\n"
        "\n"
        "/* ---- Graphics ---- */\n"
        "function Graphics() {}\n"
        "Graphics._width = 816;\n"
        "Graphics._height = 624;\n"
        "Graphics._defaultScale = 1;\n"
        "Graphics._realScale = 1;\n"
        "Graphics._app = null;\n"
        "Graphics._canvas = null;\n"
        "Graphics._errorPrinter = null;\n"
        "Graphics._tickHandler = null;\n"
        "Graphics._fpsCounter = null;\n"
        "Graphics._loadingSpinner = null;\n"
        "Graphics.frameCount = 0;\n"
        "\n"
        "Graphics.initialize = function() {\n"
        "    this._createAllElements();\n"
        "    this._createPixiApp();\n"
        "    this._updateRealScale();\n"
        "    console.log('[Graphics] Initialized ' + this._width + 'x' + this._height);\n"
        "};\n"
        "\n"
        "Graphics._createAllElements = function() {\n"
        "    this._canvas = document.createElement('canvas');\n"
        "    this._canvas.id = 'gameCanvas';\n"
        "    document.body.appendChild(this._canvas);\n"
        "};\n"
        "\n"
        "Graphics._createPixiApp = function() {\n"
        "    try {\n"
        "        this._app = new PIXI.Application({\n"
        "            view: this._canvas,\n"
        "            width: this._width,\n"
        "            height: this._height\n"
        "        });\n"
        "        console.log('[Graphics] PIXI.Application created');\n"
        "    } catch(e) {\n"
        "        console.error('[Graphics] PIXI.Application error: ' + e.message);\n"
        "    }\n"
        "};\n"
        "\n"
        "Graphics._updateRealScale = function() {\n"
        "    var w = window.innerWidth || this._width;\n"
        "    var h = window.innerHeight || this._height;\n"
        "    this._realScale = Math.min(w / this._width, h / this._height);\n"
        "};\n"
        "\n"
        "Graphics.startGameLoop = function() {\n"
        "    this._tickHandler = this._onTick.bind(this);\n"
        "    requestAnimationFrame(this._tickHandler);\n"
        "};\n"
        "\n"
        "Graphics._onTick = function(timestamp) {\n"
        "    this.frameCount++;\n"
        "    if (typeof SceneManager !== 'undefined') {\n"
        "        SceneManager.update(timestamp);\n"
        "    }\n"
        "    requestAnimationFrame(this._tickHandler);\n"
        "};\n"
        "\n"
        "Graphics.printError = function(name, message) {\n"
        "    console.error('[Graphics Error] ' + name + ': ' + message);\n"
        "};\n"
        "\n"
        "/* ---- Bitmap ---- */\n"
        "function Bitmap(width, height) {\n"
        "    this._canvas = document.createElement('canvas');\n"
        "    this._canvas.width = Math.max(width || 0, 1);\n"
        "    this._canvas.height = Math.max(height || 0, 1);\n"
        "    this._context = this._canvas.getContext('2d');\n"
        "    this._image = null;\n"
        "    this._url = '';\n"
        "    this._loadingState = 'none';\n"
        "    this._loadListeners = [];\n"
        "    this.fontFace = 'GameFont';\n"
        "    this.fontSize = 26;\n"
        "    this.fontBold = false;\n"
        "    this.fontItalic = false;\n"
        "    this.textColor = '#ffffff';\n"
        "    this.outlineColor = 'rgba(0,0,0,0.5)';\n"
        "    this.outlineWidth = 3;\n"
        "}\n"
        "\n"
        "Object.defineProperty(Bitmap.prototype, 'width', {\n"
        "    get: function() { return this._canvas ? this._canvas.width : 0; }\n"
        "});\n"
        "\n"
        "Object.defineProperty(Bitmap.prototype, 'height', {\n"
        "    get: function() { return this._canvas ? this._canvas.height : 0; }\n"
        "});\n"
        "\n"
        "Bitmap.load = function(url) {\n"
        "    var bitmap = new Bitmap();\n"
        "    bitmap._url = url;\n"
        "    bitmap._loadingState = 'loading';\n"
        "    bitmap._image = new Image();\n"
        "    bitmap._image.onload = function() {\n"
        "        bitmap._canvas.width = bitmap._image.width;\n"
        "        bitmap._canvas.height = bitmap._image.height;\n"
        "        bitmap._context.drawImage(bitmap._image, 0, 0);\n"
        "        bitmap._loadingState = 'loaded';\n"
        "        bitmap._callLoadListeners();\n"
        "        console.log('[Bitmap] Loaded: ' + url);\n"
        "    };\n"
        "    bitmap._image.onerror = function() {\n"
        "        bitmap._loadingState = 'error';\n"
        "        console.error('[Bitmap] Failed to load: ' + url);\n"
        "    };\n"
        "    bitmap._image.src = url;\n"
        "    return bitmap;\n"
        "};\n"
        "\n"
        "Bitmap.prototype.addLoadListener = function(listener) {\n"
        "    if (this._loadingState === 'loaded') {\n"
        "        listener(this);\n"
        "    } else {\n"
        "        this._loadListeners.push(listener);\n"
        "    }\n"
        "};\n"
        "\n"
        "Bitmap.prototype._callLoadListeners = function() {\n"
        "    while (this._loadListeners.length > 0) {\n"
        "        var listener = this._loadListeners.shift();\n"
        "        listener(this);\n"
        "    }\n"
        "};\n"
        "\n"
        "Bitmap.prototype.drawText = function(text, x, y, maxWidth, lineHeight, align) {\n"
        "    if (this._context) {\n"
        "        this._context.fillText(text, x, y + (lineHeight || this.fontSize));\n"
        "    }\n"
        "};\n"
        "\n"
        "Bitmap.prototype.measureTextWidth = function(text) {\n"
        "    if (this._context) {\n"
        "        return this._context.measureText(text).width;\n"
        "    }\n"
        "    return 0;\n"
        "};\n"
        "\n"
        "Bitmap.prototype.destroy = function() {\n"
        "    this._canvas = null;\n"
        "    this._context = null;\n"
        "    this._image = null;\n"
        "};\n"
        "\n"
        "/* ---- Sprite (PIXI wrapper) ---- */\n"
        "function Sprite(bitmap) {\n"
        "    PIXI.Sprite.call(this);\n"
        "    this.bitmap = bitmap || null;\n"
        "    this._frame = new PIXI.Rectangle();\n"
        "}\n"
        "\n"
        "Sprite.prototype = Object.create(PIXI.Sprite.prototype);\n"
        "Sprite.prototype.constructor = Sprite;\n"
        "\n"
        "Sprite.prototype.destroy = function() {\n"
        "    PIXI.Sprite.prototype.destroy.call(this);\n"
        "    this.bitmap = null;\n"
        "};\n"
        "\n"
        "console.log('[rmmz_core] Core classes defined');\n"
    );

    /* rmmz_managers.js: DataManager, ImageManager, SceneManager, AudioManager */
    snprintf(path, sizeof(path), "%s/js/rmmz_managers.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock rmmz_managers.js */\n"
        "\n"
        "/* ---- DataManager ---- */\n"
        "function DataManager() {}\n"
        "\n"
        "DataManager._globalInfo = null;\n"
        "DataManager._errors = [];\n"
        "DataManager._databaseLoaded = false;\n"
        "\n"
        "var $dataSystem = null;\n"
        "var $dataActors = null;\n"
        "var $dataClasses = null;\n"
        "var $dataItems = null;\n"
        "var $dataSkills = null;\n"
        "var $dataWeapons = null;\n"
        "var $dataArmors = null;\n"
        "var $dataEnemies = null;\n"
        "var $dataTroops = null;\n"
        "var $dataStates = null;\n"
        "var $dataAnimations = null;\n"
        "var $dataTilesets = null;\n"
        "var $dataCommonEvents = null;\n"
        "var $dataMapInfos = null;\n"
        "var $dataMap = null;\n"
        "\n"
        "DataManager._databaseFiles = [\n"
        "    { name: '$dataActors', src: 'Actors.json' },\n"
        "    { name: '$dataClasses', src: 'Classes.json' },\n"
        "    { name: '$dataSkills', src: 'Skills.json' },\n"
        "    { name: '$dataItems', src: 'Items.json' },\n"
        "    { name: '$dataWeapons', src: 'Weapons.json' },\n"
        "    { name: '$dataArmors', src: 'Armors.json' },\n"
        "    { name: '$dataEnemies', src: 'Enemies.json' },\n"
        "    { name: '$dataTroops', src: 'Troops.json' },\n"
        "    { name: '$dataStates', src: 'States.json' },\n"
        "    { name: '$dataAnimations', src: 'Animations.json' },\n"
        "    { name: '$dataTilesets', src: 'Tilesets.json' },\n"
        "    { name: '$dataCommonEvents', src: 'CommonEvents.json' },\n"
        "    { name: '$dataSystem', src: 'System.json' },\n"
        "    { name: '$dataMapInfos', src: 'MapInfos.json' }\n"
        "];\n"
        "\n"
        "DataManager.loadDatabase = function() {\n"
        "    for (var i = 0; i < this._databaseFiles.length; i++) {\n"
        "        var file = this._databaseFiles[i];\n"
        "        this.loadDataFile(file.name, file.src);\n"
        "    }\n"
        "    console.log('[DataManager] loadDatabase called, ' + this._databaseFiles.length + ' files queued');\n"
        "};\n"
        "\n"
        "DataManager.loadDataFile = function(name, src) {\n"
        "    var xhr = new XMLHttpRequest();\n"
        "    var url = 'data/' + src;\n"
        "    xhr.open('GET', url);\n"
        "    xhr.overrideMimeType('application/json');\n"
        "    xhr.onload = function() {\n"
        "        if (xhr.status < 400) {\n"
        "            globalThis[name] = JSON.parse(xhr.responseText);\n"
        "            console.log('[DataManager] Loaded: ' + name);\n"
        "        } else {\n"
        "            DataManager._errors.push('Failed to load ' + src + ': HTTP ' + xhr.status);\n"
        "            console.error('[DataManager] HTTP ' + xhr.status + ' for ' + src);\n"
        "        }\n"
        "    };\n"
        "    xhr.onerror = function() {\n"
        "        DataManager._errors.push('Network error loading ' + src);\n"
        "        console.error('[DataManager] Error loading ' + src);\n"
        "    };\n"
        "    xhr.send();\n"
        "};\n"
        "\n"
        "DataManager.isDatabaseLoaded = function() {\n"
        "    for (var i = 0; i < this._databaseFiles.length; i++) {\n"
        "        if (globalThis[this._databaseFiles[i].name] == null) return false;\n"
        "    }\n"
        "    return true;\n"
        "};\n"
        "\n"
        "/* ---- ImageManager ---- */\n"
        "function ImageManager() {}\n"
        "\n"
        "ImageManager._cache = {};\n"
        "ImageManager._loadedImages = [];\n"
        "\n"
        "ImageManager.loadBitmap = function(folder, filename) {\n"
        "    if (!filename) return new Bitmap(1, 1);\n"
        "    var key = folder + filename;\n"
        "    if (this._cache[key]) return this._cache[key];\n"
        "    var url = folder + filename + '.png';\n"
        "    var bitmap = Bitmap.load(url);\n"
        "    this._cache[key] = bitmap;\n"
        "    this._loadedImages.push(url);\n"
        "    return bitmap;\n"
        "};\n"
        "\n"
        "ImageManager.loadTitle1 = function(filename) {\n"
        "    return this.loadBitmap('img/titles1/', filename);\n"
        "};\n"
        "\n"
        "ImageManager.loadTitle2 = function(filename) {\n"
        "    return this.loadBitmap('img/titles2/', filename);\n"
        "};\n"
        "\n"
        "ImageManager.loadSystem = function(filename) {\n"
        "    return this.loadBitmap('img/system/', filename);\n"
        "};\n"
        "\n"
        "/* ---- AudioManager (stub) ---- */\n"
        "function AudioManager() {}\n"
        "AudioManager.playBgm = function(bgm) {\n"
        "    console.log('[AudioManager] playBgm: ' + (bgm ? bgm.name : 'none'));\n"
        "};\n"
        "AudioManager.stopAll = function() {};\n"
        "\n"
        "/* ---- SceneManager ---- */\n"
        "function SceneManager() {}\n"
        "SceneManager._scene = null;\n"
        "SceneManager._nextScene = null;\n"
        "SceneManager._sceneStarted = false;\n"
        "SceneManager._stopped = false;\n"
        "SceneManager._transitionLog = [];\n"
        "\n"
        "SceneManager.run = function(sceneClass) {\n"
        "    this.goto(sceneClass);\n"
        "    Graphics.startGameLoop();\n"
        "};\n"
        "\n"
        "SceneManager.goto = function(sceneClass) {\n"
        "    if (sceneClass) {\n"
        "        this._nextScene = new sceneClass();\n"
        "    }\n"
        "};\n"
        "\n"
        "SceneManager.update = function(timestamp) {\n"
        "    if (this._stopped) return;\n"
        "    this._changeScene();\n"
        "    this._updateScene();\n"
        "};\n"
        "\n"
        "SceneManager._changeScene = function() {\n"
        "    if (this._nextScene) {\n"
        "        if (this._scene) {\n"
        "            this._scene.terminate();\n"
        "        }\n"
        "        this._scene = this._nextScene;\n"
        "        this._nextScene = null;\n"
        "        this._sceneStarted = false;\n"
        "        if (this._scene.create) {\n"
        "            this._scene.create();\n"
        "        }\n"
        "        var name = this._scene.constructor.name || 'Unknown';\n"
        "        this._transitionLog.push(name);\n"
        "        console.log('[SceneManager] Scene changed to: ' + name);\n"
        "    }\n"
        "};\n"
        "\n"
        "SceneManager._updateScene = function() {\n"
        "    if (this._scene) {\n"
        "        if (!this._sceneStarted) {\n"
        "            if (this._scene.isReady && this._scene.isReady()) {\n"
        "                this._scene.start();\n"
        "                this._sceneStarted = true;\n"
        "            }\n"
        "        }\n"
        "        if (this._sceneStarted) {\n"
        "            this._scene.update();\n"
        "        }\n"
        "    }\n"
        "};\n"
        "\n"
        "SceneManager.catchException = function(e) {\n"
        "    console.error('[SceneManager] Exception: ' + e.message);\n"
        "};\n"
        "\n"
        "console.log('[rmmz_managers] Manager classes defined');\n"
    );

    /* rmmz_objects.js: Game_Temp, Game_System */
    snprintf(path, sizeof(path), "%s/js/rmmz_objects.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock rmmz_objects.js */\n"
        "function Game_Temp() { this.initialize(); }\n"
        "Game_Temp.prototype.initialize = function() { this._isPlaytest = false; };\n"
        "\n"
        "function Game_System() { this.initialize(); }\n"
        "Game_System.prototype.initialize = function() { this._saveEnabled = true; };\n"
        "\n"
        "var $gameTemp = null;\n"
        "var $gameSystem = null;\n"
        "\n"
        "console.log('[rmmz_objects] Game object classes defined');\n"
    );

    /* rmmz_scenes.js: Scene_Base, Scene_Boot, Scene_Title */
    snprintf(path, sizeof(path), "%s/js/rmmz_scenes.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock rmmz_scenes.js */\n"
        "\n"
        "/* ---- Scene_Base ---- */\n"
        "function Scene_Base() {\n"
        "    PIXI.Container.call(this);\n"
        "}\n"
        "Scene_Base.prototype = Object.create(PIXI.Container.prototype);\n"
        "Scene_Base.prototype.constructor = Scene_Base;\n"
        "Scene_Base.prototype.create = function() {};\n"
        "Scene_Base.prototype.isReady = function() { return true; };\n"
        "Scene_Base.prototype.start = function() {\n"
        "    console.log('[' + this.constructor.name + '] start()');\n"
        "};\n"
        "Scene_Base.prototype.update = function() {};\n"
        "Scene_Base.prototype.terminate = function() {\n"
        "    console.log('[' + this.constructor.name + '] terminate()');\n"
        "};\n"
        "\n"
        "/* ---- Scene_Boot ---- */\n"
        "function Scene_Boot() {\n"
        "    Scene_Base.call(this);\n"
        "    this._databaseLoaded = false;\n"
        "}\n"
        "Scene_Boot.prototype = Object.create(Scene_Base.prototype);\n"
        "Scene_Boot.prototype.constructor = Scene_Boot;\n"
        "\n"
        "Scene_Boot.prototype.create = function() {\n"
        "    Scene_Base.prototype.create.call(this);\n"
        "    DataManager.loadDatabase();\n"
        "    console.log('[Scene_Boot] create() - database loading started');\n"
        "};\n"
        "\n"
        "Scene_Boot.prototype.isReady = function() {\n"
        "    return DataManager.isDatabaseLoaded();\n"
        "};\n"
        "\n"
        "Scene_Boot.prototype.start = function() {\n"
        "    Scene_Base.prototype.start.call(this);\n"
        "    this._databaseLoaded = true;\n"
        "    $gameTemp = new Game_Temp();\n"
        "    $gameSystem = new Game_System();\n"
        "    console.log('[Scene_Boot] Database loaded, transitioning to Scene_Title');\n"
        "    SceneManager.goto(Scene_Title);\n"
        "};\n"
        "\n"
        "/* ---- Scene_Title ---- */\n"
        "function Scene_Title() {\n"
        "    Scene_Base.call(this);\n"
        "    this._titleSprite1 = null;\n"
        "    this._titleSprite2 = null;\n"
        "    this._commandWindow = null;\n"
        "    this._started = false;\n"
        "}\n"
        "Scene_Title.prototype = Object.create(Scene_Base.prototype);\n"
        "Scene_Title.prototype.constructor = Scene_Title;\n"
        "\n"
        "Scene_Title.prototype.create = function() {\n"
        "    Scene_Base.prototype.create.call(this);\n"
        "    this.createBackground();\n"
        "    this.createCommandWindow();\n"
        "    console.log('[Scene_Title] create() - title screen elements created');\n"
        "};\n"
        "\n"
        "Scene_Title.prototype.createBackground = function() {\n"
        "    if ($dataSystem) {\n"
        "        this._titleSprite1 = new Sprite();\n"
        "        this._titleSprite1.bitmap = ImageManager.loadTitle1($dataSystem.title1Name);\n"
        "        this.addChild(this._titleSprite1);\n"
        "\n"
        "        this._titleSprite2 = new Sprite();\n"
        "        this._titleSprite2.bitmap = ImageManager.loadTitle2($dataSystem.title2Name);\n"
        "        this.addChild(this._titleSprite2);\n"
        "\n"
        "        console.log('[Scene_Title] Background created with title images: ' +\n"
        "            $dataSystem.title1Name + ', ' + $dataSystem.title2Name);\n"
        "    }\n"
        "};\n"
        "\n"
        "Scene_Title.prototype.createCommandWindow = function() {\n"
        "    /* Simplified command window — just tracks commands */\n"
        "    this._commandWindow = {\n"
        "        commands: ['New Game', 'Continue', 'Options'],\n"
        "        active: true,\n"
        "        visible: true\n"
        "    };\n"
        "    console.log('[Scene_Title] Command window created with: ' +\n"
        "        this._commandWindow.commands.join(', '));\n"
        "};\n"
        "\n"
        "Scene_Title.prototype.start = function() {\n"
        "    Scene_Base.prototype.start.call(this);\n"
        "    this._started = true;\n"
        "    if ($dataSystem && $dataSystem.titleBgm) {\n"
        "        AudioManager.playBgm($dataSystem.titleBgm);\n"
        "    }\n"
        "    console.log('[Scene_Title] Title screen started');\n"
        "};\n"
        "\n"
        "Scene_Title.prototype.update = function() {\n"
        "    Scene_Base.prototype.update.call(this);\n"
        "};\n"
        "\n"
        "console.log('[rmmz_scenes] Scene classes defined');\n"
    );

    /* rmmz_sprites.js */
    snprintf(path, sizeof(path), "%s/js/rmmz_sprites.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock rmmz_sprites.js */\n"
        "function Sprite_Button() { Sprite.call(this); }\n"
        "Sprite_Button.prototype = Object.create(Sprite.prototype);\n"
        "Sprite_Button.prototype.constructor = Sprite_Button;\n"
        "\n"
        "console.log('[rmmz_sprites] Sprite classes defined');\n"
    );

    /* rmmz_windows.js */
    snprintf(path, sizeof(path), "%s/js/rmmz_windows.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock rmmz_windows.js */\n"
        "function Window_Base() { PIXI.Container.call(this); }\n"
        "Window_Base.prototype = Object.create(PIXI.Container.prototype);\n"
        "Window_Base.prototype.constructor = Window_Base;\n"
        "\n"
        "console.log('[rmmz_windows] Window classes defined');\n"
    );

    /* main.js: boot the game */
    snprintf(path, sizeof(path), "%s/js/main.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock main.js */\n"
        "console.log('[main.js] Starting game boot sequence');\n"
        "Graphics.initialize();\n"
        "SceneManager.run(Scene_Boot);\n"
        "console.log('[main.js] Boot sequence initiated');\n"
    );
}

static void cleanup_mock_game(void)
{
    test_rm_rf(MOCK_GAME_DIR);
}

/* Helpers: engine with all bindings, eval, and frame simulation */

static JSEngine *create_full_engine(void)
{
    JSEngine *engine = js_engine_init();
    if (!engine) return NULL;

    JSContext *ctx = js_engine_get_context(engine);

    bind_io_register(ctx);

    /* Initialize rendering subsystems in headless mode (no GL). */
    image_loader_init(false);
    bind_image_register(ctx);

    canvas2d_init();
    bind_canvas2d_register(ctx);

    font_manager_init();
    bind_font_register(ctx);

    bind_renderer_register(ctx);
    bind_tilemap_register(ctx);

    return engine;
}

static void shutdown_full_engine(JSEngine *engine)
{
    font_manager_shutdown();
    canvas2d_shutdown();
    image_loader_shutdown();
    js_engine_shutdown(engine);
}

/* Helper: eval JS and return true if no exception */
static bool eval_ok(JSEngine *engine, const char *script)
{
    return js_engine_eval(engine, script, "<test>");
}

/* Helper: eval JS and return the string result. Caller must free. */
static char *eval_str(JSEngine *engine, const char *script)
{
    return js_engine_eval_string(engine, script, "<test>");
}

/* Helper: simulate N frames (flush timers, microtasks, rAF) */
static void simulate_frames(JSEngine *engine, int count)
{
    for (int i = 0; i < count; i++) {
        eval_ok(engine, "__dom_flushTimers();");
        js_engine_execute_pending_jobs(engine);

        char flush[128];
        snprintf(flush, sizeof(flush),
                 "__dom_flushAnimationFrames(%f);", (i + 1) * 16.67);
        eval_ok(engine, flush);

        js_engine_execute_pending_jobs(engine);
    }
}

/* Loads all shims, the mock core scripts, and main.js. */
TEST(load_all_scripts)
{
    JSEngine *engine = create_full_engine();
    ASSERT(engine != NULL);

    file_io_set_game_root(MOCK_GAME_DIR);

    bool ok = script_loader_load_all(engine, "src/shims", MOCK_GAME_DIR);
    ASSERT(ok);

    /* Verify key globals exist */
    char *result;

    result = eval_str(engine, "typeof Graphics");
    ASSERT(result && strcmp(result, "function") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "typeof DataManager");
    ASSERT(result && strcmp(result, "function") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "typeof ImageManager");
    ASSERT(result && strcmp(result, "function") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "typeof SceneManager");
    ASSERT(result && strcmp(result, "function") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "typeof Scene_Boot");
    ASSERT(result && strcmp(result, "function") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "typeof Scene_Title");
    ASSERT(result && strcmp(result, "function") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "typeof PIXI");
    ASSERT(result && strcmp(result, "object") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "typeof Bitmap");
    ASSERT(result && strcmp(result, "function") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "typeof Sprite");
    ASSERT(result && strcmp(result, "function") == 0);
    js_engine_free_string(result);

    shutdown_full_engine(engine);
    PASS();
}

TEST(data_manager_loads_database)
{
    JSEngine *engine = create_full_engine();
    ASSERT(engine != NULL);

    file_io_set_game_root(MOCK_GAME_DIR);

    bool ok = script_loader_load_all(engine, "src/shims", MOCK_GAME_DIR);
    ASSERT(ok);

    /* main.js already called DataManager.loadDatabase() via Scene_Boot.
       Flush timers so XHR callbacks fire. */
    simulate_frames(engine, 5);

    /* Verify all database globals are loaded */
    char *result;

    result = eval_str(engine, "String(DataManager.isDatabaseLoaded())");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String($dataSystem !== null)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String($dataSystem.gameTitle)");
    ASSERT(result && strcmp(result, "Test Game") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String($dataActors !== null)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String($dataActors[1].name)");
    ASSERT(result && strcmp(result, "Harold") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String($dataItems !== null)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String($dataItems[1].name)");
    ASSERT(result && strcmp(result, "Potion") == 0);
    js_engine_free_string(result);

    /* Verify no DataManager errors */
    result = eval_str(engine, "String(DataManager._errors.length)");
    ASSERT(result && strcmp(result, "0") == 0);
    js_engine_free_string(result);

    shutdown_full_engine(engine);
    PASS();
}

TEST(image_manager_loads_title_images)
{
    JSEngine *engine = create_full_engine();
    ASSERT(engine != NULL);

    js_engine_set_console_callback(engine, console_capture_cb, NULL);
    console_capture_reset();

    file_io_set_game_root(MOCK_GAME_DIR);

    bool ok = script_loader_load_all(engine, "src/shims", MOCK_GAME_DIR);
    ASSERT(ok);

    /* Simulate frames so Scene_Boot→Scene_Title happens and images load */
    simulate_frames(engine, 10);

    /* Verify ImageManager was asked to load title images */
    char *result;

    result = eval_str(engine, "String(ImageManager._loadedImages.length > 0)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    /* Check that title image URLs were requested */
    result = eval_str(engine,
        "String(ImageManager._loadedImages.some(function(u) {"
        "    return u.indexOf('titles1') >= 0;"
        "}))");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine,
        "String(ImageManager._loadedImages.some(function(u) {"
        "    return u.indexOf('titles2') >= 0;"
        "}))");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    /* Verify the console logged the bitmap loads */
    ASSERT(console_has_message("log", "[Bitmap] Loaded:") ||
           console_has_message("log", "titles1"));

    shutdown_full_engine(engine);
    PASS();
}

TEST(scene_boot_to_title_transition)
{
    JSEngine *engine = create_full_engine();
    ASSERT(engine != NULL);

    js_engine_set_console_callback(engine, console_capture_cb, NULL);
    console_capture_reset();

    file_io_set_game_root(MOCK_GAME_DIR);

    bool ok = script_loader_load_all(engine, "src/shims", MOCK_GAME_DIR);
    ASSERT(ok);

    /* Simulate enough frames for boot → database load → title transition */
    simulate_frames(engine, 15);

    /* Verify the scene transition log */
    char *result;

    result = eval_str(engine, "JSON.stringify(SceneManager._transitionLog)");
    ASSERT(result != NULL);
    /* Should have Scene_Boot first, then Scene_Title */
    ASSERT(strstr(result, "Scene_Boot") != NULL);
    ASSERT(strstr(result, "Scene_Title") != NULL);
    js_engine_free_string(result);

    /* Verify Scene_Title is the current scene */
    result = eval_str(engine,
        "String(SceneManager._scene && SceneManager._scene.constructor.name)");
    ASSERT(result && strcmp(result, "Scene_Title") == 0);
    js_engine_free_string(result);

    /* Verify Scene_Title started */
    result = eval_str(engine, "String(SceneManager._scene._started)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    /* Verify console shows the transition flow */
    ASSERT(console_has_message("log", "[Scene_Boot] create()"));
    ASSERT(console_has_message("log", "Scene_Title"));

    shutdown_full_engine(engine);
    PASS();
}

/* Title scene graph is populated after boot. */
TEST(title_screen_renders)
{
    JSEngine *engine = create_full_engine();
    ASSERT(engine != NULL);

    file_io_set_game_root(MOCK_GAME_DIR);

    bool ok = script_loader_load_all(engine, "src/shims", MOCK_GAME_DIR);
    ASSERT(ok);

    /* Simulate frames for full boot + title screen */
    simulate_frames(engine, 20);

    char *result;

    /* Verify the scene has children (title sprites) */
    result = eval_str(engine,
        "String(SceneManager._scene && SceneManager._scene.children.length)");
    ASSERT(result != NULL);
    int child_count = atoi(result);
    ASSERT(child_count >= 2); /* At least title sprite 1 and 2 */
    js_engine_free_string(result);

    /* Verify title sprites have bitmaps assigned */
    result = eval_str(engine,
        "String(SceneManager._scene._titleSprite1 !== null)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine,
        "String(SceneManager._scene._titleSprite2 !== null)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    /* Verify command window exists with menu options */
    result = eval_str(engine,
        "String(SceneManager._scene._commandWindow !== null)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine,
        "String(SceneManager._scene._commandWindow.commands.length)");
    ASSERT(result && strcmp(result, "3") == 0);
    js_engine_free_string(result);

    result = eval_str(engine,
        "String(SceneManager._scene._commandWindow.commands[0])");
    ASSERT(result && strcmp(result, "New Game") == 0);
    js_engine_free_string(result);

    /* Verify Graphics frame counter advanced */
    result = eval_str(engine, "String(Graphics.frameCount > 0)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    /* Verify PIXI.Application was created */
    result = eval_str(engine, "String(Graphics._app !== null)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    shutdown_full_engine(engine);
    PASS();
}

/* Graphics.initialize creates the canvas and PIXI.Application. */
TEST(graphics_initialization)
{
    JSEngine *engine = create_full_engine();
    ASSERT(engine != NULL);

    file_io_set_game_root(MOCK_GAME_DIR);

    bool ok = script_loader_load_all(engine, "src/shims", MOCK_GAME_DIR);
    ASSERT(ok);

    /* main.js calls Graphics.initialize() */
    simulate_frames(engine, 5);

    char *result;

    /* Verify canvas was created */
    result = eval_str(engine, "String(Graphics._canvas !== null)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(Graphics._canvas.id)");
    ASSERT(result && strcmp(result, "gameCanvas") == 0);
    js_engine_free_string(result);

    /* Verify canvas is attached to document body */
    result = eval_str(engine,
        "String(document.getElementById('gameCanvas') !== null)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    /* Verify PIXI.Application was created */
    result = eval_str(engine,
        "String(Graphics._app instanceof PIXI.Application)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    /* Verify Graphics has correct dimensions */
    result = eval_str(engine, "String(Graphics._width)");
    ASSERT(result && strcmp(result, "816") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(Graphics._height)");
    ASSERT(result && strcmp(result, "624") == 0);
    js_engine_free_string(result);

    shutdown_full_engine(engine);
    PASS();
}

TEST(game_objects_initialized)
{
    JSEngine *engine = create_full_engine();
    ASSERT(engine != NULL);

    file_io_set_game_root(MOCK_GAME_DIR);

    bool ok = script_loader_load_all(engine, "src/shims", MOCK_GAME_DIR);
    ASSERT(ok);

    /* Simulate frames for full boot */
    simulate_frames(engine, 15);

    char *result;

    /* $gameTemp and $gameSystem should be initialized by Scene_Boot.start() */
    result = eval_str(engine, "String($gameTemp !== null)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String($gameSystem !== null)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    shutdown_full_engine(engine);
    PASS();
}

/* Enabled plugins load, disabled ones do not, and plugin errors are non-fatal. */
TEST(plugin_errors_documented)
{
    JSEngine *engine = create_full_engine();
    ASSERT(engine != NULL);

    js_engine_set_console_callback(engine, console_capture_cb, NULL);
    console_capture_reset();

    file_io_set_game_root(MOCK_GAME_DIR);

    bool ok = script_loader_load_all(engine, "src/shims", MOCK_GAME_DIR);
    ASSERT(ok);

    simulate_frames(engine, 5);

    /* Verify GoodPlugin loaded */
    char *result;
    result = eval_str(engine, "String(typeof GoodPlugin !== 'undefined')");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(GoodPlugin.loaded)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    /* Verify GoodPlugin logged its load */
    ASSERT(console_has_message("log", "GoodPlugin loaded successfully"));

    /* Verify ErrorPlugin didn't crash the engine */
    ASSERT(console_has_message("log", "ErrorPlugin loaded"));

    /* Verify DisabledPlugin was NOT loaded */
    result = eval_str(engine, "String(typeof DisabledPlugin)");
    ASSERT(result && strcmp(result, "undefined") == 0);
    js_engine_free_string(result);

    /* Document error count */
    int error_count = console_count_level("error");
    printf("(plugin_errors=%d) ", error_count);

    shutdown_full_engine(engine);
    PASS();
}

TEST(full_boot_sequence)
{
    JSEngine *engine = create_full_engine();
    ASSERT(engine != NULL);

    js_engine_set_console_callback(engine, console_capture_cb, NULL);
    console_capture_reset();

    file_io_set_game_root(MOCK_GAME_DIR);

    /* Load everything */
    bool ok = script_loader_load_all(engine, "src/shims", MOCK_GAME_DIR);
    ASSERT(ok);

    /* Run 30 frames — enough for boot + data load + scene transitions */
    simulate_frames(engine, 30);

    char *result;

    /* 1. All database files loaded */
    result = eval_str(engine, "String(DataManager.isDatabaseLoaded())");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    /* 2. Scene_Boot → Scene_Title transition happened */
    result = eval_str(engine, "JSON.stringify(SceneManager._transitionLog)");
    ASSERT(result != NULL);
    ASSERT(strstr(result, "Scene_Boot") != NULL);
    ASSERT(strstr(result, "Scene_Title") != NULL);
    js_engine_free_string(result);

    /* 3. Title screen is active and started */
    result = eval_str(engine,
        "String(SceneManager._scene.constructor.name)");
    ASSERT(result && strcmp(result, "Scene_Title") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(SceneManager._scene._started)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    /* 4. Title images were loaded */
    result = eval_str(engine, "String(ImageManager._loadedImages.length >= 2)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    /* 5. Graphics running */
    result = eval_str(engine, "String(Graphics.frameCount > 0)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    /* 6. AudioManager.playBgm was called */
    ASSERT(console_has_message("log", "[AudioManager] playBgm: Theme1"));

    /* 7. Game objects initialized */
    result = eval_str(engine, "String($gameTemp !== null && $gameSystem !== null)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    /* Print summary of console output for documentation */
    int log_count = console_count_level("log");
    int warn_count = console_count_level("warn");
    int error_count = console_count_level("error");
    printf("(logs=%d warns=%d errors=%d) ", log_count, warn_count, error_count);

    shutdown_full_engine(engine);
    PASS();
}

TEST(sprite_pixi_inheritance)
{
    JSEngine *engine = create_full_engine();
    ASSERT(engine != NULL);

    file_io_set_game_root(MOCK_GAME_DIR);

    bool ok = script_loader_load_all(engine, "src/shims", MOCK_GAME_DIR);
    ASSERT(ok);

    char *result;

    /* Verify Sprite extends PIXI.Sprite which extends PIXI.Container */
    result = eval_str(engine,
        "var s = new Sprite(); String(s instanceof PIXI.Container)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine,
        "var s = new Sprite(); String(typeof s.addChild === 'function')");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine,
        "var s = new Sprite();\n"
        "var child = new Sprite();\n"
        "s.addChild(child);\n"
        "String(s.children.length)");
    ASSERT(result && strcmp(result, "1") == 0);
    js_engine_free_string(result);

    /* Verify Scene_Base extends PIXI.Container */
    result = eval_str(engine,
        "var scene = new Scene_Base();\n"
        "String(scene instanceof PIXI.Container)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    shutdown_full_engine(engine);
    PASS();
}

int main(void)
{
    snprintf(MOCK_GAME_DIR, sizeof(MOCK_GAME_DIR), "%s/rmmz_integration_test", test_tmp_root());

    printf("=== Title Screen Integration Tests ===\n");

    setup_mock_game();

    printf("\n-- Script Loading --\n");
    run_load_all_scripts();

    printf("\n-- Data Loading --\n");
    run_data_manager_loads_database();

    printf("\n-- Image Loading --\n");
    run_image_manager_loads_title_images();

    printf("\n-- Scene Transitions --\n");
    run_scene_boot_to_title_transition();

    printf("\n-- Title Screen Rendering --\n");
    run_title_screen_renders();
    run_graphics_initialization();

    printf("\n-- Game State --\n");
    run_game_objects_initialized();
    run_sprite_pixi_inheritance();

    printf("\n-- Plugin Compatibility --\n");
    run_plugin_errors_documented();

    printf("\n-- Full Integration --\n");
    run_full_boot_sequence();

    cleanup_mock_game();

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
