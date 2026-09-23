/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

/*
 * Storage shim: localStorage, localforage, Node-style require("fs"/"path"),
 * process and nw stubs, all backed by native file I/O under save/.
 */

(function() {
    "use strict";

    /* localStorage: persisted as a single JSON file. */

    var STORAGE_FILE = "save/localStorage.json";
    var _storageData = {};
    var _storageDirty = false;

    (function() {
        try {
            if (__native_io.existsSync(STORAGE_FILE)) {
                var raw = __native_io.readFileSync(STORAGE_FILE);
                if (raw) {
                    _storageData = JSON.parse(raw);
                }
            }
        } catch (e) {
            _storageData = {};
        }
    })();

    function _flushStorage() {
        if (!_storageDirty) return;
        try {
            if (!__native_io.existsSync("save")) {
                __native_io.mkdirSync("save");
            }
            __native_io.writeFileSync(STORAGE_FILE, JSON.stringify(_storageData));
            _storageDirty = false;
        } catch (e) {
            console.warn("localStorage: flush failed: " + e);
        }
    }

    var localStorageShim = {
        getItem: function(key) {
            if (key in _storageData) {
                return String(_storageData[key]);
            }
            return null;
        },

        setItem: function(key, value) {
            _storageData[String(key)] = String(value);
            _storageDirty = true;
            _flushStorage();
        },

        removeItem: function(key) {
            delete _storageData[String(key)];
            _storageDirty = true;
            _flushStorage();
        },

        clear: function() {
            _storageData = {};
            _storageDirty = true;
            _flushStorage();
        },

        key: function(index) {
            var keys = Object.keys(_storageData);
            return index < keys.length ? keys[index] : null;
        },

        get length() {
            return Object.keys(_storageData).length;
        }
    };

    globalThis.localStorage = localStorageShim;

    /* localforage: one file per key in save/, plus a key map for listing. */

    var KEYMAP_FILE = "save/_localforage_keymap.json";
    var _forageKeyMap = {};  /* sanitized filename suffix -> original key */

    (function() {
        try {
            if (__native_io.existsSync(KEYMAP_FILE)) {
                var raw = __native_io.readFileSync(KEYMAP_FILE);
                if (raw) {
                    _forageKeyMap = JSON.parse(raw);
                }
            }
        } catch (e) {
            _forageKeyMap = {};
        }
    })();

    function _flushKeyMap() {
        try {
            if (!__native_io.existsSync("save")) {
                __native_io.mkdirSync("save");
            }
            __native_io.writeFileSync(KEYMAP_FILE, JSON.stringify(_forageKeyMap));
        } catch (e) {
            console.warn("localforage: keymap flush failed: " + e);
        }
    }

    function _forageSanitize(key) {
        /* Injective encoding: unsafe chars (including "_") become _XXXX with
           fixed-width hex so encoded and literal characters cannot collide. */
        return String(key).replace(/[^a-zA-Z0-9.\-]/g, function(ch) {
            var code = ch.charCodeAt(0);
            return "_" + ("0000" + code.toString(16)).slice(-4);
        });
    }

    function _forageFilePath(key) {
        return "save/forage_" + _forageSanitize(key);
    }

    var localforageShim = {
        setItem: function(key, value) {
            return new Promise(function(resolve, reject) {
                try {
                    if (!__native_io.existsSync("save")) {
                        __native_io.mkdirSync("save");
                    }
                    var path = _forageFilePath(key);
                    __native_io.writeFileSync(path, String(value));
                    _forageKeyMap[_forageSanitize(key)] = String(key);
                    _flushKeyMap();
                    resolve(value);
                } catch (e) {
                    reject(e);
                }
            });
        },

        getItem: function(key) {
            return new Promise(function(resolve, reject) {
                try {
                    var path = _forageFilePath(key);
                    if (__native_io.existsSync(path)) {
                        var data = __native_io.readFileSync(path);
                        resolve(data);
                    } else {
                        resolve(null);
                    }
                } catch (e) {
                    reject(e);
                }
            });
        },

        removeItem: function(key) {
            return new Promise(function(resolve, reject) {
                try {
                    var path = _forageFilePath(key);
                    if (__native_io.existsSync(path)) {
                        __native_io.unlinkSync(path);
                    }
                    delete _forageKeyMap[_forageSanitize(key)];
                    _flushKeyMap();
                    resolve();
                } catch (e) {
                    reject(e);
                }
            });
        },

        keys: function() {
            return new Promise(function(resolve, reject) {
                try {
                    if (!__native_io.existsSync("save")) {
                        resolve([]);
                        return;
                    }
                    var entries = __native_io.readdirSync("save");
                    var prefix = "forage_";
                    var result = [];
                    for (var i = 0; i < entries.length; i++) {
                        if (entries[i].indexOf(prefix) === 0) {
                            var sanitized = entries[i].substring(prefix.length);
                            var original = _forageKeyMap[sanitized];
                            result.push(original !== undefined ? original : sanitized);
                        }
                    }
                    resolve(result);
                } catch (e) {
                    reject(e);
                }
            });
        },

        length: function() {
            return this.keys().then(function(keys) {
                return keys.length;
            });
        },

        clear: function() {
            return this.keys().then(function(keys) {
                var promises = [];
                for (var i = 0; i < keys.length; i++) {
                    promises.push(localforageShim.removeItem(keys[i]));
                }
                return Promise.all(promises);
            });
        }
    };

    globalThis.localforage = localforageShim;

    /* require("fs") */

    var fsModule = {
        existsSync: function(path) {
            return __native_io.existsSync(path);
        },

        mkdirSync: function(path, options) {
            __native_io.mkdirSync(path);
        },

        readFileSync: function(path, options) {
            /* The encoding option is accepted but files are always read as text. */
            var encoding = null;
            if (options && typeof options === "object") {
                encoding = options.encoding;
            } else if (typeof options === "string") {
                encoding = options;
            }
            return __native_io.readFileSync(path);
        },

        writeFileSync: function(path, data, options) {
            __native_io.writeFileSync(path, String(data));
        },

        unlinkSync: function(path) {
            __native_io.unlinkSync(path);
        },

        readdirSync: function(path) {
            return __native_io.readdirSync(path);
        },

        renameSync: function(oldPath, newPath) {
            __native_io.renameSync(oldPath, newPath);
        }
    };

    /* require("path") */

    var pathModule = {
        join: function() {
            var parts = [];
            for (var i = 0; i < arguments.length; i++) {
                var part = String(arguments[i]);
                if (part) parts.push(part);
            }
            var result = parts.join("/");
            result = result.replace(/\/+/g, "/");
            /* Like Node, a trailing separator survives: rmmz_core builds the
               save directory as path.join(base, "save/") and appends the
               file name to it. */
            return result;
        },

        dirname: function(p) {
            if (!p) return ".";
            p = String(p);
            var normalized = p.replace(/\\/g, "/");
            var lastSlash = normalized.lastIndexOf("/");
            if (lastSlash === -1) return ".";
            if (lastSlash === 0) return "/";
            return p.substring(0, lastSlash);
        },

        basename: function(p, ext) {
            if (!p) return "";
            p = String(p);
            var normalized = p.replace(/\\/g, "/");
            if (normalized.length > 1 && normalized[normalized.length - 1] === "/") {
                normalized = normalized.substring(0, normalized.length - 1);
            }
            var lastSlash = normalized.lastIndexOf("/");
            var name = lastSlash >= 0 ? normalized.substring(lastSlash + 1) : normalized;
            if (ext && name.length >= ext.length &&
                name.substring(name.length - ext.length) === ext) {
                name = name.substring(0, name.length - ext.length);
            }
            return name;
        },

        resolve: function() {
            var resolved = "";
            for (var i = arguments.length - 1; i >= 0; i--) {
                var part = String(arguments[i]);
                if (!part) continue;
                if (resolved) {
                    resolved = part + "/" + resolved;
                } else {
                    resolved = part;
                }
                if (part[0] === "/" || (part.length >= 2 && part[1] === ":")) {
                    break;
                }
            }
            resolved = resolved.replace(/\/+/g, "/");
            if (resolved.length > 1 && resolved[resolved.length - 1] === "/") {
                resolved = resolved.substring(0, resolved.length - 1);
            }
            return resolved;
        },

        sep: "/",
        delimiter: ":"
    };

    /* require() */

    /* nw.gui: RPG Maker MV asks NW.js for its window during start-up, and
       plugins reach for the same object to go fullscreen or quit. Methods with
       a native equivalent are wired to it; the rest are inert. Returning an
       empty stub instead would abort SceneManager.initNwjs at gui.Window.get().

       Utils.isNwjs() has to keep reporting true: MV picks where saves go from
       it, and only the NW.js answer writes save/file1.rpgsave next to the game
       the way the original player does. */
    var _nwWindow = {
        menu: null,
        title: "",
        width: 0,
        height: 0,
        x: 0,
        y: 0,
        isFullscreen: false,
        show: function() {},
        hide: function() {},
        focus: function() {},
        blur: function() {},
        maximize: function() {},
        unmaximize: function() {},
        minimize: function() {},
        restore: function() {},
        setAlwaysOnTop: function() {},
        setPosition: function() {},
        setShowInTaskbar: function() {},
        moveTo: function() {},
        resizeTo: function() {},
        showDevTools: function() {},
        closeDevTools: function() {},
        on: function() {},
        removeAllListeners: function() {},
        reload: function() {},
        enterFullscreen: function() {
            this.isFullscreen = true;
            if (typeof __native_platform !== "undefined") {
                __native_platform.setFullscreen(true);
            }
        },
        leaveFullscreen: function() {
            this.isFullscreen = false;
            if (typeof __native_platform !== "undefined") {
                __native_platform.setFullscreen(false);
            }
        },
        toggleFullscreen: function() {
            if (this.isFullscreen) this.leaveFullscreen();
            else this.enterFullscreen();
        },
        close: function() {
            if (typeof __native_platform !== "undefined") {
                __native_platform.quit();
            }
        }
    };

    function NwMenu() {
        this.items = [];
    }
    NwMenu.prototype.append = function(item) { this.items.push(item); };
    NwMenu.prototype.insert = function(item) { this.items.push(item); };
    NwMenu.prototype.remove = function() {};
    /* macOS-only in MV, and there is no menubar to build here. */
    NwMenu.prototype.createMacBuiltin = function() {};

    var nwGuiModule = {
        Window: {
            get: function() { return _nwWindow; }
        },
        Menu: NwMenu,
        MenuItem: function MenuItem(options) {
            Object.assign(this, options || {});
        },
        Shell: {
            openExternal: function() {},
            openItem: function() {}
        },
        App: {
            quit: function() { _nwWindow.close(); },
            closeAllWindows: function() { _nwWindow.close(); },
            argv: [],
            manifest: {}
        },
        Screen: { Init: function() {}, screens: [] }
    };

    var _modules = {
        "fs": fsModule,
        "path": pathModule,
        "nw.gui": nwGuiModule
    };

    globalThis.require = function(moduleName) {
        if (_modules[moduleName]) {
            return _modules[moduleName];
        }
        /* Unknown modules (e.g. "nw.gui") get an empty stub rather than throwing. */
        console.warn("require: unknown module '" + moduleName + "', returning stub");
        return {};
    };

    /* process: used by Utils.isNwjs() and StorageManager. */

    if (typeof globalThis.process === "undefined") {
        globalThis.process = {
            mainModule: {
                /* StorageManager.fileDirectoryPath() derives save/ from this. */
                filename: __native_io.getGameRoot() + "/index.html"
            },
            /* "win32" / "darwin" / "linux", as Node spells them. RPG Maker MV
               compares against 'darwin' when it builds the macOS menu bar, and
               plugins branch on it for path handling, so it has to be the real
               one rather than a fixed guess. */
            platform: (typeof __native_platform !== "undefined" &&
                       __native_platform.getOS)
                      ? __native_platform.getOS() : "win32",

            /* NW.js reports the versions it bundles. MV and several plugins
               gate features on node-webkit being recent enough, and read the
               value without checking that process.versions exists at all. */
            versions: {
                "node-webkit": "0.29.4",
                nw: "0.29.4",
                chromium: "66.0.3359.181",
                node: "9.11.1",
                v8: "6.6.346.32"
            },

            env: {},
            argv: [],
            exit: function() {
                if (typeof __native_platform !== "undefined") __native_platform.quit();
            },
            cwd: function() {
                return __native_io.getGameRoot() || ".";
            }
        };
    }

    /* indexedDB stub for Utils.canUseIndexedDB(). */

    if (typeof globalThis.indexedDB === "undefined") {
        globalThis.indexedDB = {
            open: function() {
                return { onupgradeneeded: null, onsuccess: null, onerror: null };
            }
        };
    }

    /* nw (NW.js) window/app stubs. */

    if (typeof globalThis.nw === "undefined") {
        var _nwWindowInstance = {
            on: function() {},
            x: 0,
            y: 0,
            width: 816,
            height: 624,
            title: "",
            moveBy: function() {},
            resizeTo: function() {},
            setAlwaysOnTop: function() {},
            setVisibleOnAllWorkspaces: function() {},
            isFullscreen: false,
            enterFullscreen: function() { this.isFullscreen = true; },
            leaveFullscreen: function() { this.isFullscreen = false; },
            close: function() {
                if (typeof __native_platform !== "undefined") __native_platform.quit();
            },
            focus: function() {},
            show: function() {},
            hide: function() {}
        };
        globalThis.nw = {
            Window: {
                get: function() {
                    return _nwWindowInstance;
                }
            },
            App: {
                argv: [],
                quit: function() {
                    if (typeof __native_platform !== "undefined") __native_platform.quit();
                }
            }
        };
    }

})();
