/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

/*
 * tests/test_plugins.c — Plugin compatibility test
 *
 * Loads the enabled plugins of a real RPG Maker MZ game against a mock core
 * with all shims in place and checks for fatal JS exceptions. Also covers the
 * shim infrastructure plugins rely on (document.currentScript, PIXI filter
 * stubs, fetch).
 */

#include "test_paths.h"
#include "engine/js_engine.h"
#include "engine/script_loader.h"
#include "io/file_io.h"
#include "bindings/bind_io.h"

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
#define MAX_MSG_LEN 2048

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

/* Test context: engine + shims */

static JSEngine *g_engine = NULL;
static const char *SHIM_DIR = "src/shims";
/* Scratch directory under the platform temp directory; filled in by main(). */
static char MOCK_DIR[TEST_PATH_MAX];
/* <game>/js/plugins, from RMMZ_TEST_GAME_DIR; filled in by main(). */
static char GAME_PLUGIN_DIR[TEST_PATH_MAX];

/* Mock RPG Maker MZ core: just enough of Utils, PluginManager, Graphics,
   ImageManager, etc. for plugins to read parameters and register. */
static const char *MOCK_RMMZ_CORE =
    "/* Mock RPG Maker MZ Core Classes for Plugin Testing */\n"
    "\n"
    "/* ---- Utils ---- */\n"
    "function Utils() {}\n"
    "Utils.RPGMAKER_NAME = 'MZ';\n"
    "Utils.RPGMAKER_VERSION = '1.6.0';\n"
    "Utils.isNwjs = function() { return typeof nw !== 'undefined'; };\n"
    "Utils.isMobileDevice = function() { return false; };\n"
    "Utils.canPlayOgg = function() { return true; };\n"
    "Utils.canPlayWebm = function() { return true; };\n"
    "Utils.isOptionValid = function(name) { return false; };\n"
    "Utils.canUseCssFontLoading = function() { return true; };\n"
    "\n"
    "/* ---- PluginManager ---- */\n"
    "function PluginManager() {}\n"
    "PluginManager._scripts = [];\n"
    "PluginManager._parameters = {};\n"
    "PluginManager._commands = {};\n"
    "PluginManager.setup = function(plugins) {\n"
    "  for (var i = 0; i < plugins.length; i++) {\n"
    "    var plugin = plugins[i];\n"
    "    if (plugin.status) {\n"
    "      this._scripts.push(plugin.name);\n"
    "      this._parameters[plugin.name.toLowerCase()] = plugin.parameters;\n"
    "    }\n"
    "  }\n"
    "};\n"
    "PluginManager.parameters = function(name) {\n"
    "  return this._parameters[(name || '').toLowerCase()] || {};\n"
    "};\n"
    "PluginManager.registerCommand = function(pluginName, commandName, func) {\n"
    "  var key = pluginName + ':' + commandName;\n"
    "  this._commands[key] = func;\n"
    "};\n"
    "PluginManager.callCommand = function(caller, pluginName, commandName, args) {\n"
    "  var key = pluginName + ':' + commandName;\n"
    "  var func = this._commands[key];\n"
    "  if (func) func.call(caller, args);\n"
    "};\n"
    "\n"
    "/* ---- $plugins global (loaded from plugins.js normally) ---- */\n"
    "if (typeof $plugins === 'undefined') { var $plugins = []; }\n"
    "\n"
    "/* ---- Graphics ---- */\n"
    "function Graphics() {}\n"
    "Graphics._width = 816;\n"
    "Graphics._height = 624;\n"
    "Graphics._defaultScale = 1;\n"
    "Graphics._realScale = 1;\n"
    "Graphics._app = null;\n"
    "Graphics._canvas = null;\n"
    "Graphics.boxWidth = 816;\n"
    "Graphics.boxHeight = 624;\n"
    "Graphics.frameCount = 0;\n"
    "Graphics._requestFullScreen = function() {};\n"
    "Graphics._cancelFullScreen = function() {};\n"
    "Graphics._switchFullScreen = function() {};\n"
    "Graphics.width = 816;\n"
    "Graphics.height = 624;\n"
    "\n"
    "/* ---- ColorManager ---- */\n"
    "function ColorManager() {}\n"
    "ColorManager.textColor = function(n) { return '#ffffff'; };\n"
    "\n"
    "/* ---- ImageManager ---- */\n"
    "function ImageManager() {}\n"
    "ImageManager._cache = {};\n"
    "ImageManager.loadSystem = function(name) {\n"
    "  return { addLoadListener: function(fn) { fn(this); }, width: 4, height: 4, isReady: function() { return true; } };\n"
    "};\n"
    "ImageManager.loadPicture = function(name) {\n"
    "  return { addLoadListener: function(fn) { fn(this); }, width: 4, height: 4, isReady: function() { return true; } };\n"
    "};\n"
    "\n"
    "/* ---- AudioManager ---- */\n"
    "function AudioManager() {}\n"
    "AudioManager.playBgm = function() {};\n"
    "AudioManager.playBgs = function() {};\n"
    "AudioManager.playSe = function() {};\n"
    "AudioManager.playMe = function() {};\n"
    "AudioManager.stopBgm = function() {};\n"
    "AudioManager.stopBgs = function() {};\n"
    "AudioManager.stopSe = function() {};\n"
    "AudioManager.stopMe = function() {};\n"
    "AudioManager._bgmVolume = 100;\n"
    "AudioManager._bgsVolume = 100;\n"
    "AudioManager._meVolume = 100;\n"
    "AudioManager._seVolume = 100;\n"
    "\n"
    "/* ---- SoundManager ---- */\n"
    "function SoundManager() {}\n"
    "SoundManager.playCursor = function() {};\n"
    "SoundManager.playOk = function() {};\n"
    "SoundManager.playCancel = function() {};\n"
    "SoundManager.playBuzzer = function() {};\n"
    "\n"
    "/* ---- DataManager ---- */\n"
    "function DataManager() {}\n"
    "DataManager._databaseFiles = [];\n"
    "DataManager.loadDatabase = function() {};\n"
    "DataManager.isDatabaseLoaded = function() { return true; };\n"
    "DataManager.savefileInfo = function() { return null; };\n"
    "\n"
    "/* ---- ConfigManager ---- */\n"
    "function ConfigManager() {}\n"
    "ConfigManager._isLoaded = false;\n"
    "ConfigManager.bgmVolume = 100;\n"
    "ConfigManager.bgsVolume = 100;\n"
    "ConfigManager.meVolume = 100;\n"
    "ConfigManager.seVolume = 100;\n"
    "ConfigManager.load = function() { this._isLoaded = true; };\n"
    "ConfigManager.save = function() {};\n"
    "ConfigManager.readFlag = function(config, name, defaultValue) {\n"
    "  return (name in config) ? config[name] : defaultValue;\n"
    "};\n"
    "ConfigManager.readVolume = function(config, name) {\n"
    "  return (name in config) ? Number(config[name]) : 100;\n"
    "};\n"
    "ConfigManager.makeData = function() { return {}; };\n"
    "ConfigManager.applyData = function() {};\n"
    "\n"
    "/* ---- SceneManager ---- */\n"
    "function SceneManager() {}\n"
    "SceneManager._scene = null;\n"
    "SceneManager.goto = function(sceneClass) {};\n"
    "SceneManager.push = function(sceneClass) {};\n"
    "SceneManager.pop = function() {};\n"
    "SceneManager.updateMain = function() {};\n"
    "\n"
    "/* ---- Input ---- */\n"
    "function Input() {}\n"
    "Input.keyMapper = {};\n"
    "Input.gamepadMapper = {};\n"
    "Input.clear = function() {};\n"
    "Input.update = function() {};\n"
    "Input.isPressed = function() { return false; };\n"
    "Input.isTriggered = function() { return false; };\n"
    "Input.isRepeated = function() { return false; };\n"
    "\n"
    "/* ---- TouchInput ---- */\n"
    "function TouchInput() {}\n"
    "TouchInput.update = function() {};\n"
    "TouchInput.isPressed = function() { return false; };\n"
    "TouchInput.isTriggered = function() { return false; };\n"
    "TouchInput.isRepeated = function() { return false; };\n"
    "TouchInput.isCancelled = function() { return false; };\n"
    "\n"
    "/* ---- FontManager ---- */\n"
    "function FontManager() {}\n"
    "FontManager.load = function(family, filename) {};\n"
    "FontManager._urls = {};\n"
    "FontManager._states = {};\n"
    "\n"
    "/* ---- StorageManager ---- */\n"
    "function StorageManager() {}\n"
    "StorageManager.isLocalMode = function() { return true; };\n"
    "\n"
    "/* ---- Game objects ---- */\n"
    "function Game_Variables() { this._data = []; }\n"
    "Game_Variables.prototype.value = function(id) { return this._data[id] || 0; };\n"
    "Game_Variables.prototype.setValue = function(id, val) { this._data[id] = val; };\n"
    "var $gameVariables = new Game_Variables();\n"
    "\n"
    "function Game_Switches() { this._data = []; }\n"
    "Game_Switches.prototype.value = function(id) { return !!this._data[id]; };\n"
    "Game_Switches.prototype.setValue = function(id, val) { this._data[id] = val; };\n"
    "var $gameSwitches = new Game_Switches();\n"
    "\n"
    "function Game_System() { this._windowTone = [0,0,0,0]; }\n"
    "var $gameSystem = new Game_System();\n"
    "\n"
    "function Game_Map() {}\n"
    "Game_Map.prototype.tileWidth = function() { return 48; };\n"
    "Game_Map.prototype.tileHeight = function() { return 48; };\n"
    "Game_Map.prototype.setup = function() {};\n"
    "Game_Map.prototype.events = function() { return []; };\n"
    "Game_Map.prototype.event = function() { return null; };\n"
    "Game_Map.prototype.setupDynamicCommon = function() {};\n"
    "var $gameMap = new Game_Map();\n"
    "\n"
    "function Game_Player() {}\n"
    "Game_Player.prototype.reserveTransfer = function() {};\n"
    "Game_Player.prototype.center = function() {};\n"
    "var $gamePlayer = new Game_Player();\n"
    "\n"
    "function Game_Party() { this._items = {}; this._actors = [1]; }\n"
    "Game_Party.prototype.members = function() { return []; };\n"
    "Game_Party.prototype.hasItem = function() { return false; };\n"
    "Game_Party.prototype.numItems = function() { return 0; };\n"
    "Game_Party.prototype.gainItem = function() {};\n"
    "Game_Party.prototype.loseItem = function() {};\n"
    "var $gameParty = new Game_Party();\n"
    "\n"
    "function Game_Interpreter() { this._list = []; this._index = 0; }\n"
    "Game_Interpreter.prototype.setup = function(list, eventId) { this._list = list || []; };\n"
    "Game_Interpreter.prototype.update = function() {};\n"
    "Game_Interpreter.prototype.isRunning = function() { return false; };\n"
    "Game_Interpreter.prototype.pluginCommand = function() {};\n"
    "Game_Interpreter.prototype.command357 = function() { return true; };\n"
    "\n"
    "function Game_Actor() {}\n"
    "Game_Actor.prototype.skills = function() { return []; };\n"
    "var $gameActors = { actor: function() { return new Game_Actor(); } };\n"
    "\n"
    "/* ---- Data globals ---- */\n"
    "var $dataSystem = { advanced: { screenWidth: 816, screenHeight: 624 } };\n"
    "var $dataActors = [null];\n"
    "var $dataClasses = [null];\n"
    "var $dataItems = [null];\n"
    "var $dataWeapons = [null];\n"
    "var $dataArmors = [null];\n"
    "var $dataSkills = [null];\n"
    "var $dataStates = [null];\n"
    "var $dataEnemies = [null];\n"
    "var $dataTroops = [null];\n"
    "var $dataCommonEvents = [null];\n"
    "var $dataAnimations = [null];\n"
    "var $dataTilesets = [null];\n"
    "var $dataMapInfos = [null];\n"
    "var $dataMap = null;\n"
    "\n"
    "/* ---- Window (RMMZ base class wrapping PIXI) ---- */\n"
    "function Window() { this._width = 0; this._height = 0; this._windowskin = null;\n"
    "  this._frameSprite = { bitmap: null, setFrame: function(){} };\n"
    "  this._contentsSprite = { bitmap: null }; this.children = []; }\n"
    "Window.prototype.addChild = function(c) { this.children.push(c); };\n"
    "Window.prototype.removeChild = function() {};\n"
    "\n"
    "/* ---- Scene / Window base classes ---- */\n"
    "function Scene_Base() {}\n"
    "Scene_Base.prototype.initialize = function() {};\n"
    "Scene_Base.prototype.start = function() {};\n"
    "Scene_Base.prototype.update = function() {};\n"
    "\n"
    "function Scene_Boot() {}\n"
    "Scene_Boot.prototype = Object.create(Scene_Base.prototype);\n"
    "Scene_Boot.prototype.constructor = Scene_Boot;\n"
    "Scene_Boot.prototype.start = function() {};\n"
    "Scene_Boot.prototype.loadGameFonts = function() {};\n"
    "\n"
    "function Scene_Title() {}\n"
    "Scene_Title.prototype = Object.create(Scene_Base.prototype);\n"
    "Scene_Title.prototype.constructor = Scene_Title;\n"
    "\n"
    "function Scene_Map() {}\n"
    "Scene_Map.prototype = Object.create(Scene_Base.prototype);\n"
    "Scene_Map.prototype.constructor = Scene_Map;\n"
    "Scene_Map.prototype.shouldAutosave = function() { return false; };\n"
    "\n"
    "function Scene_Menu() {}\n"
    "Scene_Menu.prototype = Object.create(Scene_Base.prototype);\n"
    "Scene_Menu.prototype.constructor = Scene_Menu;\n"
    "\n"
    "function Scene_MenuBase() {}\n"
    "Scene_MenuBase.prototype = Object.create(Scene_Base.prototype);\n"
    "Scene_MenuBase.prototype.constructor = Scene_MenuBase;\n"
    "\n"
    "function Scene_File() {}\n"
    "Scene_File.prototype = Object.create(Scene_MenuBase.prototype);\n"
    "Scene_File.prototype.constructor = Scene_File;\n"
    "\n"
    "function Scene_Save() {}\n"
    "Scene_Save.prototype = Object.create(Scene_File.prototype);\n"
    "Scene_Save.prototype.constructor = Scene_Save;\n"
    "Scene_Save.prototype.executeSave = function() {};\n"
    "\n"
    "function Scene_Load() {}\n"
    "Scene_Load.prototype = Object.create(Scene_File.prototype);\n"
    "Scene_Load.prototype.constructor = Scene_Load;\n"
    "Scene_Load.prototype.onLoadSuccess = function() {};\n"
    "\n"
    "function Scene_Options() {}\n"
    "Scene_Options.prototype = Object.create(Scene_MenuBase.prototype);\n"
    "Scene_Options.prototype.constructor = Scene_Options;\n"
    "\n"
    "function Window_Base() {}\n"
    "Window_Base.prototype.initialize = function() {};\n"
    "Window_Base.prototype.drawText = function() {};\n"
    "Window_Base.prototype.drawTextEx = function() {};\n"
    "Window_Base.prototype.textWidth = function() { return 0; };\n"
    "Window_Base.prototype.contents = { fillRect: function(){}, clear: function(){} };\n"
    "Window_Base.prototype.convertEscapeCharacters = function(text) { return text; };\n"
    "Window_Base.prototype.lineHeight = function() { return 36; };\n"
    "Window_Base.prototype.standardPadding = function() { return 18; };\n"
    "Window_Base.prototype.padding = 18;\n"
    "\n"
    "function Window_Selectable() {}\n"
    "Window_Selectable.prototype = Object.create(Window_Base.prototype);\n"
    "Window_Selectable.prototype.constructor = Window_Selectable;\n"
    "\n"
    "function Window_Command() {}\n"
    "Window_Command.prototype = Object.create(Window_Selectable.prototype);\n"
    "Window_Command.prototype.constructor = Window_Command;\n"
    "\n"
    "function Window_Options() {}\n"
    "Window_Options.prototype = Object.create(Window_Command.prototype);\n"
    "Window_Options.prototype.constructor = Window_Options;\n"
    "Window_Options.prototype.addGeneralOptions = function() {};\n"
    "Window_Options.prototype.addVolumeOptions = function() {};\n"
    "Window_Options.prototype.addCommand = function() {};\n"
    "Window_Options.prototype.makeCommandList = function() {};\n"
    "\n"
    "function Window_MenuCommand() {}\n"
    "Window_MenuCommand.prototype = Object.create(Window_Command.prototype);\n"
    "Window_MenuCommand.prototype.constructor = Window_MenuCommand;\n"
    "Window_MenuCommand.prototype.makeCommandList = function() {};\n"
    "\n"
    "function Window_ChoiceList() {}\n"
    "Window_ChoiceList.prototype = Object.create(Window_Command.prototype);\n"
    "Window_ChoiceList.prototype.constructor = Window_ChoiceList;\n"
    "Window_ChoiceList.prototype.makeCommandList = function() {};\n"
    "\n"
    "function Window_SkillList() {}\n"
    "Window_SkillList.prototype = Object.create(Window_Selectable.prototype);\n"
    "Window_SkillList.prototype.constructor = Window_SkillList;\n"
    "Window_SkillList.prototype.drawSkillCost = function() {};\n"
    "\n"
    "function Window_NameInput() {}\n"
    "Window_NameInput.prototype = Object.create(Window_Selectable.prototype);\n"
    "Window_NameInput.prototype.constructor = Window_NameInput;\n"
    "\n"
    "function Window_SavefileList() {}\n"
    "Window_SavefileList.prototype = Object.create(Window_Selectable.prototype);\n"
    "Window_SavefileList.prototype.constructor = Window_SavefileList;\n"
    "\n"
    "/* ---- Sprite classes ---- */\n"
    "function Sprite() { this.filters = []; this.children = []; }\n"
    "Sprite.prototype.addChild = function(child) { this.children.push(child); };\n"
    "Sprite.prototype.removeChild = function() {};\n"
    "Sprite.prototype.destroy = function() {};\n"
    "Sprite.prototype.update = function() {};\n"
    "\n"
    "function Sprite_Character() {}\n"
    "Sprite_Character.prototype = Object.create(Sprite.prototype);\n"
    "Sprite_Character.prototype.constructor = Sprite_Character;\n"
    "\n"
    "function Sprite_Picture() {}\n"
    "Sprite_Picture.prototype = Object.create(Sprite.prototype);\n"
    "Sprite_Picture.prototype.constructor = Sprite_Picture;\n"
    "\n"
    "function Sprite_Damage() {}\n"
    "Sprite_Damage.prototype = Object.create(Sprite.prototype);\n"
    "Sprite_Damage.prototype.constructor = Sprite_Damage;\n"
    "Sprite_Damage.prototype.setup = function() {};\n"
    "\n"
    "function Sprite_Actor() {}\n"
    "Sprite_Actor.prototype = Object.create(Sprite.prototype);\n"
    "Sprite_Actor.prototype.constructor = Sprite_Actor;\n"
    "\n"
    "function Sprite_Enemy() {}\n"
    "Sprite_Enemy.prototype = Object.create(Sprite.prototype);\n"
    "Sprite_Enemy.prototype.constructor = Sprite_Enemy;\n"
    "\n"
    "function Spriteset_Map() {}\n"
    "Spriteset_Map.prototype = Object.create(Sprite.prototype);\n"
    "Spriteset_Map.prototype.constructor = Spriteset_Map;\n"
    "Spriteset_Map.prototype.createLowerLayer = function() {};\n"
    "\n"
    "function Spriteset_Battle() {}\n"
    "Spriteset_Battle.prototype = Object.create(Sprite.prototype);\n"
    "Spriteset_Battle.prototype.constructor = Spriteset_Battle;\n"
    "\n"
    "/* ---- Bitmap ---- */\n"
    "function Bitmap(width, height) {\n"
    "  this.width = width || 0;\n"
    "  this.height = height || 0;\n"
    "  this.smooth = true;\n"
    "  this._canvas = document.createElement('canvas');\n"
    "  this._context = this._canvas.getContext('2d');\n"
    "}\n"
    "Bitmap.prototype.addLoadListener = function(fn) { fn(this); };\n"
    "Bitmap.prototype.isReady = function() { return true; };\n"
    "\n"
    "/* ---- Game_Battler ---- */\n"
    "function Game_BattlerBase() {}\n"
    "Game_BattlerBase.prototype.isStateAffected = function() { return false; };\n"
    "Game_BattlerBase.prototype.canPaySkillCost = function() { return true; };\n"
    "Game_BattlerBase.prototype.paySkillCost = function() {};\n"
    "\n"
    "function Game_Battler() {}\n"
    "Game_Battler.prototype = Object.create(Game_BattlerBase.prototype);\n"
    "Game_Battler.prototype.constructor = Game_Battler;\n"
    "Game_Battler.prototype.regenerateHp = function() {};\n"
    "\n"
    "/* ---- Game_Event ---- */\n"
    "function Game_Event() { this._mapId = 1; this._eventId = 1; }\n"
    "Game_Event.prototype.event = function() { return { note: '', meta: {} }; };\n"
    "\n"
    "/* ---- Game_Message ---- */\n"
    "function Game_Message() {}\n"
    "var $gameMessage = new Game_Message();\n"
    "\n"
    "/* ---- Game_SelfSwitches ---- */\n"
    "function Game_SelfSwitches() { this._data = {}; }\n"
    "var $gameSelfSwitches = new Game_SelfSwitches();\n"
    "\n"
    "/* ---- Game_Picture ---- */\n"
    "function Game_Picture() {}\n"
    "\n"
    "/* ---- BattleManager ---- */\n"
    "function BattleManager() {}\n"
    "BattleManager._action = null;\n"
    "\n"
    "/* ---- WindowLayer ---- */\n"
    "function WindowLayer() {}\n"
    "\n"
    "/* ---- Tilemap (use var to avoid block-scoped function issues) ---- */\n"
    "if (typeof Tilemap === 'undefined') {\n"
    "  var Tilemap = function() {};\n"
    "}\n"
    "if (!Tilemap.Layer) {\n"
    "  Tilemap.Layer = function() {};\n"
    "  Tilemap.Layer.prototype.addRect = function() {};\n"
    "  Tilemap.Layer.prototype.clear = function() {};\n"
    "  Tilemap.Layer.prototype.render = function() {};\n"
    "}\n"
    "\n"
    "/* ---- WebAudio (used by MUSH_Audio_Engine) ---- */\n"
    "function WebAudio(url) { this._url = url; this._volume = 1; this._pitch = 1; }\n"
    "WebAudio.prototype.play = function() {};\n"
    "WebAudio.prototype.stop = function() {};\n"
    "WebAudio.prototype.fadeIn = function() {};\n"
    "WebAudio.prototype.fadeOut = function() {};\n"
    "WebAudio.prototype.isPlaying = function() { return false; };\n"
    "WebAudio.prototype.isReady = function() { return true; };\n"
    "WebAudio.prototype.addLoadListener = function(fn) { fn(this); };\n"
    "WebAudio.setMasterVolume = function(v) {};\n"
    "WebAudio._masterVolume = 1;\n"
    "Object.defineProperty(WebAudio.prototype, 'volume', {\n"
    "  get: function() { return this._volume; },\n"
    "  set: function(v) { this._volume = v; }\n"
    "});\n"
    "Object.defineProperty(WebAudio.prototype, 'pitch', {\n"
    "  get: function() { return this._pitch; },\n"
    "  set: function(v) { this._pitch = v; }\n"
    "});\n"
    "\n"
    "/* ---- Game objects (extended set for plugin compat) ---- */\n"
    "function Game_Temp() {}\n"
    "Game_Temp.prototype.setDestination = function() {};\n"
    "var $gameTemp = new Game_Temp();\n"
    "\n"
    "function Game_Screen() { this._tone = [0,0,0,0]; this._brightness = 255; }\n"
    "Game_Screen.prototype.startTint = function() {};\n"
    "Game_Screen.prototype.startFlash = function() {};\n"
    "var $gameScreen = new Game_Screen();\n"
    "\n"
    "function Game_CharacterBase() {}\n"
    "Game_CharacterBase.prototype.x = 0;\n"
    "Game_CharacterBase.prototype.y = 0;\n"
    "\n"
    "function Game_Character() {}\n"
    "Game_Character.prototype = Object.create(Game_CharacterBase.prototype);\n"
    "Game_Character.prototype.constructor = Game_Character;\n"
    "\n"
    "function Game_Enemy() {}\n"
    "Game_Enemy.prototype = Object.create(Game_Battler.prototype);\n"
    "Game_Enemy.prototype.constructor = Game_Enemy;\n"
    "\n"
    "function Game_Unit() { this._inBattle = false; }\n"
    "Game_Unit.prototype.members = function() { return []; };\n"
    "\n"
    "function Game_Troop() {}\n"
    "Game_Troop.prototype = Object.create(Game_Unit.prototype);\n"
    "Game_Troop.prototype.constructor = Game_Troop;\n"
    "var $gameTroop = new Game_Troop();\n"
    "\n"
    "function Game_Action() { this._item = {}; }\n"
    "Game_Action.prototype.setSubject = function() {};\n"
    "Game_Action.prototype.setAttack = function() {};\n"
    "\n"
    "function Game_ActionResult() { this.used = false; this.missed = false; }\n"
    "\n"
    "/* ---- Scene classes (extended set) ---- */\n"
    "function Scene_Battle() {}\n"
    "Scene_Battle.prototype = Object.create(Scene_Base.prototype);\n"
    "Scene_Battle.prototype.constructor = Scene_Battle;\n"
    "\n"
    "function Scene_Gameover() {}\n"
    "Scene_Gameover.prototype = Object.create(Scene_Base.prototype);\n"
    "Scene_Gameover.prototype.constructor = Scene_Gameover;\n"
    "\n"
    "function Scene_GameEnd() {}\n"
    "Scene_GameEnd.prototype = Object.create(Scene_MenuBase.prototype);\n"
    "Scene_GameEnd.prototype.constructor = Scene_GameEnd;\n"
    "\n"
    "function Scene_Message() {}\n"
    "Scene_Message.prototype = Object.create(Scene_Base.prototype);\n"
    "Scene_Message.prototype.constructor = Scene_Message;\n"
    "\n"
    "/* ---- Sprite classes (extended set) ---- */\n"
    "function Sprite_Clickable() {}\n"
    "Sprite_Clickable.prototype = Object.create(Sprite.prototype);\n"
    "Sprite_Clickable.prototype.constructor = Sprite_Clickable;\n"
    "Sprite_Clickable.prototype.onClick = function() {};\n"
    "\n"
    "function Sprite_Battler() {}\n"
    "Sprite_Battler.prototype = Object.create(Sprite_Clickable.prototype);\n"
    "Sprite_Battler.prototype.constructor = Sprite_Battler;\n"
    "\n"
    "function Sprite_AnimationMV() {}\n"
    "Sprite_AnimationMV.prototype = Object.create(Sprite.prototype);\n"
    "Sprite_AnimationMV.prototype.constructor = Sprite_AnimationMV;\n"
    "\n"
    "function Sprite_Gauge() {}\n"
    "Sprite_Gauge.prototype = Object.create(Sprite.prototype);\n"
    "Sprite_Gauge.prototype.constructor = Sprite_Gauge;\n"
    "\n"
    "function Sprite_Battleback() {}\n"
    "Sprite_Battleback.prototype = Object.create(Sprite.prototype);\n"
    "Sprite_Battleback.prototype.constructor = Sprite_Battleback;\n"
    "\n"
    "function Spriteset_Base() {}\n"
    "Spriteset_Base.prototype = Object.create(Sprite.prototype);\n"
    "Spriteset_Base.prototype.constructor = Spriteset_Base;\n"
    "\n"
    "function TilingSprite() { this.children = []; }\n"
    "TilingSprite.prototype = Object.create(Sprite.prototype);\n"
    "TilingSprite.prototype.constructor = TilingSprite;\n"
    "TilingSprite.prototype.move = function() {};\n"
    "\n"
    "/* ---- Window classes (extended set) ---- */\n"
    "function Window_StatusBase() {}\n"
    "Window_StatusBase.prototype = Object.create(Window_Selectable.prototype);\n"
    "Window_StatusBase.prototype.constructor = Window_StatusBase;\n"
    "\n"
    "function Window_ShopBuy() {}\n"
    "Window_ShopBuy.prototype = Object.create(Window_Selectable.prototype);\n"
    "Window_ShopBuy.prototype.constructor = Window_ShopBuy;\n"
    "Window_ShopBuy.prototype.goodsToItem = function() { return null; };\n"
    "\n"
    "function Window_Message() {}\n"
    "Window_Message.prototype = Object.create(Window_Base.prototype);\n"
    "Window_Message.prototype.constructor = Window_Message;\n"
    "Window_Message.prototype.startMessage = function() {};\n"
    "\n"
    "function Window_BattleLog() {}\n"
    "Window_BattleLog.prototype = Object.create(Window_Base.prototype);\n"
    "Window_BattleLog.prototype.constructor = Window_BattleLog;\n"
    "\n"
    "function Window_ActorCommand() {}\n"
    "Window_ActorCommand.prototype = Object.create(Window_Command.prototype);\n"
    "Window_ActorCommand.prototype.constructor = Window_ActorCommand;\n"
    "Window_ActorCommand.prototype.makeCommandList = function() {};\n"
    "\n"
    "function Window_NameBox() {}\n"
    "Window_NameBox.prototype = Object.create(Window_Base.prototype);\n"
    "Window_NameBox.prototype.constructor = Window_NameBox;\n"
    "\n"
    "function Window_TitleCommand() {}\n"
    "Window_TitleCommand.prototype = Object.create(Window_Command.prototype);\n"
    "Window_TitleCommand.prototype.constructor = Window_TitleCommand;\n"
    "Window_TitleCommand.prototype.makeCommandList = function() {};\n"
    "\n"
    "function Window_MenuStatus() {}\n"
    "Window_MenuStatus.prototype = Object.create(Window_StatusBase.prototype);\n"
    "Window_MenuStatus.prototype.constructor = Window_MenuStatus;\n"
    "\n"
    "/* ---- Scene_Shop, Scene_Item, Scene_Skill (needed by bunchastuff) ---- */\n"
    "function Scene_Item() {}\n"
    "Scene_Item.prototype = Object.create(Scene_MenuBase.prototype);\n"
    "Scene_Item.prototype.constructor = Scene_Item;\n"
    "Scene_Item.prototype.user = function() { return $gameParty.members()[0] || new Game_Actor(); };\n"
    "Scene_Item.prototype.item = function() { return null; };\n"
    "Scene_Item.prototype.onItemOk = function() {};\n"
    "\n"
    "function Scene_Shop() {}\n"
    "Scene_Shop.prototype = Object.create(Scene_MenuBase.prototype);\n"
    "Scene_Shop.prototype.constructor = Scene_Shop;\n"
    "Scene_Shop.prototype.sellingPrice = function() { return 0; };\n"
    "\n"
    "function Scene_Skill() {}\n"
    "Scene_Skill.prototype = Object.create(Scene_MenuBase.prototype);\n"
    "Scene_Skill.prototype.constructor = Scene_Skill;\n"
    "Scene_Skill.prototype.user = function() { return new Game_Actor(); };\n"
    "Scene_Skill.prototype.item = function() { return null; };\n"
    "\n"
    "/* ---- Window_NameInput static arrays (needed by deleteNameInput) ---- */\n"
    "Window_NameInput.LATIN1 = new Array(90).fill('');\n"
    "Window_NameInput.LATIN2 = new Array(90).fill('');\n"
    "\n"
    "/* ---- Array.contains polyfill (used by TLB_LimitedShopStock) ---- */\n"
    "if (!Array.prototype.contains) {\n"
    "  Array.prototype.contains = function(item) {\n"
    "    return this.indexOf(item) !== -1;\n"
    "  };\n"
    "}\n"
    "\n"
    "/* ---- Imported tracking ---- */\n"
    "var Imported = Imported || {};\n"
    "\n"
    "/* Set up $plugins so PluginManager.setup works */\n"
    "if (typeof $plugins !== 'undefined') {\n"
    "  PluginManager.setup($plugins);\n"
    "}\n"
;

/* Load a single plugin file with document.currentScript set, as the browser would. */
static bool load_plugin(JSEngine *engine, const char *plugin_path, const char *plugin_name)
{
    char set_script[1024];
    snprintf(set_script, sizeof(set_script),
             "__dom_setCurrentScript('js/plugins/%s.js');", plugin_name);
    js_engine_eval(engine, set_script, "<set-current-script>");

    bool ok = js_engine_eval_file(engine, plugin_path);
    js_engine_execute_pending_jobs(engine);

    js_engine_eval(engine, "__dom_setCurrentScript(null);", "<clear-current-script>");

    return ok;
}

/* Create engine, load shims, eval mock core. */
static bool setup_test_engine(void)
{
    g_engine = js_engine_init();
    if (!g_engine) return false;

    js_engine_set_console_callback(g_engine, console_capture_cb, NULL);

    /* I/O bindings are needed by the XHR and storage shims */
    JSContext *ctx = js_engine_get_context(g_engine);
    bind_io_register(ctx);

    if (!script_loader_load_shims(g_engine, SHIM_DIR)) {
        fprintf(stderr, "Failed to load shims\n");
        return false;
    }

    bool ok = js_engine_eval(g_engine, MOCK_RMMZ_CORE, "<mock-rmmz-core>");
    js_engine_execute_pending_jobs(g_engine);
    if (!ok) {
        fprintf(stderr, "Failed to eval mock RMMZ core\n");
        return false;
    }

    return true;
}

static void teardown_test_engine(void)
{
    if (g_engine) {
        js_engine_shutdown(g_engine);
        g_engine = NULL;
    }
}

/* Tests: Shim infrastructure */

TEST(document_currentScript_exists)
{
    char *r = js_engine_eval_string(g_engine,
        "typeof document.currentScript", "test");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "object") == 0);  /* null is typeof "object" */
    free(r);
    PASS();
}

TEST(dom_setCurrentScript_works)
{
    js_engine_eval(g_engine,
        "__dom_setCurrentScript('js/plugins/TestPlugin.js');", "test");
    char *r = js_engine_eval_string(g_engine,
        "document.currentScript ? document.currentScript.src : 'null'", "test");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "js/plugins/TestPlugin.js") == 0);
    free(r);

    js_engine_eval(g_engine, "__dom_setCurrentScript(null);", "test");
    r = js_engine_eval_string(g_engine,
        "document.currentScript === null ? 'cleared' : 'not cleared'", "test");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "cleared") == 0);
    free(r);
    PASS();
}

TEST(pixi_filter_stubs_exist)
{
    char *r = js_engine_eval_string(g_engine,
        "var filters = ['DisplacementFilter','BulgePinchFilter','RadialBlurFilter',"
        "'GodrayFilter','AsciiFilter','CrossHatchFilter','DotFilter','EmbossFilter',"
        "'ShockwaveFilter','TwistFilter','ZoomBlurFilter','NoiseFilter',"
        "'KawaseBlurFilter','OldFilmFilter','RGBSplitFilter','AdvancedBloomFilter',"
        "'AdjustmentFilter','PixelateFilter','CRTFilter','ReflectionFilter',"
        "'MotionBlurFilter','GlowFilter','BevelFilter','TiltShiftFilter','GlitchFilter'];\n"
        "var missing = [];\n"
        "for (var i = 0; i < filters.length; i++) {\n"
        "  if (!PIXI.filters[filters[i]]) missing.push(filters[i]);\n"
        "}\n"
        "missing.length === 0 ? 'all_present' : 'missing:' + missing.join(',');",
        "test");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "all_present") == 0);
    free(r);
    PASS();
}

TEST(pixi_filter_stubs_constructable)
{
    char *r = js_engine_eval_string(g_engine,
        "try {\n"
        "  var f = new PIXI.filters.DisplacementFilter();\n"
        "  (f && typeof f.enabled !== 'undefined') ? 'ok' : 'fail';\n"
        "} catch(e) { 'error:' + e.message; }",
        "test");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "ok") == 0);
    free(r);
    PASS();
}

TEST(fetch_shim_exists)
{
    char *r = js_engine_eval_string(g_engine,
        "typeof fetch", "test");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "function") == 0);
    free(r);
    PASS();
}

TEST(wayback_machine_wrappers_exist)
{
    char *r = js_engine_eval_string(g_engine,
        "(typeof _____WB$wombat$assign$function_____ === 'function' && "
        "typeof __WB_pmw !== 'undefined') ? 'ok' : 'missing'",
        "test");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "ok") == 0);
    free(r);
    PASS();
}

TEST(legacy_regexp_statics)
{
    /* Plugins read capture groups through RegExp.$1 after a String match. */
    char *r = js_engine_eval_string(g_engine,
        "(function() {"
        "  var d = '[RPG Maker MZ] [Version 1.10] [MainMenuCore]';"
        "  if (!d.match(/\\[Version[ ](.*?)\\]/i)) return 'no match';"
        "  if (Number(RegExp.$1) !== 1.10) return 'bad $1: ' + RegExp.$1;"
        "  if (RegExp.lastMatch !== '[Version 1.10]') return 'bad lastMatch';"
        "  if (RegExp.leftContext !== '[RPG Maker MZ] ') return 'bad leftContext';"
        "  'x-y'.replace(/(-)/, '+');"
        "  if (RegExp.$1 !== '-') return 'replace did not update $1';"
        "  if (RegExp.$2 !== '') return 'missing group should be empty';"
        "  return 'ok';"
        "})()",
        "test");
    ASSERT(r != NULL);
    if (strcmp(r, "ok") != 0) { printf("(%s) ", r); }
    ASSERT(strcmp(r, "ok") == 0);
    free(r);
    PASS();
}

TEST(mock_rmmz_core_works)
{
    char *r = js_engine_eval_string(g_engine,
        "(typeof Utils !== 'undefined' && "
        "typeof PluginManager !== 'undefined' && "
        "typeof Graphics !== 'undefined' && "
        "typeof ConfigManager !== 'undefined') ? 'ok' : 'fail'",
        "test");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "ok") == 0);
    free(r);
    PASS();
}

/* Tests: Individual plugin loading */

/* Load the game's plugins.js so $plugins and PluginManager.parameters() work. */
static bool load_plugins_config(void)
{
    char plugins_js[TEST_PATH_MAX];
    snprintf(plugins_js, sizeof(plugins_js), "%s/js/plugins.js", test_game_dir());
    if (!file_io_exists(plugins_js)) {
        fprintf(stderr, "plugins.js not found: %s\n", plugins_js);
        return false;
    }
    bool ok = js_engine_eval_file(g_engine, plugins_js);
    js_engine_execute_pending_jobs(g_engine);
    if (!ok) return false;

    js_engine_eval(g_engine, "PluginManager.setup($plugins);", "<setup-plugins>");
    js_engine_execute_pending_jobs(g_engine);
    return true;
}

/* Load each enabled plugin. Errors are tolerated (the mock core lacks some
   classes); the test only fails if the engine breaks or most plugins error. */
TEST(load_all_enabled_plugins)
{
    int loaded = 0;
    int with_errors = 0;
    int not_found = 0;
    char path[TEST_PATH_MAX];

    /* Enabled plugin names from $plugins, one per line, in load order. */
    char *names = js_engine_eval_string(g_engine,
        "$plugins.filter(function(p) { return p.status; })"
        "        .map(function(p) { return p.name; }).join('\\n')", "test");
    ASSERT(names != NULL);

    char *cursor = names;
    while (cursor && *cursor) {
        char *nl = strchr(cursor, '\n');
        if (nl) *nl = '\0';
        const char *name = cursor;
        cursor = nl ? nl + 1 : NULL;
        if (name[0] == '\0') continue;

        snprintf(path, sizeof(path), "%s/%s.js", GAME_PLUGIN_DIR, name);
        if (!file_io_exists(path)) {
            printf("\n    SKIP (not found): %s", name);
            not_found++;
            continue;
        }

        int pre_err_count = captured_count;
        bool ok = load_plugin(g_engine, path, name);
        if (!ok) {
            for (int j = pre_err_count; j < captured_count; j++) {
                if (strcmp(captured_msgs[j].level, "error") == 0) {
                    printf("\n    WARN [%s]: %.80s", name, captured_msgs[j].message);
                }
            }
            with_errors++;
        }
        loaded++;
    }
    free(names);

    printf("\n    Loaded: %d, with JS errors: %d, not found: %d. ", loaded, with_errors, not_found);

    /* Engine must still be functional after loading all plugins */
    char *r = js_engine_eval_string(g_engine, "'engine_ok'", "test");
    ASSERT(r != NULL);
    ASSERT(strcmp(r, "engine_ok") == 0);
    free(r);

    /* Most plugins should load without errors. */
    ASSERT(loaded == 0 || (loaded - with_errors) * 2 >= loaded);
    PASS();
}

/* No unexpected JS errors during plugin loading. Steam plugins
   (OrangeGreenworks, Cyclone-Steam) are expected to error without the
   Steamworks SDK and degrade gracefully. */
TEST(no_unexpected_js_errors)
{
    int unexpected_errors = 0;
    for (int i = 0; i < captured_count; i++) {
        if (strcmp(captured_msgs[i].level, "error") == 0) {
            /* Expected: Steam/Greenworks related */
            if (strstr(captured_msgs[i].message, "Greenworks") != NULL) continue;
            if (strstr(captured_msgs[i].message, "greenworks") != NULL) continue;
            if (strstr(captured_msgs[i].message, "Steam") != NULL) continue;
            if (strstr(captured_msgs[i].message, "OrangeGreenworks") != NULL) continue;
            if (strstr(captured_msgs[i].message, "Cyclone") != NULL) continue;
            if (strstr(captured_msgs[i].message, "CycloneSteam") != NULL) continue;
            /* Expected: QuickJS static property limitation hit by CyclonePatcher */
            if (strstr(captured_msgs[i].message, "no setter for property") != NULL) continue;
            /* Expected: require() stub warnings */
            if (strstr(captured_msgs[i].message, "unknown module") != NULL) continue;

            if (strstr(captured_msgs[i].message, "ReferenceError") != NULL ||
                strstr(captured_msgs[i].message, "TypeError") != NULL ||
                strstr(captured_msgs[i].message, "SyntaxError") != NULL) {
                printf("\n    UNEXPECTED: %.100s", captured_msgs[i].message);
                unexpected_errors++;
            }
        }
    }
    printf("\n    Unexpected JS errors: %d. ", unexpected_errors);
    ASSERT(unexpected_errors == 0);
    PASS();
}

/* Main */

int main(void)
{
    snprintf(MOCK_DIR, sizeof(MOCK_DIR), "%s/rmmz_plugin_test", test_tmp_root());

    printf("=== Plugin Compatibility Tests ===\n\n");

    printf("Setting up engine and shims...\n");
    if (!setup_test_engine()) {
        printf("FATAL: Failed to set up test engine\n");
        return 1;
    }

    printf("\n--- Shim Infrastructure ---\n");
    run_document_currentScript_exists();
    run_dom_setCurrentScript_works();
    run_pixi_filter_stubs_exist();
    run_pixi_filter_stubs_constructable();
    run_fetch_shim_exists();
    run_wayback_machine_wrappers_exist();
    run_legacy_regexp_statics();
    run_mock_rmmz_core_works();

    /* Real-game plugin tests need RMMZ_TEST_GAME_DIR; skip otherwise. */
    printf("\n--- Plugin Loading ---\n");
    if (!test_game_dir()) {
        printf("SKIP: RMMZ_TEST_GAME_DIR not set; real-game plugin tests skipped\n");
    } else {
        snprintf(GAME_PLUGIN_DIR, sizeof(GAME_PLUGIN_DIR), "%s/js/plugins", test_game_dir());
        if (!load_plugins_config()) {
            printf("WARNING: Failed to load plugins.js config, continuing with defaults\n");
        }
        console_capture_reset();
        run_load_all_enabled_plugins();
        run_no_unexpected_js_errors();
    }

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ===\n");

    teardown_test_engine();

    return tests_failed > 0 ? 1 : 0;
}
