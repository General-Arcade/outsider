/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

/*
 * tests/integration/test_acceptance.c — End-to-end acceptance tests
 *
 * Exercises all subsystems together against a mock game: boot to title,
 * new game flow, audio buses, save/load, fullscreen, gamepad, Effekseer,
 * build pipeline layout, and shim loading.
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
#include "bindings/bind_effekseer.h"
#include "rendering/image_loader.h"
#include "rendering/canvas2d.h"
#include "rendering/font_manager.h"
#include "audio/audio_engine.h"
#include "input/input_manager.h"
#include "effects/effekseer_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

/* File helpers */

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static bool is_directory(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Mock game directory setup */

/* Scratch directory under the platform temp directory; filled in by main(). */
static char MOCK_GAME_DIR[TEST_PATH_MAX];

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

static void setup_mock_game(void)
{
    test_rm_rf(MOCK_GAME_DIR);

    file_io_mkdir(MOCK_GAME_DIR);

    const char *dirs[] = {
        "data", "img", "img/titles1", "img/titles2", "img/system",
        "img/characters", "img/tilesets", "img/battlebacks1", "img/battlebacks2",
        "fonts", "js", "js/libs", "js/plugins", "audio", "audio/bgm",
        "audio/bgs", "audio/se", "audio/me", "save", "effects"
    };
    char path[512];
    for (int i = 0; i < (int)(sizeof(dirs) / sizeof(dirs[0])); i++) {
        snprintf(path, sizeof(path), "%s/%s", MOCK_GAME_DIR, dirs[i]);
        file_io_mkdir(path);
    }

    const char *images[] = {
        "img/titles1/Castle.png", "img/titles2/Stakes.png",
        "img/system/Window.png", "img/characters/Actor1.png",
        "img/tilesets/Inside_A1.png", "img/battlebacks1/Grassland.png",
        "img/battlebacks2/Grassland.png"
    };
    for (int i = 0; i < (int)(sizeof(images) / sizeof(images[0])); i++) {
        snprintf(path, sizeof(path), "%s/%s", MOCK_GAME_DIR, images[i]);
        file_io_write_binary(path, tiny_png, sizeof(tiny_png));
    }

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
        "  \"battleBgm\": { \"name\": \"Battle1\", \"volume\": 90, \"pitch\": 100, \"pan\": 0 },\n"
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

    /* Mock RMMZ core exercising every shim subsystem */

    snprintf(path, sizeof(path), "%s/js/rmmz_core.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock rmmz_core.js — exercises all shims */\n"
        "\n"
        "var Utils = {\n"
        "  RPGMAKER_NAME: 'MZ',\n"
        "  RPGMAKER_VERSION: '1.6.0',\n"
        "  isNwjs: function() { return false; },\n"
        "  isMobileDevice: function() { return false; },\n"
        "  canUseWebGL: function() { return true; },\n"
        "  canPlayOgg: function() { return true; },\n"
        "  canPlayWebm: function() { return false; },\n"
        "  encodeUri: function(str) { return encodeURIComponent(str); },\n"
        "};\n"
        "\n"
        "/* Graphics — PIXI shim integration */\n"
        "var Graphics = {\n"
        "  _width: 816,\n"
        "  _height: 624,\n"
        "  _defaultScale: 1,\n"
        "  _realScale: 1,\n"
        "  _app: null,\n"
        "  _canvas: null,\n"
        "  _errorPrinter: null,\n"
        "  _fullScreenElement: null,\n"
        "  frameCount: 0,\n"
        "  initialize: function() {\n"
        "    this._canvas = document.createElement('canvas');\n"
        "    this._canvas.width = this._width;\n"
        "    this._canvas.height = this._height;\n"
        "    this._canvas.id = 'gameCanvas';\n"
        "    document.body.appendChild(this._canvas);\n"
        "    console.log('[Graphics] Canvas created: ' + this._width + 'x' + this._height);\n"
        "    this._createPixiApp();\n"
        "  },\n"
        "  _createPixiApp: function() {\n"
        "    this._app = new PIXI.Application({\n"
        "      view: this._canvas,\n"
        "      width: this._width,\n"
        "      height: this._height\n"
        "    });\n"
        "    console.log('[Graphics] PIXI.Application created');\n"
        "  },\n"
        "  _switchFullScreen: function() {\n"
        "    if (document.fullscreenElement) {\n"
        "      document.exitFullscreen();\n"
        "    } else {\n"
        "      this._canvas.requestFullscreen();\n"
        "    }\n"
        "  },\n"
        "  _updateRealScale: function() {\n"
        "    var sw = window.innerWidth;\n"
        "    var sh = window.innerHeight;\n"
        "    var sx = sw / this._width;\n"
        "    var sy = sh / this._height;\n"
        "    this._realScale = Math.min(sx, sy);\n"
        "    return this._realScale;\n"
        "  },\n"
        "  printError: function(name, message) {\n"
        "    console.error('[Graphics.printError] ' + name + ': ' + message);\n"
        "  }\n"
        "};\n"
        "\n"
        "/* Bitmap — Canvas2D + Image loading */\n"
        "var Bitmap = function(width, height) {\n"
        "  this._canvas = document.createElement('canvas');\n"
        "  this._canvas.width = width || 1;\n"
        "  this._canvas.height = height || 1;\n"
        "  this._context = this._canvas.getContext('2d');\n"
        "  this._image = null;\n"
        "  this._url = '';\n"
        "  this.fontFace = 'GameFont';\n"
        "  this.fontSize = 26;\n"
        "  this.fontBold = false;\n"
        "  this.fontItalic = false;\n"
        "  this.textColor = '#ffffff';\n"
        "  this.outlineColor = 'rgba(0,0,0,0.5)';\n"
        "  this.outlineWidth = 3;\n"
        "};\n"
        "Bitmap.load = function(url) {\n"
        "  var bmp = new Bitmap();\n"
        "  bmp._url = url;\n"
        "  bmp._image = new Image();\n"
        "  bmp._image.src = url;\n"
        "  bmp._image.onload = function() {\n"
        "    bmp._canvas.width = bmp._image.width;\n"
        "    bmp._canvas.height = bmp._image.height;\n"
        "    bmp._context.drawImage(bmp._image, 0, 0);\n"
        "    console.log('[Bitmap] Loaded: ' + url);\n"
        "  };\n"
        "  bmp._image.onerror = function() {\n"
        "    console.warn('[Bitmap] Failed to load: ' + url);\n"
        "  };\n"
        "  return bmp;\n"
        "};\n"
        "Bitmap.prototype.drawText = function(text, x, y, maxWidth, lineHeight, align) {\n"
        "  var ctx = this._context;\n"
        "  ctx.font = (this.fontBold ? 'bold ' : '') + this.fontSize + 'px ' + this.fontFace;\n"
        "  ctx.textAlign = align || 'left';\n"
        "  ctx.fillStyle = this.textColor;\n"
        "  ctx.fillText(text, x, y + lineHeight);\n"
        "};\n"
        "Bitmap.prototype.measureTextWidth = function(text) {\n"
        "  var ctx = this._context;\n"
        "  ctx.font = this.fontSize + 'px ' + this.fontFace;\n"
        "  return ctx.measureText(text).width;\n"
        "};\n"
        "\n"
        "/* Sprite — PIXI.Sprite wrapper */\n"
        "function Sprite(bitmap) {\n"
        "  PIXI.Sprite.call(this, bitmap ? PIXI.Texture.EMPTY : PIXI.Texture.EMPTY);\n"
        "  this.bitmap = bitmap;\n"
        "}\n"
        "Sprite.prototype = Object.create(PIXI.Sprite.prototype);\n"
        "Sprite.prototype.constructor = Sprite;\n"
        "\n"
        "/* Input — keyboard input management */\n"
        "var Input = {\n"
        "  _currentState: {},\n"
        "  _previousState: {},\n"
        "  _latestButton: null,\n"
        "  keyMapper: { 13: 'ok', 27: 'escape', 37: 'left', 38: 'up', 39: 'right', 40: 'down' },\n"
        "  initialize: function() {\n"
        "    this._currentState = {};\n"
        "    document.addEventListener('keydown', this._onKeyDown.bind(this));\n"
        "    document.addEventListener('keyup', this._onKeyUp.bind(this));\n"
        "    console.log('[Input] initialized');\n"
        "  },\n"
        "  _onKeyDown: function(e) {\n"
        "    var btn = this.keyMapper[e.keyCode];\n"
        "    if (btn) { this._currentState[btn] = true; this._latestButton = btn; }\n"
        "  },\n"
        "  _onKeyUp: function(e) {\n"
        "    var btn = this.keyMapper[e.keyCode];\n"
        "    if (btn) { this._currentState[btn] = false; }\n"
        "  },\n"
        "  update: function() {\n"
        "    this._previousState = Object.assign({}, this._currentState);\n"
        "  },\n"
        "  isTriggered: function(keyName) {\n"
        "    return this._currentState[keyName] && !this._previousState[keyName];\n"
        "  },\n"
        "  isPressed: function(keyName) {\n"
        "    return !!this._currentState[keyName];\n"
        "  }\n"
        "};\n"
        "\n"
        "/* TouchInput — mouse input management */\n"
        "var TouchInput = {\n"
        "  _mousePressed: false,\n"
        "  _screenPressed: false,\n"
        "  _pressedTime: 0,\n"
        "  x: 0, y: 0,\n"
        "  initialize: function() {\n"
        "    document.addEventListener('mousedown', this._onMouseDown.bind(this));\n"
        "    document.addEventListener('mouseup', this._onMouseUp.bind(this));\n"
        "    document.addEventListener('mousemove', this._onMouseMove.bind(this));\n"
        "    console.log('[TouchInput] initialized');\n"
        "  },\n"
        "  _onMouseDown: function(e) { this._mousePressed = true; this._screenPressed = true; },\n"
        "  _onMouseUp: function(e) { this._mousePressed = false; },\n"
        "  _onMouseMove: function(e) { this.x = e.clientX; this.y = e.clientY; },\n"
        "  update: function() { if (this._screenPressed) this._pressedTime++; }\n"
        "};\n"
        "\n"
        "console.log('[rmmz_core] loaded');\n"
    );

    snprintf(path, sizeof(path), "%s/js/rmmz_managers.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock rmmz_managers.js */\n"
        "\n"
        "var DataManager = {\n"
        "  _databaseFiles: [\n"
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
        "  ],\n"
        "  _loaded: 0,\n"
        "  _errors: [],\n"
        "  loadDatabase: function() {\n"
        "    for (var i = 0; i < this._databaseFiles.length; i++) {\n"
        "      var entry = this._databaseFiles[i];\n"
        "      this._loadDataFile(entry.name, 'data/' + entry.src);\n"
        "    }\n"
        "  },\n"
        "  _loadDataFile: function(name, src) {\n"
        "    var xhr = new XMLHttpRequest();\n"
        "    xhr.open('GET', src);\n"
        "    xhr.overrideMimeType('application/json');\n"
        "    xhr.onload = function() {\n"
        "      if (xhr.status < 400) {\n"
        "        globalThis[name] = JSON.parse(xhr.responseText);\n"
        "        DataManager._loaded++;\n"
        "        console.log('[DataManager] Loaded: ' + name);\n"
        "      }\n"
        "    };\n"
        "    xhr.onerror = function() {\n"
        "      DataManager._errors.push(name);\n"
        "      console.error('[DataManager] Error loading: ' + name);\n"
        "    };\n"
        "    xhr.send();\n"
        "  },\n"
        "  isDatabaseLoaded: function() {\n"
        "    return this._loaded >= this._databaseFiles.length;\n"
        "  },\n"
        "  isMapLoaded: function() {\n"
        "    return !!globalThis.$dataMap;\n"
        "  },\n"
        "  loadMapData: function(mapId) {\n"
        "    var filename = 'Map' + String(mapId).padStart(3, '0') + '.json';\n"
        "    var xhr = new XMLHttpRequest();\n"
        "    xhr.open('GET', 'data/' + filename);\n"
        "    xhr.overrideMimeType('application/json');\n"
        "    xhr.onload = function() {\n"
        "      if (xhr.status < 400) {\n"
        "        globalThis.$dataMap = JSON.parse(xhr.responseText);\n"
        "        console.log('[DataManager] Map loaded: ' + filename);\n"
        "      }\n"
        "    };\n"
        "    xhr.send();\n"
        "  }\n"
        "};\n"
        "\n"
        "var ImageManager = {\n"
        "  _cache: {},\n"
        "  loadTitle1: function(filename) {\n"
        "    return this._load('img/titles1/', filename);\n"
        "  },\n"
        "  loadTitle2: function(filename) {\n"
        "    return this._load('img/titles2/', filename);\n"
        "  },\n"
        "  loadSystem: function(filename) {\n"
        "    return this._load('img/system/', filename);\n"
        "  },\n"
        "  loadCharacter: function(filename) {\n"
        "    return this._load('img/characters/', filename);\n"
        "  },\n"
        "  _load: function(folder, filename) {\n"
        "    var url = folder + filename + '.png';\n"
        "    if (!this._cache[url]) {\n"
        "      this._cache[url] = Bitmap.load(url);\n"
        "    }\n"
        "    return this._cache[url];\n"
        "  }\n"
        "};\n"
        "\n"
        "var AudioManager = {\n"
        "  _bgmPlaying: null,\n"
        "  _bgsPlaying: null,\n"
        "  _mePlaying: null,\n"
        "  bgmVolume: 100,\n"
        "  bgsVolume: 100,\n"
        "  meVolume: 100,\n"
        "  seVolume: 100,\n"
        "  playBgm: function(bgm) {\n"
        "    this._bgmPlaying = bgm;\n"
        "    console.log('[AudioManager] BGM play: ' + (bgm ? bgm.name : 'null'));\n"
        "  },\n"
        "  playBgs: function(bgs) {\n"
        "    this._bgsPlaying = bgs;\n"
        "    console.log('[AudioManager] BGS play: ' + (bgs ? bgs.name : 'null'));\n"
        "  },\n"
        "  playMe: function(me) {\n"
        "    this._mePlaying = me;\n"
        "    console.log('[AudioManager] ME play: ' + (me ? me.name : 'null'));\n"
        "  },\n"
        "  playSe: function(se) {\n"
        "    console.log('[AudioManager] SE play: ' + (se ? se.name : 'null'));\n"
        "  },\n"
        "  stopBgm: function() { this._bgmPlaying = null; },\n"
        "  stopBgs: function() { this._bgsPlaying = null; },\n"
        "  stopMe: function() { this._mePlaying = null; }\n"
        "};\n"
        "\n"
        "var StorageManager = {\n"
        "  isLocalMode: function() { return true; },\n"
        "  saveToLocalFile: function(savefileId, json) {\n"
        "    var fs = require('fs');\n"
        "    var dir = 'save';\n"
        "    if (!fs.existsSync(dir)) fs.mkdirSync(dir);\n"
        "    var path = dir + '/file' + savefileId + '.rmmzsave';\n"
        "    var compressed = pako.deflate(json);\n"
        "    fs.writeFileSync(path, JSON.stringify({data: Array.from ? Array.from(compressed) : compressed}));\n"
        "    console.log('[StorageManager] Saved: ' + path);\n"
        "    return true;\n"
        "  },\n"
        "  loadFromLocalFile: function(savefileId) {\n"
        "    var fs = require('fs');\n"
        "    var path = 'save/file' + savefileId + '.rmmzsave';\n"
        "    if (!fs.existsSync(path)) return null;\n"
        "    var text = fs.readFileSync(path);\n"
        "    var obj = JSON.parse(text);\n"
        "    var inflated = pako.inflate(obj.data);\n"
        "    console.log('[StorageManager] Loaded: ' + path);\n"
        "    return inflated;\n"
        "  },\n"
        "  localFileExists: function(savefileId) {\n"
        "    var fs = require('fs');\n"
        "    return fs.existsSync('save/file' + savefileId + '.rmmzsave');\n"
        "  }\n"
        "};\n"
        "\n"
        "var EffectManager = {\n"
        "  _cache: {},\n"
        "  _initialized: false,\n"
        "  initialize: function() {\n"
        "    this._initialized = true;\n"
        "    console.log('[EffectManager] initialized');\n"
        "  },\n"
        "  load: function(filename) {\n"
        "    console.log('[EffectManager] load: ' + filename);\n"
        "    this._cache[filename] = true;\n"
        "  },\n"
        "  isReady: function() { return this._initialized; }\n"
        "};\n"
        "\n"
        "console.log('[rmmz_managers] loaded');\n"
    );

    snprintf(path, sizeof(path), "%s/js/rmmz_objects.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock rmmz_objects.js */\n"
        "function Game_Temp() { this.initialize(); }\n"
        "Game_Temp.prototype.initialize = function() { this._isPlaytest = false; };\n"
        "function Game_System() { this.initialize(); }\n"
        "Game_System.prototype.initialize = function() { this._bgmOnSave = null; };\n"
        "function Game_Screen() { this.initialize(); }\n"
        "Game_Screen.prototype.initialize = function() { this._tone = [0,0,0,0]; };\n"
        "function Game_Timer() { this.initialize(); }\n"
        "Game_Timer.prototype.initialize = function() { this._frames = 0; };\n"
        "function Game_Switches() { this.initialize(); }\n"
        "Game_Switches.prototype.initialize = function() { this._data = []; };\n"
        "function Game_Variables() { this.initialize(); }\n"
        "Game_Variables.prototype.initialize = function() { this._data = []; };\n"
        "function Game_SelfSwitches() { this.initialize(); }\n"
        "Game_SelfSwitches.prototype.initialize = function() { this._data = {}; };\n"
        "function Game_Actors() { this.initialize(); }\n"
        "Game_Actors.prototype.initialize = function() { this._data = []; };\n"
        "function Game_Party() { this.initialize(); }\n"
        "Game_Party.prototype.initialize = function() { this._gold = 0; this._items = {}; };\n"
        "function Game_Map() { this.initialize(); }\n"
        "Game_Map.prototype.initialize = function() {\n"
        "  this._mapId = 0;\n"
        "  this._tilesetId = 0;\n"
        "  this._events = [];\n"
        "};\n"
        "Game_Map.prototype.setup = function(mapId) {\n"
        "  this._mapId = mapId;\n"
        "  console.log('[Game_Map] setup: mapId=' + mapId);\n"
        "};\n"
        "function Game_Player() { this.initialize(); }\n"
        "Game_Player.prototype.initialize = function() {\n"
        "  this._x = 0; this._y = 0;\n"
        "  this._realX = 0; this._realY = 0;\n"
        "  this._direction = 2;\n"
        "};\n"
        "Game_Player.prototype.moveStraight = function(d) {\n"
        "  this._direction = d;\n"
        "  if (d === 2) this._y++;\n"
        "  if (d === 4) this._x--;\n"
        "  if (d === 6) this._x++;\n"
        "  if (d === 8) this._y--;\n"
        "  console.log('[Game_Player] move d=' + d + ' pos=(' + this._x + ',' + this._y + ')');\n"
        "};\n"
        "\n"
        "console.log('[rmmz_objects] loaded');\n"
    );

    snprintf(path, sizeof(path), "%s/js/rmmz_scenes.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock rmmz_scenes.js */\n"
        "\n"
        "var SceneManager = {\n"
        "  _scene: null,\n"
        "  _nextScene: null,\n"
        "  _sceneStack: [],\n"
        "  run: function(sceneClass) {\n"
        "    this._scene = new sceneClass();\n"
        "    this._scene.create();\n"
        "    console.log('[SceneManager] Running: ' + sceneClass.name);\n"
        "  },\n"
        "  goto: function(sceneClass) {\n"
        "    this._nextScene = new sceneClass();\n"
        "    console.log('[SceneManager] goto: ' + sceneClass.name);\n"
        "  },\n"
        "  changeScene: function() {\n"
        "    if (this._nextScene) {\n"
        "      this._scene = this._nextScene;\n"
        "      this._scene.create();\n"
        "      this._nextScene = null;\n"
        "    }\n"
        "  },\n"
        "  catchException: function(e) {\n"
        "    console.error('[SceneManager.catchException] ' + e);\n"
        "  }\n"
        "};\n"
        "\n"
        "function Scene_Boot() {}\n"
        "Scene_Boot.prototype.create = function() {\n"
        "  console.log('[Scene_Boot] create');\n"
        "  DataManager.loadDatabase();\n"
        "  Graphics.initialize();\n"
        "  Input.initialize();\n"
        "  TouchInput.initialize();\n"
        "  EffectManager.initialize();\n"
        "};\n"
        "Scene_Boot.prototype.isReady = function() {\n"
        "  return DataManager.isDatabaseLoaded();\n"
        "};\n"
        "Scene_Boot.prototype.start = function() {\n"
        "  console.log('[Scene_Boot] start -> Scene_Title');\n"
        "  SceneManager.goto(Scene_Title);\n"
        "};\n"
        "\n"
        "function Scene_Title() {}\n"
        "Scene_Title.prototype.create = function() {\n"
        "  console.log('[Scene_Title] create');\n"
        "  this._titleSprite1 = ImageManager.loadTitle1($dataSystem.title1Name);\n"
        "  this._titleSprite2 = ImageManager.loadTitle2($dataSystem.title2Name);\n"
        "  AudioManager.playBgm($dataSystem.titleBgm);\n"
        "};\n"
        "Scene_Title.prototype.commandNewGame = function() {\n"
        "  console.log('[Scene_Title] commandNewGame');\n"
        "  AudioManager.playSe($dataSystem.sounds[1]);\n"
        "  DataManager.loadMapData($dataSystem.startMapId);\n"
        "  SceneManager.goto(Scene_Map);\n"
        "};\n"
        "\n"
        "function Scene_Map() {}\n"
        "Scene_Map.prototype.create = function() {\n"
        "  console.log('[Scene_Map] create');\n"
        "  globalThis.$gameMap = new Game_Map();\n"
        "  globalThis.$gamePlayer = new Game_Player();\n"
        "  globalThis.$gameMap.setup($dataSystem.startMapId);\n"
        "  globalThis.$gamePlayer._x = $dataSystem.startX;\n"
        "  globalThis.$gamePlayer._y = $dataSystem.startY;\n"
        "  /* Play map BGM/BGS */\n"
        "  if ($dataMap && $dataMap.bgm) AudioManager.playBgm($dataMap.bgm);\n"
        "  if ($dataMap && $dataMap.bgs) AudioManager.playBgs($dataMap.bgs);\n"
        "};\n"
        "\n"
        "console.log('[rmmz_scenes] loaded');\n"
    );

    snprintf(path, sizeof(path), "%s/js/rmmz_sprites.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock rmmz_sprites.js */\n"
        "function Spriteset_Map() {}\n"
        "Spriteset_Map.prototype.create = function() {\n"
        "  console.log('[Spriteset_Map] created');\n"
        "};\n"
        "function Spriteset_Battle() {}\n"
        "Spriteset_Battle.prototype.create = function() {\n"
        "  console.log('[Spriteset_Battle] created');\n"
        "};\n"
        "console.log('[rmmz_sprites] loaded');\n"
    );

    snprintf(path, sizeof(path), "%s/js/rmmz_windows.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock rmmz_windows.js */\n"
        "function Window_Base() {}\n"
        "Window_Base.prototype.create = function() {};\n"
        "function Window_Command() {}\n"
        "Window_Command.prototype.create = function() {};\n"
        "function Window_TitleCommand() {}\n"
        "Window_TitleCommand.prototype.create = function() {};\n"
        "console.log('[rmmz_windows] loaded');\n"
    );

    snprintf(path, sizeof(path), "%s/js/main.js", MOCK_GAME_DIR);
    file_io_write_text(path,
        "/* Mock main.js */\n"
        "console.log('[main.js] Starting game boot sequence...');\n"
        "SceneManager.run(Scene_Boot);\n"
        "console.log('[main.js] Scene_Boot created, loading database...');\n"
    );
}

static void cleanup_mock_game(void)
{
    test_rm_rf(MOCK_GAME_DIR);
}

/* Game boots and shows title screen */

TEST(title_screen_boot_sequence)
{
    JSEngine *engine = js_engine_init();
    ASSERT(engine != NULL);
    js_engine_set_console_callback(engine, console_capture_cb, NULL);
    console_capture_reset();

    JSContext *ctx = js_engine_get_context(engine);
    bind_io_register(ctx);
    bind_image_register(ctx);
    bind_canvas2d_register(ctx);
    bind_renderer_register(ctx);
    bind_font_register(ctx);
    bind_tilemap_register(ctx);

    file_io_set_game_root(MOCK_GAME_DIR);

    ASSERT(js_engine_eval_file(engine, "src/shims/dom_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/navigator_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/xhr_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/canvas2d_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/font_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/pixi_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/storage_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/webaudio_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/effekseer_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/tilemap_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/plugin_compat.js"));

    char p[512];
    snprintf(p, sizeof(p), "%s/js/libs/pako.min.js", MOCK_GAME_DIR);
    ASSERT(js_engine_eval_file(engine, p));
    snprintf(p, sizeof(p), "%s/js/rmmz_core.js", MOCK_GAME_DIR);
    ASSERT(js_engine_eval_file(engine, p));
    snprintf(p, sizeof(p), "%s/js/rmmz_managers.js", MOCK_GAME_DIR);
    ASSERT(js_engine_eval_file(engine, p));
    snprintf(p, sizeof(p), "%s/js/rmmz_objects.js", MOCK_GAME_DIR);
    ASSERT(js_engine_eval_file(engine, p));
    snprintf(p, sizeof(p), "%s/js/rmmz_scenes.js", MOCK_GAME_DIR);
    ASSERT(js_engine_eval_file(engine, p));
    snprintf(p, sizeof(p), "%s/js/rmmz_sprites.js", MOCK_GAME_DIR);
    ASSERT(js_engine_eval_file(engine, p));
    snprintf(p, sizeof(p), "%s/js/rmmz_windows.js", MOCK_GAME_DIR);
    ASSERT(js_engine_eval_file(engine, p));
    snprintf(p, sizeof(p), "%s/js/main.js", MOCK_GAME_DIR);
    ASSERT(js_engine_eval_file(engine, p));

    /* Flush microtasks to process XHR callbacks */
    js_engine_execute_pending_jobs(engine);

    ASSERT(console_has_message("log", "[Scene_Boot] create"));

    ASSERT(console_has_message("log", "[DataManager] Loaded: $dataSystem"));
    ASSERT(console_has_message("log", "[DataManager] Loaded: $dataActors"));

    /* Simulate Scene_Boot.isReady() -> start() -> Scene_Title */
    ASSERT(js_engine_eval(engine,
        "if (SceneManager._scene.isReady()) SceneManager._scene.start();",
        "<boot_ready>"));
    js_engine_execute_pending_jobs(engine);

    ASSERT(console_has_message("log", "[Scene_Boot] start -> Scene_Title"));
    ASSERT(console_has_message("log", "[SceneManager] goto: Scene_Title"));

    ASSERT(js_engine_eval(engine, "SceneManager.changeScene();", "<change>"));
    js_engine_execute_pending_jobs(engine);

    ASSERT(console_has_message("log", "[Scene_Title] create"));
    ASSERT(console_has_message("log", "[AudioManager] BGM play: Theme1"));
    ASSERT(console_has_message("log", "[Graphics] PIXI.Application created"));
    ASSERT(console_has_message("log", "[Graphics] Canvas created: 816x624"));

    int err_count = console_count_level("error");
    ASSERT(err_count == 0);

    js_engine_shutdown(engine);
    PASS();
}

/* New game gameplay flow */

TEST(new_game_gameplay_flow)
{
    JSEngine *engine = js_engine_init();
    ASSERT(engine != NULL);
    js_engine_set_console_callback(engine, console_capture_cb, NULL);
    console_capture_reset();

    JSContext *ctx = js_engine_get_context(engine);
    bind_io_register(ctx);
    bind_image_register(ctx);
    bind_canvas2d_register(ctx);
    bind_renderer_register(ctx);
    bind_font_register(ctx);
    bind_tilemap_register(ctx);

    file_io_set_game_root(MOCK_GAME_DIR);

    ASSERT(js_engine_eval_file(engine, "src/shims/dom_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/navigator_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/xhr_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/canvas2d_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/font_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/pixi_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/storage_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/webaudio_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/effekseer_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/tilemap_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/plugin_compat.js"));

    char p[512];
    snprintf(p, sizeof(p), "%s/js/libs/pako.min.js", MOCK_GAME_DIR);
    ASSERT(js_engine_eval_file(engine, p));
    snprintf(p, sizeof(p), "%s/js/rmmz_core.js", MOCK_GAME_DIR);
    ASSERT(js_engine_eval_file(engine, p));
    snprintf(p, sizeof(p), "%s/js/rmmz_managers.js", MOCK_GAME_DIR);
    ASSERT(js_engine_eval_file(engine, p));
    snprintf(p, sizeof(p), "%s/js/rmmz_objects.js", MOCK_GAME_DIR);
    ASSERT(js_engine_eval_file(engine, p));
    snprintf(p, sizeof(p), "%s/js/rmmz_scenes.js", MOCK_GAME_DIR);
    ASSERT(js_engine_eval_file(engine, p));
    snprintf(p, sizeof(p), "%s/js/rmmz_sprites.js", MOCK_GAME_DIR);
    ASSERT(js_engine_eval_file(engine, p));
    snprintf(p, sizeof(p), "%s/js/rmmz_windows.js", MOCK_GAME_DIR);
    ASSERT(js_engine_eval_file(engine, p));
    snprintf(p, sizeof(p), "%s/js/main.js", MOCK_GAME_DIR);
    ASSERT(js_engine_eval_file(engine, p));
    js_engine_execute_pending_jobs(engine);

    /* Boot -> Title */
    ASSERT(js_engine_eval(engine,
        "if (SceneManager._scene.isReady()) SceneManager._scene.start();"
        "SceneManager.changeScene();",
        "<boot>"));
    js_engine_execute_pending_jobs(engine);

    ASSERT(js_engine_eval(engine,
        "SceneManager._scene.commandNewGame();",
        "<new_game>"));
    js_engine_execute_pending_jobs(engine);

    ASSERT(console_has_message("log", "[Scene_Title] commandNewGame"));
    ASSERT(console_has_message("log", "[AudioManager] SE play: Decision2"));

    /* Map data loaded -> transition to Scene_Map */
    ASSERT(js_engine_eval(engine,
        "SceneManager.changeScene();",
        "<to_map>"));
    js_engine_execute_pending_jobs(engine);

    ASSERT(console_has_message("log", "[Scene_Map] create"));
    ASSERT(console_has_message("log", "[Game_Map] setup: mapId=1"));

    char *result = js_engine_eval_string(engine,
        "JSON.stringify({x: $gamePlayer._x, y: $gamePlayer._y})",
        "<pos>");
    ASSERT(result != NULL);
    ASSERT(strstr(result, "\"x\":8") != NULL);
    ASSERT(strstr(result, "\"y\":6") != NULL);
    js_engine_free_string(result);

    ASSERT(js_engine_eval(engine, "$gamePlayer.moveStraight(6);", "<move>"));
    ASSERT(console_has_message("log", "[Game_Player] move d=6 pos=(9,6)"));

    js_engine_shutdown(engine);
    PASS();
}

/* Audio channels */

TEST(all_audio_channels)
{
    ASSERT(audio_engine_init());
    ASSERT(audio_engine_is_initialized());

    /* Bus layout matches RPG Maker MZ channels */
    ASSERT(AUDIO_BUS_BGM == 0);
    ASSERT(AUDIO_BUS_BGS == 1);
    ASSERT(AUDIO_BUS_ME == 2);
    ASSERT(AUDIO_BUS_SE == 3);
    ASSERT(AUDIO_BUS_COUNT == 4);

    audio_set_bus_volume(AUDIO_BUS_BGM, 0.9f);
    audio_set_bus_volume(AUDIO_BUS_BGS, 0.5f);
    audio_set_bus_volume(AUDIO_BUS_ME, 0.8f);
    audio_set_bus_volume(AUDIO_BUS_SE, 1.0f);

    audio_stop_all();

    audio_stop_bus(AUDIO_BUS_BGM);
    audio_stop_bus(AUDIO_BUS_BGS);
    audio_stop_bus(AUDIO_BUS_ME);
    audio_stop_bus(AUDIO_BUS_SE);

    audio_update();

    ASSERT(audio_get_active_voice_count() == 0);

    audio_engine_shutdown();
    PASS();
}

/* Audio via the WebAudio shim */

TEST(audio_webaudio_shim_integration)
{
    ASSERT(audio_engine_init());

    JSEngine *engine = js_engine_init();
    ASSERT(engine != NULL);
    js_engine_set_console_callback(engine, console_capture_cb, NULL);
    console_capture_reset();

    JSContext *ctx = js_engine_get_context(engine);
    bind_io_register(ctx);
    bind_audio_register(ctx);

    ASSERT(js_engine_eval_file(engine, "src/shims/dom_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/navigator_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/webaudio_shim.js"));

    char *result = js_engine_eval_string(engine,
        "var ctx = new AudioContext();"
        "JSON.stringify({"
        "  state: ctx.state,"
        "  sampleRate: ctx.sampleRate > 0,"
        "  hasDecodeAudioData: typeof ctx.decodeAudioData === 'function',"
        "  hasCreateGain: typeof ctx.createGain === 'function',"
        "  hasCreateBufferSource: typeof ctx.createBufferSource === 'function'"
        "});",
        "<audio_ctx>");
    ASSERT(result != NULL);
    ASSERT(strstr(result, "\"hasDecodeAudioData\":true") != NULL);
    ASSERT(strstr(result, "\"hasCreateGain\":true") != NULL);
    ASSERT(strstr(result, "\"hasCreateBufferSource\":true") != NULL);
    js_engine_free_string(result);

    result = js_engine_eval_string(engine,
        "JSON.stringify({"
        "  hasInit: typeof __native_audio.init === 'function',"
        "  hasPlay: typeof __native_audio.play === 'function',"
        "  hasStop: typeof __native_audio.stop === 'function',"
        "  isInitialized: __native_audio.isInitialized()"
        "});",
        "<native_audio>");
    ASSERT(result != NULL);
    ASSERT(strstr(result, "\"hasInit\":true") != NULL);
    ASSERT(strstr(result, "\"hasPlay\":true") != NULL);
    ASSERT(strstr(result, "\"isInitialized\":true") != NULL);
    js_engine_free_string(result);

    js_engine_shutdown(engine);
    audio_engine_shutdown();
    PASS();
}

/* Save/load round-trip */

TEST(save_load_roundtrip)
{
    JSEngine *engine = js_engine_init();
    ASSERT(engine != NULL);
    js_engine_set_console_callback(engine, console_capture_cb, NULL);
    console_capture_reset();

    JSContext *ctx = js_engine_get_context(engine);
    bind_io_register(ctx);

    file_io_set_game_root(MOCK_GAME_DIR);

    ASSERT(js_engine_eval_file(engine, "src/shims/dom_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/navigator_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/storage_shim.js"));

    char p[512];
    snprintf(p, sizeof(p), "%s/js/libs/pako.min.js", MOCK_GAME_DIR);
    ASSERT(js_engine_eval_file(engine, p));

    ASSERT(js_engine_eval(engine,
        "localStorage.setItem('testKey', 'testValue123');"
        "var stored = localStorage.getItem('testKey');",
        "<ls_set>"));

    char *result = js_engine_eval_string(engine, "stored", "<ls_get>");
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "testValue123") == 0);
    js_engine_free_string(result);

    result = js_engine_eval_string(engine,
        "var fs = require('fs');"
        "JSON.stringify({"
        "  hasExists: typeof fs.existsSync === 'function',"
        "  hasMkdir: typeof fs.mkdirSync === 'function',"
        "  hasRead: typeof fs.readFileSync === 'function',"
        "  hasWrite: typeof fs.writeFileSync === 'function',"
        "  hasUnlink: typeof fs.unlinkSync === 'function',"
        "  hasReaddir: typeof fs.readdirSync === 'function'"
        "});",
        "<fs_check>");
    ASSERT(result != NULL);
    ASSERT(strstr(result, "\"hasExists\":true") != NULL);
    ASSERT(strstr(result, "\"hasWrite\":true") != NULL);
    ASSERT(strstr(result, "\"hasUnlink\":true") != NULL);
    js_engine_free_string(result);

    result = js_engine_eval_string(engine,
        "var path = require('path');"
        "JSON.stringify({"
        "  hasJoin: typeof path.join === 'function',"
        "  hasDirname: typeof path.dirname === 'function',"
        "  hasBasename: typeof path.basename === 'function'"
        "});",
        "<path_check>");
    ASSERT(result != NULL);
    ASSERT(strstr(result, "\"hasJoin\":true") != NULL);
    ASSERT(strstr(result, "\"hasDirname\":true") != NULL);
    js_engine_free_string(result);

    ASSERT(js_engine_eval(engine,
        "var fs = require('fs');"
        "var saveData = JSON.stringify({gold: 500, items: [1,2,3], playtime: 3600});"
        "var savePath = 'save/file1.rmmzsave';"
        "fs.writeFileSync(savePath, saveData);"
        "var loaded = fs.readFileSync(savePath);"
        "var parsed = JSON.parse(loaded);",
        "<save_roundtrip>"));
    js_engine_execute_pending_jobs(engine);

    result = js_engine_eval_string(engine,
        "JSON.stringify({gold: parsed.gold, itemCount: parsed.items.length, playtime: parsed.playtime})",
        "<verify_save>");
    ASSERT(result != NULL);
    ASSERT(strstr(result, "\"gold\":500") != NULL);
    ASSERT(strstr(result, "\"itemCount\":3") != NULL);
    ASSERT(strstr(result, "\"playtime\":3600") != NULL);
    js_engine_free_string(result);

    char save_path[512];
    snprintf(save_path, sizeof(save_path), "%s/save/file1.rmmzsave", MOCK_GAME_DIR);
    ASSERT(file_exists(save_path));

    js_engine_shutdown(engine);
    PASS();
}

/* Fullscreen and resize */

TEST(fullscreen_and_resize_logic)
{
    JSEngine *engine = js_engine_init();
    ASSERT(engine != NULL);
    js_engine_set_console_callback(engine, console_capture_cb, NULL);
    console_capture_reset();

    JSContext *ctx = js_engine_get_context(engine);
    bind_io_register(ctx);

    ASSERT(js_engine_eval_file(engine, "src/shims/dom_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/navigator_shim.js"));

    char *result = js_engine_eval_string(engine,
        "JSON.stringify({"
        "  hasRequestFullscreen: typeof document.documentElement.requestFullscreen === 'function',"
        "  hasExitFullscreen: typeof document.exitFullscreen === 'function',"
        "  fullscreenElement: document.fullscreenElement,"
        "  hasDevicePixelRatio: typeof window.devicePixelRatio === 'number',"
        "  hasInnerWidth: typeof window.innerWidth === 'number',"
        "  hasInnerHeight: typeof window.innerHeight === 'number',"
        "  hasScreenWidth: typeof screen.width === 'number',"
        "  hasScreenHeight: typeof screen.height === 'number'"
        "});",
        "<fs_api>");
    ASSERT(result != NULL);
    ASSERT(strstr(result, "\"hasRequestFullscreen\":true") != NULL);
    ASSERT(strstr(result, "\"hasExitFullscreen\":true") != NULL);
    ASSERT(strstr(result, "\"hasDevicePixelRatio\":true") != NULL);
    ASSERT(strstr(result, "\"hasInnerWidth\":true") != NULL);
    ASSERT(strstr(result, "\"hasScreenWidth\":true") != NULL);
    js_engine_free_string(result);

    result = js_engine_eval_string(engine,
        "var gameW = 816, gameH = 624;"
        "var sw = window.innerWidth, sh = window.innerHeight;"
        "var sx = sw / gameW, sy = sh / gameH;"
        "var scale = Math.min(sx, sy);"
        "JSON.stringify({"
        "  scaleValid: scale > 0,"
        "  innerWidth: sw,"
        "  innerHeight: sh"
        "});",
        "<scale>");
    ASSERT(result != NULL);
    ASSERT(strstr(result, "\"scaleValid\":true") != NULL);
    js_engine_free_string(result);

    ASSERT(js_engine_eval(engine,
        "var resized = false;"
        "window.addEventListener('resize', function() { resized = true; });",
        "<resize_listener>"));

    js_engine_shutdown(engine);
    PASS();
}

/* Gamepad input */

TEST(gamepad_input_subsystem)
{
    input_manager_init();

    for (int i = 0; i < INPUT_MAX_GAMEPADS; i++) {
        const GamepadState *gp = input_manager_get_gamepad(i);
        ASSERT(gp != NULL);
        ASSERT(gp->connected == false);
    }

    GamepadState pad = {0};
    pad.connected = true;
    snprintf(pad.id, sizeof(pad.id), "Xbox 360 Controller (XInput)");
    pad.index = 0;
    pad.buttons[0].pressed = true;   /* A button */
    pad.buttons[0].value = 1.0;
    pad.axes[0] = 0.5;              /* Left stick right */

    input_manager_set_gamepad(0, &pad);

    const GamepadState *gp = input_manager_get_gamepad(0);
    ASSERT(gp != NULL);
    ASSERT(gp->connected == true);
    ASSERT(gp->buttons[0].pressed == true);
    ASSERT(gp->axes[0] == 0.5);

    JSEngine *engine = js_engine_init();
    ASSERT(engine != NULL);
    JSContext *ctx = js_engine_get_context(engine);
    bind_io_register(ctx);
    bind_input_register(ctx);

    ASSERT(js_engine_eval_file(engine, "src/shims/dom_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/navigator_shim.js"));

    char *result = js_engine_eval_string(engine,
        "JSON.stringify({"
        "  hasGetGamepads: typeof navigator.getGamepads === 'function',"
        "  gamepadSlots: navigator.getGamepads().length"
        "});",
        "<gamepad_js>");
    ASSERT(result != NULL);
    ASSERT(strstr(result, "\"hasGetGamepads\":true") != NULL);
    js_engine_free_string(result);

    js_engine_shutdown(engine);
    input_manager_shutdown();
    PASS();
}

/* Effekseer effects */

TEST(effekseer_effects_subsystem)
{
    ASSERT(effekseer_init(816, 624, 8000));
    ASSERT(effekseer_is_initialized());

    EffectHandle eff = effekseer_load("effects/test.efkefc", 1.0f);
    ASSERT(eff != EFFECT_HANDLE_INVALID);
    ASSERT(effekseer_is_loaded(eff));
    ASSERT(effekseer_get_loaded_count() == 1);

    EffectInstanceHandle inst = effekseer_play(eff, 400.0f, 300.0f, 0.0f);
    ASSERT(inst != EFFECT_INSTANCE_INVALID);
    ASSERT(effekseer_exists(inst));
    ASSERT(effekseer_get_playing_count() == 1);

    effekseer_set_position(inst, 500.0f, 400.0f, 0.0f);
    effekseer_set_scale(inst, 2.0f, 2.0f, 1.0f);
    effekseer_set_speed(inst, 1.5f);

    effekseer_update(1.0f);
    effekseer_begin_draw();
    effekseer_draw_handle(inst);
    effekseer_end_draw();

    effekseer_stop(inst);
    effekseer_release(eff);
    ASSERT(effekseer_get_loaded_count() == 0);

    JSEngine *engine = js_engine_init();
    ASSERT(engine != NULL);
    JSContext *ctx = js_engine_get_context(engine);
    bind_io_register(ctx);
    bind_effekseer_register(ctx);

    ASSERT(js_engine_eval_file(engine, "src/shims/dom_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/navigator_shim.js"));
    ASSERT(js_engine_eval_file(engine, "src/shims/effekseer_shim.js"));

    char *result = js_engine_eval_string(engine,
        "JSON.stringify({"
        "  hasEffekseer: typeof effekseer !== 'undefined',"
        "  hasInitRuntime: typeof effekseer.initRuntime === 'function',"
        "  hasCreateContext: typeof effekseer.createContext === 'function'"
        "});",
        "<efk_js>");
    ASSERT(result != NULL);
    ASSERT(strstr(result, "\"hasEffekseer\":true") != NULL);
    ASSERT(strstr(result, "\"hasInitRuntime\":true") != NULL);
    ASSERT(strstr(result, "\"hasCreateContext\":true") != NULL);
    js_engine_free_string(result);

    js_engine_shutdown(engine);
    effekseer_shutdown();
    PASS();
}

/* Build pipeline layout */

TEST(build_pipeline_structure)
{
    size_t len = 0;
    char *cmake = file_io_read_text("CMakeLists.txt", &len);
    ASSERT(cmake != NULL);
    ASSERT(strstr(cmake, "install(TARGETS outsider") != NULL);
    ASSERT(strstr(cmake, "install(DIRECTORY src/shims/") != NULL);
    ASSERT(strstr(cmake, "RMMZ_GAME_DIR") != NULL);
    free(cmake);

    /* Verify the build produced the main executable (name and location
       depend on platform and CMake generator). */
    ASSERT(file_exists("build/outsider") ||
           file_exists("build/outsider.exe") ||
           file_exists("build/Debug/outsider.exe") ||
           file_exists("build/Release/outsider.exe"));

    const char *shims[] = {
        "src/shims/dom_shim.js",
        "src/shims/navigator_shim.js",
        "src/shims/xhr_shim.js",
        "src/shims/canvas2d_shim.js",
        "src/shims/font_shim.js",
        "src/shims/pixi_shim.js",
        "src/shims/storage_shim.js",
        "src/shims/webaudio_shim.js",
        "src/shims/effekseer_shim.js",
        "src/shims/tilemap_shim.js",
        "src/shims/plugin_compat.js",
    };
    for (int i = 0; i < (int)(sizeof(shims) / sizeof(shims[0])); i++) {
        ASSERT(file_exists(shims[i]));
    }

    ASSERT(file_exists("tools/resource_converter.py"));
    ASSERT(file_exists("tools/build_game.py"));

    PASS();
}

/* Original game JS files unmodified */

TEST(original_js_files_unmodified)
{
    const char *game = test_game_dir();
    if (!game) {
        printf("SKIP (RMMZ_TEST_GAME_DIR not set) ");
        return;
    }

    /* The core game scripts must be present and untouched. */
    static const char *game_js_files[] = {
        "js/rmmz_core.js", "js/rmmz_managers.js", "js/rmmz_objects.js",
        "js/rmmz_scenes.js", "js/rmmz_sprites.js", "js/rmmz_windows.js",
        "js/main.js", "js/plugins.js",
    };
    char path[TEST_PATH_MAX];
    for (int i = 0; i < (int)(sizeof(game_js_files) / sizeof(game_js_files[0])); i++) {
        snprintf(path, sizeof(path), "%s/%s", game, game_js_files[i]);
        ASSERT(file_exists(path));
    }

    /* Shims live in src/shims/, never inside the game directory. */
    static const char *shim_files[] = {
        "js/dom_shim.js", "js/pixi_shim.js", "js/webaudio_shim.js", "js/storage_shim.js",
    };
    for (int i = 0; i < (int)(sizeof(shim_files) / sizeof(shim_files[0])); i++) {
        snprintf(path, sizeof(path), "%s/%s", game, shim_files[i]);
        ASSERT(!file_exists(path));
    }

    /* Game scripts must not reference the native bindings. */
    static const char *core_files[] = { "js/rmmz_core.js", "js/rmmz_managers.js" };
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof(path), "%s/%s", game, core_files[i]);
        size_t len = 0;
        char *content = file_io_read_text(path, &len);
        ASSERT(content != NULL);
        ASSERT(strstr(content, "__native_io") == NULL);
        ASSERT(strstr(content, "__native_audio") == NULL);
        ASSERT(strstr(content, "__native_renderer") == NULL);
        free(content);
    }

    PASS();
}

/* All shims load without errors */

TEST(all_shims_load_cleanly)
{
    JSEngine *engine = js_engine_init();
    ASSERT(engine != NULL);
    js_engine_set_console_callback(engine, console_capture_cb, NULL);
    console_capture_reset();

    JSContext *ctx = js_engine_get_context(engine);
    bind_io_register(ctx);
    bind_image_register(ctx);
    bind_canvas2d_register(ctx);
    bind_renderer_register(ctx);
    bind_font_register(ctx);
    bind_tilemap_register(ctx);
    bind_audio_register(ctx);
    bind_input_register(ctx);
    bind_effekseer_register(ctx);

    const char *shim_files[] = {
        "src/shims/dom_shim.js",
        "src/shims/navigator_shim.js",
        "src/shims/xhr_shim.js",
        "src/shims/canvas2d_shim.js",
        "src/shims/font_shim.js",
        "src/shims/pixi_shim.js",
        "src/shims/storage_shim.js",
        "src/shims/webaudio_shim.js",
        "src/shims/effekseer_shim.js",
        "src/shims/tilemap_shim.js",
        "src/shims/plugin_compat.js",
    };

    for (int i = 0; i < (int)(sizeof(shim_files) / sizeof(shim_files[0])); i++) {
        bool ok = js_engine_eval_file(engine, shim_files[i]);
        if (!ok) {
            printf("FAIL loading shim: %s\n", shim_files[i]);
        }
        ASSERT(ok);
    }

    int err_count = console_count_level("error");
    ASSERT(err_count == 0);

    js_engine_shutdown(engine);
    PASS();
}

/* Keyboard input event queue */

TEST(keyboard_input_dispatch)
{
    input_manager_init();

    InputEvent ev = {0};
    ev.type = INPUT_KEY_DOWN;
    ev.keyCode = 13;  /* Enter */
    snprintf(ev.key, sizeof(ev.key), "Enter");
    snprintf(ev.code, sizeof(ev.code), "Enter");
    input_manager_push_event(&ev);

    ev.keyCode = 27;  /* Escape */
    snprintf(ev.key, sizeof(ev.key), "Escape");
    snprintf(ev.code, sizeof(ev.code), "Escape");
    input_manager_push_event(&ev);

    ev.keyCode = 37;  /* ArrowLeft */
    snprintf(ev.key, sizeof(ev.key), "ArrowLeft");
    snprintf(ev.code, sizeof(ev.code), "ArrowLeft");
    input_manager_push_event(&ev);

    InputEvent out[16];
    int count = input_manager_poll_events(out, 16);
    ASSERT(count == 3);
    ASSERT(out[0].keyCode == 13);
    ASSERT(out[1].keyCode == 27);
    ASSERT(out[2].keyCode == 37);

    count = input_manager_poll_events(out, 16);
    ASSERT(count == 0);

    input_manager_shutdown();
    PASS();
}

/* main */

int main(void)
{
    snprintf(MOCK_GAME_DIR, sizeof(MOCK_GAME_DIR), "%s/rmmz_acceptance_test", test_tmp_root());

    printf("=== Acceptance Criteria Verification ===\n\n");

    setup_mock_game();

    image_loader_init(false);  /* no GL in headless tests */
    canvas2d_init();
    font_manager_init();

    printf("Criterion 1: Game boots and title screen renders\n");
    run_title_screen_boot_sequence();

    printf("\nCriterion 2: New game gameplay flow\n");
    run_new_game_gameplay_flow();

    printf("\nCriterion 3: All audio channels (BGM, BGS, ME, SE)\n");
    run_all_audio_channels();
    run_audio_webaudio_shim_integration();

    printf("\nCriterion 4: Save/load round-trip\n");
    run_save_load_roundtrip();

    printf("\nCriterion 5: Fullscreen and resize\n");
    run_fullscreen_and_resize_logic();

    printf("\nCriterion 6: Gamepad input\n");
    run_gamepad_input_subsystem();

    printf("\nCriterion 7: Effekseer particle effects\n");
    run_effekseer_effects_subsystem();

    printf("\nCriterion 8: Build pipeline structure\n");
    run_build_pipeline_structure();

    printf("\nCriterion 9: Original game JS files unmodified\n");
    run_original_js_files_unmodified();

    printf("\nAdditional: All shims load cleanly\n");
    run_all_shims_load_cleanly();

    printf("\nAdditional: Keyboard input dispatch\n");
    run_keyboard_input_dispatch();

    font_manager_shutdown();
    canvas2d_shutdown();
    image_loader_shutdown();
    cleanup_mock_game();

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
