/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

/*
 * tests/integration/test_gameplay.c — End-to-end gameplay flow
 *
 * Boot, title menu, new game, map movement, audio, and save/load, driven
 * through a mock game whose class stubs exercise the same shim paths as
 * the real RPG Maker MZ engine.
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
#include "bindings/bind_audio.h"
#include "bindings/bind_input.h"
#include "rendering/image_loader.h"
#include "rendering/canvas2d.h"
#include "rendering/font_manager.h"
#include "audio/audio_engine.h"
#include "input/input_manager.h"

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

#define MAX_CONSOLE_MSGS 512
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

    char path[512];
    snprintf(path, sizeof(path), "%s/data", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/img", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/img/titles1", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/img/titles2", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/img/system", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/img/characters", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/img/tilesets", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/fonts", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/js", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/js/libs", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/js/plugins", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/audio", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/audio/bgm", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/audio/bgs", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/audio/se", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/audio/me", MOCK_GAME_DIR); file_io_mkdir(path);
    snprintf(path, sizeof(path), "%s/save", MOCK_GAME_DIR); file_io_mkdir(path);

    /* Valid 4x4 RGBA PNG */
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
    snprintf(path, sizeof(path), "%s/img/characters/Actor1.png", MOCK_GAME_DIR);
    file_io_write_binary(path, tiny_png, sizeof(tiny_png));

    /* Data JSON files */

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
        "    { \"name\": \"Decision2\", \"volume\": 90, \"pitch\": 100, \"pan\": 0 },\n"
        "    { \"name\": \"Cancel2\", \"volume\": 90, \"pitch\": 100, \"pan\": 0 },\n"
        "    { \"name\": \"Buzzer1\", \"volume\": 90, \"pitch\": 100, \"pan\": 0 }\n"
        "  ],\n"
        "  \"startMapId\": 1,\n"
        "  \"startX\": 8,\n"
        "  \"startY\": 6,\n"
        "  \"partyMembers\": [1],\n"
        "  \"versionId\": 1\n"
        "}\n"
    );

    snprintf(path, sizeof(path), "%s/data/Actors.json", MOCK_GAME_DIR);
    file_io_write_text(path,
        "[null, {\"id\":1,\"name\":\"Harold\",\"nickname\":\"\",\"classId\":1,"
        "\"characterName\":\"Actor1\",\"characterIndex\":0,"
        "\"initialLevel\":1,\"maxLevel\":99,\"equips\":[1,1,0,0,0]}]\n"
    );

    snprintf(path, sizeof(path), "%s/data/Classes.json", MOCK_GAME_DIR);
    file_io_write_text(path,
        "[null, {\"id\":1,\"name\":\"Hero\",\"expParams\":[30,20,30,30]}]\n"
    );

    snprintf(path, sizeof(path), "%s/data/CommonEvents.json", MOCK_GAME_DIR);
    file_io_write_text(path, "[null]\n");

    snprintf(path, sizeof(path), "%s/data/Items.json", MOCK_GAME_DIR);
    file_io_write_text(path,
        "[null, {\"id\":1,\"name\":\"Potion\",\"price\":50,\"consumable\":true}]\n"
    );

    snprintf(path, sizeof(path), "%s/data/Skills.json", MOCK_GAME_DIR);
    file_io_write_text(path,
        "[null, {\"id\":1,\"name\":\"Attack\",\"mpCost\":0}]\n"
    );

    snprintf(path, sizeof(path), "%s/data/Weapons.json", MOCK_GAME_DIR);
    file_io_write_text(path,
        "[null, {\"id\":1,\"name\":\"Sword\",\"atk\":10}]\n"
    );

    snprintf(path, sizeof(path), "%s/data/Armors.json", MOCK_GAME_DIR);
    file_io_write_text(path,
        "[null, {\"id\":1,\"name\":\"Shield\",\"def\":5}]\n"
    );

    snprintf(path, sizeof(path), "%s/data/Enemies.json", MOCK_GAME_DIR);
    file_io_write_text(path,
        "[null, {\"id\":1,\"name\":\"Slime\",\"hp\":50}]\n"
    );

    snprintf(path, sizeof(path), "%s/data/Troops.json", MOCK_GAME_DIR);
    file_io_write_text(path,
        "[null, {\"id\":1,\"name\":\"Slime*2\",\"members\":[]}]\n"
    );

    snprintf(path, sizeof(path), "%s/data/States.json", MOCK_GAME_DIR);
    file_io_write_text(path,
        "[null, {\"id\":1,\"name\":\"Death\"}]\n"
    );

    snprintf(path, sizeof(path), "%s/data/Animations.json", MOCK_GAME_DIR);
    file_io_write_text(path, "[null]\n");

    snprintf(path, sizeof(path), "%s/data/Tilesets.json", MOCK_GAME_DIR);
    file_io_write_text(path,
        "[null, {\"id\":1,\"name\":\"World\",\"tilesetNames\":[\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\"],"
        "\"flags\":[]}]\n"
    );

    snprintf(path, sizeof(path), "%s/data/MapInfos.json", MOCK_GAME_DIR);
    file_io_write_text(path,
        "[null, {\"id\":1,\"name\":\"Map001\",\"parentId\":0}]\n"
    );

    snprintf(path, sizeof(path), "%s/data/Map001.json", MOCK_GAME_DIR);
    file_io_write_text(path,
        "{ \"width\": 17, \"height\": 13, \"data\": [], "
        "\"events\": [null], \"tilesetId\": 1, "
        "\"bgm\": { \"name\": \"Field1\", \"volume\": 80, \"pitch\": 100, \"pan\": 0 }, "
        "\"bgs\": { \"name\": \"Wind\", \"volume\": 50, \"pitch\": 100, \"pan\": 0 } }\n"
    );

    /* Stub game scripts */

    snprintf(path, sizeof(path), "%s/js/libs/pako.min.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "var pako = {\n"
        "  inflate: function(data) { return data; },\n"
        "  deflate: function(data) { return data; }\n"
        "};\n"
    );

    snprintf(path, sizeof(path), "%s/js/plugins.js", MOCK_GAME_DIR);
    file_io_write_text(path, "var $plugins = [];\n");

    /* Mock RPG Maker MZ classes with just enough of the real interfaces
       to drive boot, title, new game, map movement, save/load, audio, and input. */
    snprintf(path, sizeof(path), "%s/js/rmmz_core.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock rmmz_core.js */\n"
        "\n"
        "/* ---- Utils ---- */\n"
        "function Utils() {}\n"
        "Utils.RPGMAKER_NAME = 'MZ';\n"
        "Utils.RPGMAKER_VERSION = '1.6.0';\n"
        "Utils.isNwjs = function() { return typeof process !== 'undefined' && typeof process.versions !== 'undefined'; };\n"
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
        "Graphics._tickHandler = null;\n"
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
        "    } catch(e) {\n"
        "        console.error('[Graphics] PIXI error: ' + e.message);\n"
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
        "/* ---- Input (matches real RPG Maker MZ behavior) ---- */\n"
        "var Input = {};\n"
        "Input._currentState = {};\n"
        "Input._previousState = {};\n"
        "Input._triggeredState = {};\n"
        "Input._dir4 = 0;\n"
        "Input._dir8 = 0;\n"
        "Input._log = [];\n"
        "\n"
        "Input.keyMapper = {\n"
        "    9: 'tab', 13: 'ok', 27: 'escape', 32: 'ok',\n"
        "    37: 'left', 38: 'up', 39: 'right', 40: 'down',\n"
        "    33: 'pageup', 34: 'pagedown',\n"
        "    45: 'escape', 46: 'ok',\n"
        "    65: 'left', 68: 'right', 83: 'down', 87: 'up',\n"
        "    88: 'escape', 90: 'ok',\n"
        "    96: 'escape', 98: 'down', 100: 'left', 102: 'right', 104: 'up'\n"
        "};\n"
        "\n"
        "Input.initialize = function() {\n"
        "    this._setupEventHandlers();\n"
        "    console.log('[Input] Initialized');\n"
        "};\n"
        "\n"
        "Input._setupEventHandlers = function() {\n"
        "    document.addEventListener('keydown', this._onKeyDown.bind(this));\n"
        "    document.addEventListener('keyup', this._onKeyUp.bind(this));\n"
        "};\n"
        "\n"
        "Input._onKeyDown = function(event) {\n"
        "    var buttonName = this.keyMapper[event.keyCode];\n"
        "    if (buttonName) {\n"
        "        this._currentState[buttonName] = true;\n"
        "        this._log.push({ type: 'keydown', button: buttonName, keyCode: event.keyCode });\n"
        "    }\n"
        "};\n"
        "\n"
        "Input._onKeyUp = function(event) {\n"
        "    var buttonName = this.keyMapper[event.keyCode];\n"
        "    if (buttonName) {\n"
        "        this._currentState[buttonName] = false;\n"
        "        this._log.push({ type: 'keyup', button: buttonName, keyCode: event.keyCode });\n"
        "    }\n"
        "};\n"
        "\n"
        "Input.update = function() {\n"
        "    /* Detect triggered keys BEFORE copying previous state. */\n"
        "    /* A key is triggered if it is pressed now but was not last frame. */\n"
        "    this._triggeredState = {};\n"
        "    for (var key in this._currentState) {\n"
        "        if (this._currentState[key] && !this._previousState[key]) {\n"
        "            this._triggeredState[key] = true;\n"
        "        }\n"
        "    }\n"
        "    /* Update directional state */\n"
        "    this._dir4 = 0;\n"
        "    if (this._currentState['down'])  this._dir4 = 2;\n"
        "    if (this._currentState['left'])  this._dir4 = 4;\n"
        "    if (this._currentState['right']) this._dir4 = 6;\n"
        "    if (this._currentState['up'])    this._dir4 = 8;\n"
        "    /* Snapshot for next frame */\n"
        "    this._previousState = Object.assign({}, this._currentState);\n"
        "};\n"
        "\n"
        "Input.isPressed = function(keyName) {\n"
        "    return !!this._currentState[keyName];\n"
        "};\n"
        "\n"
        "Input.isTriggered = function(keyName) {\n"
        "    return !!this._triggeredState[keyName];\n"
        "};\n"
        "\n"
        "Input.isRepeated = function(keyName) {\n"
        "    return this.isPressed(keyName);\n"
        "};\n"
        "\n"
        "/* ---- TouchInput ---- */\n"
        "var TouchInput = {};\n"
        "TouchInput._x = 0;\n"
        "TouchInput._y = 0;\n"
        "TouchInput._mousePressed = false;\n"
        "TouchInput._triggered = false;\n"
        "TouchInput._log = [];\n"
        "\n"
        "TouchInput.initialize = function() {\n"
        "    this._setupEventHandlers();\n"
        "    console.log('[TouchInput] Initialized');\n"
        "};\n"
        "\n"
        "TouchInput._setupEventHandlers = function() {\n"
        "    document.addEventListener('mousedown', this._onMouseDown.bind(this));\n"
        "    document.addEventListener('mouseup', this._onMouseUp.bind(this));\n"
        "    document.addEventListener('mousemove', this._onMouseMove.bind(this));\n"
        "    document.addEventListener('wheel', this._onWheel.bind(this));\n"
        "};\n"
        "\n"
        "TouchInput._onMouseDown = function(event) {\n"
        "    this._mousePressed = true;\n"
        "    this._triggered = true;\n"
        "    this._x = event.clientX || event.pageX || 0;\n"
        "    this._y = event.clientY || event.pageY || 0;\n"
        "    this._log.push({ type: 'mousedown', x: this._x, y: this._y });\n"
        "};\n"
        "\n"
        "TouchInput._onMouseUp = function(event) {\n"
        "    this._mousePressed = false;\n"
        "    this._log.push({ type: 'mouseup' });\n"
        "};\n"
        "\n"
        "TouchInput._onMouseMove = function(event) {\n"
        "    this._x = event.clientX || event.pageX || 0;\n"
        "    this._y = event.clientY || event.pageY || 0;\n"
        "};\n"
        "\n"
        "TouchInput._onWheel = function(event) {\n"
        "    this._log.push({ type: 'wheel', deltaY: event.deltaY || 0 });\n"
        "};\n"
        "\n"
        "TouchInput.update = function() {\n"
        "    this._triggered = false;\n"
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
        "}\n"
        "\n"
        "Object.defineProperty(Bitmap.prototype, 'width', {\n"
        "    get: function() { return this._canvas ? this._canvas.width : 0; }\n"
        "});\n"
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
        "    };\n"
        "    bitmap._image.onerror = function() {\n"
        "        bitmap._loadingState = 'error';\n"
        "    };\n"
        "    bitmap._image.src = url;\n"
        "    return bitmap;\n"
        "};\n"
        "\n"
        "Bitmap.prototype.addLoadListener = function(listener) {\n"
        "    if (this._loadingState === 'loaded') listener(this);\n"
        "    else this._loadListeners.push(listener);\n"
        "};\n"
        "\n"
        "Bitmap.prototype._callLoadListeners = function() {\n"
        "    while (this._loadListeners.length > 0) {\n"
        "        this._loadListeners.shift()(this);\n"
        "    }\n"
        "};\n"
        "\n"
        "Bitmap.prototype.drawText = function(text, x, y) {\n"
        "    if (this._context) this._context.fillText(text, x, y + this.fontSize);\n"
        "};\n"
        "\n"
        "Bitmap.prototype.measureTextWidth = function(text) {\n"
        "    return this._context ? this._context.measureText(text).width : 0;\n"
        "};\n"
        "\n"
        "Bitmap.prototype.destroy = function() {\n"
        "    this._canvas = null;\n"
        "    this._context = null;\n"
        "};\n"
        "\n"
        "/* ---- Sprite (PIXI wrapper) ---- */\n"
        "function Sprite(bitmap) {\n"
        "    PIXI.Sprite.call(this);\n"
        "    this.bitmap = bitmap || null;\n"
        "}\n"
        "Sprite.prototype = Object.create(PIXI.Sprite.prototype);\n"
        "Sprite.prototype.constructor = Sprite;\n"
        "\n"
        "console.log('[rmmz_core] Core classes defined');\n"
    );

    snprintf(path, sizeof(path), "%s/js/rmmz_managers.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock rmmz_managers.js */\n"
        "\n"
        "/* ---- DataManager ---- */\n"
        "function DataManager() {}\n"
        "DataManager._errors = [];\n"
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
        "        this.loadDataFile(this._databaseFiles[i].name, this._databaseFiles[i].src);\n"
        "    }\n"
        "};\n"
        "\n"
        "DataManager.loadDataFile = function(name, src) {\n"
        "    var xhr = new XMLHttpRequest();\n"
        "    xhr.open('GET', 'data/' + src);\n"
        "    xhr.overrideMimeType('application/json');\n"
        "    xhr.onload = function() {\n"
        "        if (xhr.status < 400) {\n"
        "            globalThis[name] = JSON.parse(xhr.responseText);\n"
        "        } else {\n"
        "            DataManager._errors.push(src + ': HTTP ' + xhr.status);\n"
        "        }\n"
        "    };\n"
        "    xhr.onerror = function() {\n"
        "        DataManager._errors.push(src + ': network error');\n"
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
        "DataManager.loadMapData = function(mapId) {\n"
        "    var src = 'Map' + String(mapId).padStart(3, '0') + '.json';\n"
        "    this.loadDataFile('$dataMap', src);\n"
        "};\n"
        "\n"
        "DataManager.isMapLoaded = function() {\n"
        "    return $dataMap !== null;\n"
        "};\n"
        "\n"
        "DataManager.makeSaveContents = function() {\n"
        "    return {\n"
        "        system: $gameSystem ? $gameSystem.toSaveData() : {},\n"
        "        party: $gameParty ? $gameParty.toSaveData() : {},\n"
        "        player: $gamePlayer ? $gamePlayer.toSaveData() : {},\n"
        "        map: { mapId: $gameMap ? $gameMap._mapId : 0 }\n"
        "    };\n"
        "};\n"
        "\n"
        "DataManager.extractSaveContents = function(contents) {\n"
        "    if (contents.player && $gamePlayer) {\n"
        "        $gamePlayer._x = contents.player.x || 0;\n"
        "        $gamePlayer._y = contents.player.y || 0;\n"
        "    }\n"
        "};\n"
        "\n"
        "DataManager.maxSavefiles = function() { return 20; };\n"
        "\n"
        "/* ---- ImageManager ---- */\n"
        "function ImageManager() {}\n"
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
        "ImageManager.loadTitle1 = function(f) { return this.loadBitmap('img/titles1/', f); };\n"
        "ImageManager.loadTitle2 = function(f) { return this.loadBitmap('img/titles2/', f); };\n"
        "ImageManager.loadSystem = function(f) { return this.loadBitmap('img/system/', f); };\n"
        "ImageManager.loadCharacter = function(f) { return this.loadBitmap('img/characters/', f); };\n"
        "\n"
        "/* ---- AudioManager ---- */\n"
        "function AudioManager() {}\n"
        "AudioManager._bgmBuffer = null;\n"
        "AudioManager._bgsBuffer = null;\n"
        "AudioManager._seBuffers = [];\n"
        "AudioManager._log = [];\n"
        "\n"
        "AudioManager.playBgm = function(bgm) {\n"
        "    if (bgm && bgm.name) {\n"
        "        this._bgmBuffer = bgm;\n"
        "        this._log.push({ type: 'bgm', name: bgm.name, volume: bgm.volume });\n"
        "        console.log('[AudioManager] playBgm: ' + bgm.name);\n"
        "    }\n"
        "};\n"
        "\n"
        "AudioManager.playBgs = function(bgs) {\n"
        "    if (bgs && bgs.name) {\n"
        "        this._bgsBuffer = bgs;\n"
        "        this._log.push({ type: 'bgs', name: bgs.name, volume: bgs.volume });\n"
        "        console.log('[AudioManager] playBgs: ' + bgs.name);\n"
        "    }\n"
        "};\n"
        "\n"
        "AudioManager.playSe = function(se) {\n"
        "    if (se && se.name) {\n"
        "        this._seBuffers.push(se);\n"
        "        this._log.push({ type: 'se', name: se.name });\n"
        "        console.log('[AudioManager] playSe: ' + se.name);\n"
        "    }\n"
        "};\n"
        "\n"
        "AudioManager.playMe = function(me) {\n"
        "    if (me && me.name) {\n"
        "        this._log.push({ type: 'me', name: me.name });\n"
        "        console.log('[AudioManager] playMe: ' + me.name);\n"
        "    }\n"
        "};\n"
        "\n"
        "AudioManager.stopBgm = function() { this._bgmBuffer = null; };\n"
        "AudioManager.stopBgs = function() { this._bgsBuffer = null; };\n"
        "AudioManager.stopAll = function() { this._bgmBuffer = null; this._bgsBuffer = null; };\n"
        "\n"
        "AudioManager.playSystemSound = function(n) {\n"
        "    if ($dataSystem && $dataSystem.sounds && $dataSystem.sounds[n]) {\n"
        "        this.playSe($dataSystem.sounds[n]);\n"
        "    }\n"
        "};\n"
        "\n"
        "/* ---- StorageManager ---- */\n"
        "function StorageManager() {}\n"
        "StorageManager._forageKeys = [];\n"
        "\n"
        "StorageManager.isLocalMode = function() {\n"
        "    return Utils.isNwjs();\n"
        "};\n"
        "\n"
        "StorageManager.saveObject = function(saveName, object) {\n"
        "    var json = JSON.stringify(object);\n"
        "    return localforage.setItem(saveName, json);\n"
        "};\n"
        "\n"
        "StorageManager.loadObject = function(saveName) {\n"
        "    return localforage.getItem(saveName).then(function(json) {\n"
        "        return json ? JSON.parse(json) : null;\n"
        "    });\n"
        "};\n"
        "\n"
        "StorageManager.removeObject = function(saveName) {\n"
        "    return localforage.removeItem(saveName);\n"
        "};\n"
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
        "    if (sceneClass) this._nextScene = new sceneClass();\n"
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
        "        if (this._scene && this._scene.terminate) this._scene.terminate();\n"
        "        this._scene = this._nextScene;\n"
        "        this._nextScene = null;\n"
        "        this._sceneStarted = false;\n"
        "        if (this._scene.create) this._scene.create();\n"
        "        var name = this._scene.constructor.name || 'Unknown';\n"
        "        this._transitionLog.push(name);\n"
        "        console.log('[SceneManager] Scene: ' + name);\n"
        "    }\n"
        "};\n"
        "\n"
        "SceneManager._updateScene = function() {\n"
        "    if (this._scene) {\n"
        "        if (!this._sceneStarted) {\n"
        "            if (!this._scene.isReady || this._scene.isReady()) {\n"
        "                if (this._scene.start) this._scene.start();\n"
        "                this._sceneStarted = true;\n"
        "            }\n"
        "        }\n"
        "        if (this._sceneStarted && this._scene.update) {\n"
        "            this._scene.update();\n"
        "        }\n"
        "    }\n"
        "};\n"
        "\n"
        "console.log('[rmmz_managers] Manager classes defined');\n"
    );

    snprintf(path, sizeof(path), "%s/js/rmmz_objects.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock rmmz_objects.js */\n"
        "\n"
        "function Game_Temp() { this._isPlaytest = false; }\n"
        "\n"
        "function Game_System() { this._saveEnabled = true; }\n"
        "Game_System.prototype.toSaveData = function() {\n"
        "    return { saveEnabled: this._saveEnabled };\n"
        "};\n"
        "\n"
        "function Game_Map() {\n"
        "    this._mapId = 0;\n"
        "    this._tilesetId = 0;\n"
        "    this._events = [];\n"
        "}\n"
        "Game_Map.prototype.setup = function(mapId) {\n"
        "    this._mapId = mapId;\n"
        "    if ($dataMap) {\n"
        "        this._tilesetId = $dataMap.tilesetId;\n"
        "    }\n"
        "    console.log('[Game_Map] setup mapId=' + mapId);\n"
        "};\n"
        "Game_Map.prototype.update = function() {};\n"
        "Game_Map.prototype.autoplay = function() {\n"
        "    if ($dataMap && $dataMap.bgm && $dataMap.bgm.name) {\n"
        "        AudioManager.playBgm($dataMap.bgm);\n"
        "    }\n"
        "    if ($dataMap && $dataMap.bgs && $dataMap.bgs.name) {\n"
        "        AudioManager.playBgs($dataMap.bgs);\n"
        "    }\n"
        "};\n"
        "\n"
        "function Game_Player() {\n"
        "    this._x = 0;\n"
        "    this._y = 0;\n"
        "    this._direction = 2; /* down */\n"
        "    this._moveLog = [];\n"
        "}\n"
        "Game_Player.prototype.setupForNewGame = function() {\n"
        "    this._x = $dataSystem.startX;\n"
        "    this._y = $dataSystem.startY;\n"
        "    console.log('[Game_Player] New game at (' + this._x + ',' + this._y + ')');\n"
        "};\n"
        "Game_Player.prototype.update = function() {\n"
        "    this._moveByInput();\n"
        "};\n"
        "Game_Player.prototype._moveByInput = function() {\n"
        "    var dir = Input._dir4;\n"
        "    if (dir > 0) {\n"
        "        this._direction = dir;\n"
        "        var dx = 0, dy = 0;\n"
        "        if (dir === 2) dy = 1;  /* down */\n"
        "        if (dir === 4) dx = -1; /* left */\n"
        "        if (dir === 6) dx = 1;  /* right */\n"
        "        if (dir === 8) dy = -1; /* up */\n"
        "        this._x += dx;\n"
        "        this._y += dy;\n"
        "        this._moveLog.push({ dir: dir, x: this._x, y: this._y });\n"
        "    }\n"
        "};\n"
        "Game_Player.prototype.toSaveData = function() {\n"
        "    return { x: this._x, y: this._y, direction: this._direction };\n"
        "};\n"
        "\n"
        "function Game_Party() {\n"
        "    this._actors = [];\n"
        "    this._gold = 0;\n"
        "    this._items = {};\n"
        "}\n"
        "Game_Party.prototype.setupStartingMembers = function() {\n"
        "    if ($dataSystem && $dataSystem.partyMembers) {\n"
        "        this._actors = $dataSystem.partyMembers.slice();\n"
        "    }\n"
        "};\n"
        "Game_Party.prototype.toSaveData = function() {\n"
        "    return { actors: this._actors, gold: this._gold };\n"
        "};\n"
        "\n"
        "var $gameTemp = null;\n"
        "var $gameSystem = null;\n"
        "var $gameMap = null;\n"
        "var $gamePlayer = null;\n"
        "var $gameParty = null;\n"
        "\n"
        "console.log('[rmmz_objects] Game object classes defined');\n"
    );

    snprintf(path, sizeof(path), "%s/js/rmmz_scenes.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock rmmz_scenes.js */\n"
        "\n"
        "function Scene_Base() {\n"
        "    PIXI.Container.call(this);\n"
        "}\n"
        "Scene_Base.prototype = Object.create(PIXI.Container.prototype);\n"
        "Scene_Base.prototype.constructor = Scene_Base;\n"
        "Scene_Base.prototype.create = function() {};\n"
        "Scene_Base.prototype.isReady = function() { return true; };\n"
        "Scene_Base.prototype.start = function() {};\n"
        "Scene_Base.prototype.update = function() {};\n"
        "Scene_Base.prototype.terminate = function() {};\n"
        "\n"
        "/* ---- Scene_Boot ---- */\n"
        "function Scene_Boot() { Scene_Base.call(this); }\n"
        "Scene_Boot.prototype = Object.create(Scene_Base.prototype);\n"
        "Scene_Boot.prototype.constructor = Scene_Boot;\n"
        "\n"
        "Scene_Boot.prototype.create = function() {\n"
        "    DataManager.loadDatabase();\n"
        "};\n"
        "\n"
        "Scene_Boot.prototype.isReady = function() {\n"
        "    return DataManager.isDatabaseLoaded();\n"
        "};\n"
        "\n"
        "Scene_Boot.prototype.start = function() {\n"
        "    $gameTemp = new Game_Temp();\n"
        "    $gameSystem = new Game_System();\n"
        "    console.log('[Scene_Boot] Database loaded, going to title');\n"
        "    SceneManager.goto(Scene_Title);\n"
        "};\n"
        "\n"
        "/* ---- Scene_Title ---- */\n"
        "function Scene_Title() {\n"
        "    Scene_Base.call(this);\n"
        "    this._commandWindow = null;\n"
        "    this._started = false;\n"
        "}\n"
        "Scene_Title.prototype = Object.create(Scene_Base.prototype);\n"
        "Scene_Title.prototype.constructor = Scene_Title;\n"
        "\n"
        "Scene_Title.prototype.create = function() {\n"
        "    this._createBackground();\n"
        "    this._commandWindow = {\n"
        "        commands: ['New Game', 'Continue', 'Options'],\n"
        "        index: 0,\n"
        "        active: true\n"
        "    };\n"
        "};\n"
        "\n"
        "Scene_Title.prototype._createBackground = function() {\n"
        "    if ($dataSystem) {\n"
        "        var s1 = new Sprite();\n"
        "        s1.bitmap = ImageManager.loadTitle1($dataSystem.title1Name);\n"
        "        this.addChild(s1);\n"
        "        var s2 = new Sprite();\n"
        "        s2.bitmap = ImageManager.loadTitle2($dataSystem.title2Name);\n"
        "        this.addChild(s2);\n"
        "    }\n"
        "};\n"
        "\n"
        "Scene_Title.prototype.start = function() {\n"
        "    this._started = true;\n"
        "    if ($dataSystem && $dataSystem.titleBgm) {\n"
        "        AudioManager.playBgm($dataSystem.titleBgm);\n"
        "    }\n"
        "    console.log('[Scene_Title] Started');\n"
        "};\n"
        "\n"
        "Scene_Title.prototype.update = function() {\n"
        "    Input.update();\n"
        "    if (!this._commandWindow || !this._commandWindow.active) return;\n"
        "\n"
        "    /* Navigate command list with arrow keys */\n"
        "    if (Input.isTriggered('down')) {\n"
        "        this._commandWindow.index = Math.min(\n"
        "            this._commandWindow.index + 1,\n"
        "            this._commandWindow.commands.length - 1\n"
        "        );\n"
        "        AudioManager.playSystemSound(0); /* Cursor sound */\n"
        "        console.log('[Scene_Title] Cursor -> ' + this._commandWindow.commands[this._commandWindow.index]);\n"
        "    }\n"
        "    if (Input.isTriggered('up')) {\n"
        "        this._commandWindow.index = Math.max(this._commandWindow.index - 1, 0);\n"
        "        AudioManager.playSystemSound(0);\n"
        "    }\n"
        "\n"
        "    /* Select command with Enter */\n"
        "    if (Input.isTriggered('ok')) {\n"
        "        var cmd = this._commandWindow.commands[this._commandWindow.index];\n"
        "        AudioManager.playSystemSound(1); /* Decision sound */\n"
        "        console.log('[Scene_Title] Selected: ' + cmd);\n"
        "        if (cmd === 'New Game') {\n"
        "            this._commandNewGame();\n"
        "        }\n"
        "    }\n"
        "};\n"
        "\n"
        "Scene_Title.prototype._commandNewGame = function() {\n"
        "    DataManager.loadMapData($dataSystem.startMapId);\n"
        "    $gameMap = new Game_Map();\n"
        "    $gamePlayer = new Game_Player();\n"
        "    $gameParty = new Game_Party();\n"
        "    $gameParty.setupStartingMembers();\n"
        "    $gamePlayer.setupForNewGame();\n"
        "    console.log('[Scene_Title] New Game started');\n"
        "    SceneManager.goto(Scene_Map);\n"
        "};\n"
        "\n"
        "/* ---- Scene_Map ---- */\n"
        "function Scene_Map() {\n"
        "    Scene_Base.call(this);\n"
        "    this._mapLoaded = false;\n"
        "}\n"
        "Scene_Map.prototype = Object.create(Scene_Base.prototype);\n"
        "Scene_Map.prototype.constructor = Scene_Map;\n"
        "\n"
        "Scene_Map.prototype.isReady = function() {\n"
        "    return DataManager.isMapLoaded();\n"
        "};\n"
        "\n"
        "Scene_Map.prototype.start = function() {\n"
        "    this._mapLoaded = true;\n"
        "    $gameMap.setup($dataSystem.startMapId);\n"
        "    $gameMap.autoplay();\n"
        "    console.log('[Scene_Map] Started, map loaded');\n"
        "};\n"
        "\n"
        "Scene_Map.prototype.update = function() {\n"
        "    Input.update();\n"
        "    $gameMap.update();\n"
        "    $gamePlayer.update();\n"
        "};\n"
        "\n"
        "console.log('[rmmz_scenes] Scene classes defined');\n"
    );

    snprintf(path, sizeof(path), "%s/js/rmmz_sprites.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock rmmz_sprites.js */\n"
        "console.log('[rmmz_sprites] Sprite classes defined');\n"
    );

    snprintf(path, sizeof(path), "%s/js/rmmz_windows.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock rmmz_windows.js */\n"
        "function Window_Base() { PIXI.Container.call(this); }\n"
        "Window_Base.prototype = Object.create(PIXI.Container.prototype);\n"
        "Window_Base.prototype.constructor = Window_Base;\n"
        "console.log('[rmmz_windows] Window classes defined');\n"
    );

    snprintf(path, sizeof(path), "%s/js/main.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock main.js */\n"
        "Graphics.initialize();\n"
        "Input.initialize();\n"
        "TouchInput.initialize();\n"
        "SceneManager.run(Scene_Boot);\n"
        "console.log('[main.js] Boot sequence initiated');\n"
    );
}

static void cleanup_mock_game(void)
{
    test_rm_rf(MOCK_GAME_DIR);
}

/* Engine with all bindings registered */

static JSEngine *create_full_engine(void)
{
    JSEngine *engine = js_engine_init();
    if (!engine) return NULL;

    JSContext *ctx = js_engine_get_context(engine);

    bind_io_register(ctx);

    image_loader_init(false); /* headless, no GL */
    bind_image_register(ctx);

    canvas2d_init();
    bind_canvas2d_register(ctx);

    font_manager_init();
    bind_font_register(ctx);

    bind_renderer_register(ctx);
    bind_tilemap_register(ctx);

    audio_engine_init();
    bind_audio_register(ctx);

    input_manager_init();
    bind_input_register(ctx);

    return engine;
}

static void shutdown_full_engine(JSEngine *engine)
{
    input_manager_shutdown();
    audio_engine_shutdown();
    font_manager_shutdown();
    canvas2d_shutdown();
    image_loader_shutdown();
    js_engine_shutdown(engine);
}

static bool eval_ok(JSEngine *engine, const char *script)
{
    return js_engine_eval(engine, script, "<test>");
}

/* Caller frees the result. */
static char *eval_str(JSEngine *engine, const char *script)
{
    return js_engine_eval_string(engine, script, "<test>");
}

/* Simulate N frames: flush input events, timers, microtasks, and rAF. */
static void simulate_frames(JSEngine *engine, int count)
{
    for (int i = 0; i < count; i++) {
        eval_ok(engine, "__dom_flushInputEvents();");
        eval_ok(engine, "__dom_updateGamepads();");

        eval_ok(engine, "__dom_flushTimers();");
        js_engine_execute_pending_jobs(engine);

        char flush[128];
        snprintf(flush, sizeof(flush),
                 "__dom_flushAnimationFrames(%f);", (i + 1) * 16.67);
        eval_ok(engine, flush);

        js_engine_execute_pending_jobs(engine);
    }
}

static void inject_key_event(InputEventType type, int keyCode, const char *key, const char *code)
{
    InputEvent ev = {0};
    ev.type = type;
    ev.keyCode = keyCode;
    snprintf(ev.key, sizeof(ev.key), "%s", key);
    snprintf(ev.code, sizeof(ev.code), "%s", code);
    input_manager_push_event(&ev);
}

static void press_key(JSEngine *engine, int keyCode, const char *key, const char *code, int hold_frames)
{
    inject_key_event(INPUT_KEY_DOWN, keyCode, key, code);
    simulate_frames(engine, hold_frames > 0 ? hold_frames : 1);
    inject_key_event(INPUT_KEY_UP, keyCode, key, code);
    simulate_frames(engine, 1);
}

/* Boot -> title -> new game -> map */

TEST(boot_to_title_to_new_game)
{
    JSEngine *engine = create_full_engine();
    ASSERT(engine != NULL);

    js_engine_set_console_callback(engine, console_capture_cb, NULL);
    console_capture_reset();

    file_io_set_game_root(MOCK_GAME_DIR);

    bool ok = script_loader_load_all(engine, "src/shims", MOCK_GAME_DIR);
    ASSERT(ok);

    simulate_frames(engine, 15);

    char *result;
    result = eval_str(engine, "String(SceneManager._scene.constructor.name)");
    ASSERT(result && strcmp(result, "Scene_Title") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(SceneManager._scene._started)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    /* Enter selects "New Game" (index 0) */
    press_key(engine, 13, "Enter", "Enter", 1);

    simulate_frames(engine, 10);

    result = eval_str(engine, "JSON.stringify(SceneManager._transitionLog)");
    ASSERT(result != NULL);
    ASSERT(strstr(result, "Scene_Boot") != NULL);
    ASSERT(strstr(result, "Scene_Title") != NULL);
    ASSERT(strstr(result, "Scene_Map") != NULL);
    js_engine_free_string(result);

    result = eval_str(engine, "String(SceneManager._scene.constructor.name)");
    ASSERT(result && strcmp(result, "Scene_Map") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(SceneManager._scene._mapLoaded)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String($gameMap !== null)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String($gamePlayer !== null)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String($gameParty !== null)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String($gamePlayer._x)");
    ASSERT(result && strcmp(result, "8") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String($gamePlayer._y)");
    ASSERT(result && strcmp(result, "6") == 0);
    js_engine_free_string(result);

    ASSERT(console_has_message("log", "[Scene_Title] Selected: New Game"));
    ASSERT(console_has_message("log", "[Game_Player] New game at (8,6)"));

    shutdown_full_engine(engine);
    PASS();
}

/* BGM/BGS during title and gameplay */

TEST(audio_plays_during_title_and_gameplay)
{
    JSEngine *engine = create_full_engine();
    ASSERT(engine != NULL);

    js_engine_set_console_callback(engine, console_capture_cb, NULL);
    console_capture_reset();

    file_io_set_game_root(MOCK_GAME_DIR);

    bool ok = script_loader_load_all(engine, "src/shims", MOCK_GAME_DIR);
    ASSERT(ok);

    simulate_frames(engine, 15);

    ASSERT(console_has_message("log", "[AudioManager] playBgm: Theme1"));

    char *result;
    result = eval_str(engine,
        "String(AudioManager._log.some(function(e) {"
        "    return e.type === 'bgm' && e.name === 'Theme1';"
        "}))");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    press_key(engine, 13, "Enter", "Enter", 1);
    simulate_frames(engine, 15);

    /* Map BGM/BGS come from Map001.json */
    ASSERT(console_has_message("log", "[AudioManager] playBgm: Field1"));

    ASSERT(console_has_message("log", "[AudioManager] playBgs: Wind"));

    result = eval_str(engine,
        "String(AudioManager._log.some(function(e) {"
        "    return e.type === 'bgs' && e.name === 'Wind';"
        "}))");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    shutdown_full_engine(engine);
    PASS();
}

/* Keyboard menu navigation */

TEST(keyboard_navigates_menus)
{
    JSEngine *engine = create_full_engine();
    ASSERT(engine != NULL);

    js_engine_set_console_callback(engine, console_capture_cb, NULL);
    console_capture_reset();

    file_io_set_game_root(MOCK_GAME_DIR);

    bool ok = script_loader_load_all(engine, "src/shims", MOCK_GAME_DIR);
    ASSERT(ok);

    simulate_frames(engine, 15);

    char *result;

    result = eval_str(engine, "String(SceneManager._scene._commandWindow.index)");
    ASSERT(result && strcmp(result, "0") == 0);
    js_engine_free_string(result);

    press_key(engine, 40, "ArrowDown", "ArrowDown", 1);

    result = eval_str(engine, "String(SceneManager._scene._commandWindow.index)");
    ASSERT(result && strcmp(result, "1") == 0);
    js_engine_free_string(result);

    press_key(engine, 40, "ArrowDown", "ArrowDown", 1);

    result = eval_str(engine, "String(SceneManager._scene._commandWindow.index)");
    ASSERT(result && strcmp(result, "2") == 0);
    js_engine_free_string(result);

    press_key(engine, 38, "ArrowUp", "ArrowUp", 1);

    result = eval_str(engine, "String(SceneManager._scene._commandWindow.index)");
    ASSERT(result && strcmp(result, "1") == 0);
    js_engine_free_string(result);

    ASSERT(console_has_message("log", "[AudioManager] playSe: Cursor2"));

    result = eval_str(engine, "String(Input._log.length > 0)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    shutdown_full_engine(engine);
    PASS();
}

/* Character movement on map */

TEST(character_movement_on_map)
{
    JSEngine *engine = create_full_engine();
    ASSERT(engine != NULL);

    js_engine_set_console_callback(engine, console_capture_cb, NULL);
    console_capture_reset();

    file_io_set_game_root(MOCK_GAME_DIR);

    bool ok = script_loader_load_all(engine, "src/shims", MOCK_GAME_DIR);
    ASSERT(ok);

    simulate_frames(engine, 15);
    press_key(engine, 13, "Enter", "Enter", 1);
    simulate_frames(engine, 15);

    char *result;
    result = eval_str(engine, "String($gamePlayer._x + ',' + $gamePlayer._y)");
    ASSERT(result && strcmp(result, "8,6") == 0);
    js_engine_free_string(result);

    press_key(engine, 39, "ArrowRight", "ArrowRight", 1);

    result = eval_str(engine, "String($gamePlayer._x)");
    ASSERT(result != NULL);
    int x_after_right = atoi(result);
    js_engine_free_string(result);
    ASSERT(x_after_right == 9);

    press_key(engine, 40, "ArrowDown", "ArrowDown", 1);

    result = eval_str(engine, "String($gamePlayer._y)");
    ASSERT(result != NULL);
    int y_after_down = atoi(result);
    js_engine_free_string(result);
    ASSERT(y_after_down == 7);

    press_key(engine, 37, "ArrowLeft", "ArrowLeft", 1);

    result = eval_str(engine, "String($gamePlayer._x)");
    ASSERT(result != NULL);
    int x_after_left = atoi(result);
    js_engine_free_string(result);
    ASSERT(x_after_left == 8);

    press_key(engine, 38, "ArrowUp", "ArrowUp", 1);

    result = eval_str(engine, "String($gamePlayer._y)");
    ASSERT(result != NULL);
    int y_after_up = atoi(result);
    js_engine_free_string(result);
    ASSERT(y_after_up == 6);

    result = eval_str(engine, "String($gamePlayer._moveLog.length)");
    ASSERT(result != NULL);
    int moves = atoi(result);
    js_engine_free_string(result);
    ASSERT(moves == 4);

    shutdown_full_engine(engine);
    PASS();
}

/* Save/load round-trip */

TEST(save_load_roundtrip)
{
    JSEngine *engine = create_full_engine();
    ASSERT(engine != NULL);

    js_engine_set_console_callback(engine, console_capture_cb, NULL);
    console_capture_reset();

    file_io_set_game_root(MOCK_GAME_DIR);

    bool ok = script_loader_load_all(engine, "src/shims", MOCK_GAME_DIR);
    ASSERT(ok);

    simulate_frames(engine, 15);
    press_key(engine, 13, "Enter", "Enter", 1);
    simulate_frames(engine, 15);

    press_key(engine, 39, "ArrowRight", "ArrowRight", 1);
    press_key(engine, 39, "ArrowRight", "ArrowRight", 1);
    press_key(engine, 40, "ArrowDown", "ArrowDown", 1);

    char *result;
    result = eval_str(engine, "String($gamePlayer._x + ',' + $gamePlayer._y)");
    ASSERT(result && strcmp(result, "10,7") == 0);
    js_engine_free_string(result);

    eval_ok(engine,
        "var _saveResult = null;\n"
        "var _saveError = null;\n"
        "StorageManager.saveObject('file1', DataManager.makeSaveContents())\n"
        "    .then(function() { _saveResult = 'ok'; })\n"
        "    .catch(function(e) { _saveError = e.message || String(e); });\n"
    );

    simulate_frames(engine, 5);

    result = eval_str(engine, "String(_saveResult)");
    ASSERT(result && strcmp(result, "ok") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(_saveError)");
    ASSERT(result && strcmp(result, "null") == 0);
    js_engine_free_string(result);

    /* Reset position so the load visibly restores it */
    eval_ok(engine, "$gamePlayer._x = 0; $gamePlayer._y = 0;");

    result = eval_str(engine, "String($gamePlayer._x + ',' + $gamePlayer._y)");
    ASSERT(result && strcmp(result, "0,0") == 0);
    js_engine_free_string(result);

    eval_ok(engine,
        "var _loadResult = null;\n"
        "var _loadError = null;\n"
        "StorageManager.loadObject('file1')\n"
        "    .then(function(contents) {\n"
        "        DataManager.extractSaveContents(contents);\n"
        "        _loadResult = 'ok';\n"
        "    })\n"
        "    .catch(function(e) { _loadError = e.message || String(e); });\n"
    );

    simulate_frames(engine, 5);

    result = eval_str(engine, "String(_loadResult)");
    ASSERT(result && strcmp(result, "ok") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String($gamePlayer._x + ',' + $gamePlayer._y)");
    ASSERT(result && strcmp(result, "10,7") == 0);
    js_engine_free_string(result);

    eval_ok(engine, "StorageManager.removeObject('file1');");
    simulate_frames(engine, 3);

    shutdown_full_engine(engine);
    PASS();
}

/* SE on menu selection */

TEST(se_plays_on_selection)
{
    JSEngine *engine = create_full_engine();
    ASSERT(engine != NULL);

    js_engine_set_console_callback(engine, console_capture_cb, NULL);
    console_capture_reset();

    file_io_set_game_root(MOCK_GAME_DIR);

    bool ok = script_loader_load_all(engine, "src/shims", MOCK_GAME_DIR);
    ASSERT(ok);

    simulate_frames(engine, 15);

    press_key(engine, 40, "ArrowDown", "ArrowDown", 1);
    ASSERT(console_has_message("log", "[AudioManager] playSe: Cursor2"));

    press_key(engine, 38, "ArrowUp", "ArrowUp", 1);
    press_key(engine, 13, "Enter", "Enter", 1);

    ASSERT(console_has_message("log", "[AudioManager] playSe: Decision2"));

    char *result;
    result = eval_str(engine,
        "String(AudioManager._log.filter(function(e) { return e.type === 'se'; }).length)");
    ASSERT(result != NULL);
    int se_count = atoi(result);
    js_engine_free_string(result);
    ASSERT(se_count >= 3); /* cursor down, cursor up, decision */

    shutdown_full_engine(engine);
    PASS();
}

/* Full run: report console errors/warnings */

TEST(document_issues)
{
    JSEngine *engine = create_full_engine();
    ASSERT(engine != NULL);

    js_engine_set_console_callback(engine, console_capture_cb, NULL);
    console_capture_reset();

    file_io_set_game_root(MOCK_GAME_DIR);

    bool ok = script_loader_load_all(engine, "src/shims", MOCK_GAME_DIR);
    ASSERT(ok);

    simulate_frames(engine, 15);
    press_key(engine, 13, "Enter", "Enter", 1);
    simulate_frames(engine, 15);

    press_key(engine, 39, "ArrowRight", "ArrowRight", 1);
    press_key(engine, 40, "ArrowDown", "ArrowDown", 1);
    press_key(engine, 37, "ArrowLeft", "ArrowLeft", 1);
    press_key(engine, 38, "ArrowUp", "ArrowUp", 1);
    simulate_frames(engine, 5);

    int log_count = console_count_level("log");
    int warn_count = console_count_level("warn");
    int error_count = console_count_level("error");

    printf("(logs=%d warns=%d errors=%d) ", log_count, warn_count, error_count);

    if (error_count > 0) {
        printf("\n    Errors found during gameplay:\n");
        for (int i = 0; i < captured_count; i++) {
            if (strcmp(captured_msgs[i].level, "error") == 0) {
                printf("      - %s\n", captured_msgs[i].message);
            }
        }
    }

    if (warn_count > 0) {
        printf("\n    Warnings during gameplay:\n");
        for (int i = 0; i < captured_count && i < 10; i++) {
            if (strcmp(captured_msgs[i].level, "warn") == 0) {
                printf("      - %s\n", captured_msgs[i].message);
            }
        }
    }

    char *result;

    result = eval_str(engine, "String(Graphics._app !== null)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(Input._log.length > 0)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String(AudioManager._log.length > 0)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    result = eval_str(engine, "String($gamePlayer !== null && $gameMap !== null)");
    ASSERT(result && strcmp(result, "true") == 0);
    js_engine_free_string(result);

    shutdown_full_engine(engine);
    PASS();
}

/* main */

int main(void)
{
    snprintf(MOCK_GAME_DIR, sizeof(MOCK_GAME_DIR), "%s/rmmz_gameplay_test", test_tmp_root());

    printf("=== Gameplay Integration Tests ===\n");

    setup_mock_game();

    printf("\n-- Boot to New Game --\n");
    run_boot_to_title_to_new_game();

    printf("\n-- Audio Playback --\n");
    run_audio_plays_during_title_and_gameplay();

    printf("\n-- Keyboard Navigation --\n");
    run_keyboard_navigates_menus();

    printf("\n-- Character Movement --\n");
    run_character_movement_on_map();

    printf("\n-- Save/Load --\n");
    run_save_load_roundtrip();

    printf("\n-- Sound Effects --\n");
    run_se_plays_on_selection();

    printf("\n-- Issue Documentation --\n");
    run_document_issues();

    cleanup_mock_game();

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
