/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

/*
 * Plugin compatibility layer: stubs and polyfills for APIs used by RPG Maker MZ
 * plugins that the core shims do not cover. Loaded after pixi_shim.js, before game code.
 */

(function() {
    "use strict";

    /* PIXI filter stubs for FilterControllerMZ: pixi-filters classes the PIXI shim
       does not implement. They accept properties but have no visual effect. */

    if (typeof PIXI !== "undefined" && PIXI.filters) {

        function StubFilter() {
            if (PIXI.Filter) {
                PIXI.Filter.call(this);
            }
            this.enabled = true;
            this.padding = 0;
        }
        if (PIXI.Filter) {
            StubFilter.prototype = Object.create(PIXI.Filter.prototype);
            StubFilter.prototype.constructor = StubFilter;
        }

        /* Creates a named stub filter class with the given default properties. */
        function makeStubFilter(name, defaults) {
            function FilterCtor() {
                StubFilter.call(this);
                if (defaults) {
                    for (var k in defaults) {
                        if (defaults.hasOwnProperty(k)) {
                            this[k] = defaults[k];
                        }
                    }
                }
            }
            FilterCtor.prototype = Object.create(StubFilter.prototype);
            FilterCtor.prototype.constructor = FilterCtor;
            FilterCtor.displayName = name;
            return FilterCtor;
        }

        /* Only create stubs for filters not already defined */
        var filterStubs = {
            DisplacementFilter: { scale: { x: 0, y: 0 } },
            BulgePinchFilter: { radius: 0, strength: 0, center: [0.5, 0.5] },
            RadialBlurFilter: { angle: 0, center: [0.5, 0.5], radius: -1 },
            GodrayFilter: { angle: 30, gain: 0.5, lacunarity: 2.5, time: 0 },
            AsciiFilter: { size: 8 },
            CrossHatchFilter: {},
            DotFilter: { scale: 1, angle: 5 },
            EmbossFilter: { strength: 5 },
            ShockwaveFilter: { center: [0.5, 0.5], radius: -1, amplitude: 30, wavelength: 160, speed: 500, time: 0 },
            TwistFilter: { radius: 200, angle: 4, padding: 20, offset: [0, 0] },
            ZoomBlurFilter: { strength: 0.1, center: [0.5, 0.5], innerRadius: 0 },
            NoiseFilter: { noise: 0.5, seed: 0 },
            KawaseBlurFilter: { blur: 4, quality: 3 },
            OldFilmFilter: { sepia: 0.3, noise: 0.3, scratch: 0.5, vignetting: 0.3 },
            RGBSplitFilter: { red: [0, 0], green: [0, 0], blue: [0, 0] },
            AdvancedBloomFilter: { threshold: 0.5, bloomScale: 1.0, brightness: 1.0, blur: 8, quality: 4 },
            AdjustmentFilter: { gamma: 1, saturation: 1, contrast: 1, brightness: 1, red: 1, green: 1, blue: 1, alpha: 1 },
            PixelateFilter: { size: [1, 1] },
            CRTFilter: { curvature: 1, lineWidth: 1, lineContrast: 0.25, noise: 0.3, noiseSize: 1, vignetting: 0.3, seed: 0, time: 0 },
            ReflectionFilter: { mirror: true, boundary: 0.5, amplitude: [0, 20], waveLength: [30, 100], alpha: [1, 1], time: 0 },
            MotionBlurFilter: { velocity: [0, 0], kernelSize: 5, offset: 0 },
            GlowFilter: { distance: 10, outerStrength: 4, innerStrength: 0, color: 0xffffff, quality: 0.1 },
            BevelFilter: { rotation: 45, thickness: 2, lightColor: 0xffffff, lightAlpha: 0.7, shadowColor: 0x000000, shadowAlpha: 0.7 },
            TiltShiftFilter: { blur: 100, gradientBlur: 600, start: null, end: null },
            GlitchFilter: { slices: 5, offset: 100, direction: 0, fillMode: 0, seed: 0 }
        };

        for (var fname in filterStubs) {
            if (filterStubs.hasOwnProperty(fname) && !PIXI.filters[fname]) {
                PIXI.filters[fname] = makeStubFilter(fname, filterStubs[fname]);
            }
        }
    }

    /* fetch() shim built on XMLHttpRequest. */

    if (typeof globalThis.fetch === "undefined") {
        globalThis.fetch = function(url, options) {
            return new Promise(function(resolve, reject) {
                try {
                    var xhr = new XMLHttpRequest();
                    var method = (options && options.method) || "GET";
                    xhr.open(method, url);

                    if (options && options.headers) {
                        for (var key in options.headers) {
                            if (options.headers.hasOwnProperty(key)) {
                                if (typeof xhr.setRequestHeader === "function") {
                                    xhr.setRequestHeader(key, options.headers[key]);
                                }
                            }
                        }
                    }

                    xhr.onload = function() {
                        var responseText = xhr.responseText || "";
                        resolve({
                            ok: xhr.status >= 200 && xhr.status < 300,
                            status: xhr.status,
                            statusText: xhr.statusText || "",
                            headers: {
                                get: function() { return null; }
                            },
                            text: function() {
                                return Promise.resolve(responseText);
                            },
                            json: function() {
                                try {
                                    return Promise.resolve(JSON.parse(responseText));
                                } catch (e) {
                                    return Promise.reject(e);
                                }
                            },
                            arrayBuffer: function() {
                                /* Re-read as binary if caller needs an ArrayBuffer. */
                                var binXhr = new XMLHttpRequest();
                                binXhr.open("GET", url);
                                binXhr.responseType = "arraybuffer";
                                return new Promise(function(res, rej) {
                                    binXhr.onload = function() { res(binXhr.response); };
                                    binXhr.onerror = function() { rej(new Error("arrayBuffer fetch failed")); };
                                    binXhr.send();
                                });
                            },
                            blob: function() {
                                var binXhr = new XMLHttpRequest();
                                binXhr.open("GET", url);
                                binXhr.responseType = "arraybuffer";
                                return new Promise(function(res, rej) {
                                    binXhr.onload = function() { res(new Blob([binXhr.response])); };
                                    binXhr.onerror = function() { rej(new Error("blob fetch failed")); };
                                    binXhr.send();
                                });
                            }
                        });
                    };

                    xhr.onerror = function() {
                        reject(new Error("fetch failed: " + url));
                    };

                    if (options && options.body) {
                        xhr.send(options.body);
                    } else {
                        xhr.send();
                    }
                } catch (e) {
                    reject(e);
                }
            });
        };
    }

    /* Wayback Machine wrapper globals: plugins saved from web.archive.org are
       wrapped in _____WB$wombat... code that expects these to exist. */

    if (typeof globalThis._____WB$wombat$assign$function_____ === "undefined") {
        globalThis._____WB$wombat$assign$function_____ = function(name) {
            return globalThis[name];
        };
    }

    if (typeof globalThis.__WB_pmw === "undefined") {
        globalThis.__WB_pmw = function(obj) {
            this.__WB_source = obj;
            return this;
        };
    }

    /* Greenworks (Steam SDK) stub module so OrangeGreenworks / Cyclone-Steam
       see initAPI() === false and degrade gracefully. */

    if (typeof globalThis.require === "function") {
        var _origRequire = globalThis.require;
        globalThis.require = function(moduleName) {
            if (moduleName === "./greenworks" || moduleName === "greenworks") {
                return {
                    initAPI: function() { return false; },
                    getSteamId: function() { return { screenName: "Player" }; },
                    isSteamRunning: function() { return false; },
                    activateAchievement: function() {},
                    getAchievement: function() { return false; },
                    clearAchievement: function() {},
                    getNumberOfAchievements: function() { return 0; },
                    activateGameOverlay: function() {},
                    isGameOverlayEnabled: function() { return false; },
                    activateGameOverlayToWebPage: function() {},
                    getDLCCount: function() { return 0; },
                    isDLCInstalled: function() { return false; },
                    installDLC: function() {},
                    uninstallDLC: function() {},
                    getStatInt: function() { return 0; },
                    getStatFloat: function() { return 0; },
                    setStat: function() { return false; },
                    storeStats: function() { return false; },
                    isSubscribedApp: function() { return false; },
                    isCloudEnabled: function() { return false; },
                    isCloudEnabledForUser: function() { return false; },
                    getCurrentUILanguage: function() { return "english"; },
                    getCurrentGameLanguage: function() { return "english"; },
                    FriendFlags: { Immediate: 0 },
                    getFriendCount: function() { return 0; }
                };
            }
            return _origRequire(moduleName);
        };
    }

    /* QuickJS-ng defines getter-only fileName/lineNumber/columnNumber on
       Function.prototype; browsers do not, and strict-mode static assignments
       with those names would throw "no setter for property". */

    (function() {
        var names = ["fileName", "lineNumber", "columnNumber"];
        for (var i = 0; i < names.length; i++) {
            var d = Object.getOwnPropertyDescriptor(Function.prototype, names[i]);
            if (d && d.get && !d.set && d.configurable) {
                try { delete Function.prototype[names[i]]; } catch (e) { /* ignore */ }
            }
        }
    })();

    /* Non-standard Array.prototype.contains used by some plugins. */

    if (!Array.prototype.contains) {
        /* Non-enumerable so it does not leak into for..in over arrays. */
        Object.defineProperty(Array.prototype, "contains", {
            value: function(item) {
                return this.indexOf(item) !== -1;
            },
            enumerable: false,
            writable: true,
            configurable: true
        });
    }

    /* decodeURIComponent / encodeURIComponent fallbacks. */

    if (typeof globalThis.decodeURIComponent === "undefined") {
        globalThis.decodeURIComponent = function(s) { return s; };
    }
    if (typeof globalThis.encodeURIComponent === "undefined") {
        globalThis.encodeURIComponent = function(s) { return s; };
    }

    /* Minor compatibility stubs. */

    if (typeof globalThis.CSS === "undefined") {
        globalThis.CSS = {
            supports: function() { return false; }
        };
    }

    if (typeof globalThis.FontFace === "undefined") {
        globalThis.FontFace = function(family, source, descriptors) {
            this.family = family;
            this.source = source;
            this.style = (descriptors && descriptors.style) || "normal";
            this.weight = (descriptors && descriptors.weight) || "normal";
            this.status = "loaded";
            this.loaded = Promise.resolve(this);
        };
        globalThis.FontFace.prototype.load = function() {
            this.status = "loaded";
            return Promise.resolve(this);
        };
    }

    if (typeof globalThis.MutationObserver === "undefined") {
        globalThis.MutationObserver = function(callback) {
            this._callback = callback;
        };
        globalThis.MutationObserver.prototype.observe = function() {};
        globalThis.MutationObserver.prototype.disconnect = function() {};
        globalThis.MutationObserver.prototype.takeRecords = function() { return []; };
    }

    if (typeof globalThis.ResizeObserver === "undefined") {
        globalThis.ResizeObserver = function(callback) {
            this._callback = callback;
        };
        globalThis.ResizeObserver.prototype.observe = function() {};
        globalThis.ResizeObserver.prototype.unobserve = function() {};
        globalThis.ResizeObserver.prototype.disconnect = function() {};
    }

    /* PIXI.Sprite.from() / PIXI.Texture.from() shorthands. */

    if (typeof PIXI !== "undefined" && PIXI.Sprite && !PIXI.Sprite.from) {
        PIXI.Sprite.from = function(source) {
            var texture;
            if (typeof source === "string") {
                texture = PIXI.Texture.from ? PIXI.Texture.from(source) : PIXI.Texture.EMPTY;
            } else if (source instanceof PIXI.Texture) {
                texture = source;
            } else {
                texture = PIXI.Texture.EMPTY;
            }
            return new PIXI.Sprite(texture);
        };
    }

    if (typeof PIXI !== "undefined" && PIXI.Texture && !PIXI.Texture.from) {
        PIXI.Texture.from = function(source) {
            if (source instanceof PIXI.Texture) return source;
            if (source instanceof PIXI.BaseTexture) {
                return new PIXI.Texture(source);
            }
            /* String/canvas/image sources are not resolved here. */
            return PIXI.Texture.EMPTY || new PIXI.Texture(PIXI.BaseTexture.EMPTY);
        };
    }

    /* Native APNG decoding for pixi-apngAndGif (the library behind
       ApngPicture.js). It decodes APNG with a bundled pure-JS UPNG + pako,
       which takes minutes under QuickJS when a game preloads hundreds of
       animations at boot. Its apngResourceToTextures(resource) method is the
       seam: it receives the loaded ArrayBuffer and returns frame textures and
       delays, so it is replaced with the native decoder and falls back to the
       original for anything the native side rejects. The class is defined
       later by the plugin script, so the patch is applied on assignment. */

    if (typeof __native_image !== "undefined" &&
        typeof __native_image.decodeApng === "function" &&
        typeof globalThis.PixiApngAndGif === "undefined") {

        var _patchApngLibrary = function(lib) {
            if (!lib || !lib.prototype ||
                typeof lib.prototype.apngResourceToTextures !== "function" ||
                lib.prototype.__nativeApng) {
                return lib;
            }
            var original = lib.prototype.apngResourceToTextures;
            lib.prototype.apngResourceToTextures = function(resource) {
                var data = resource && resource.data;
                var decoded = null;
                if (data && (data instanceof ArrayBuffer || ArrayBuffer.isView(data))) {
                    try {
                        decoded = __native_image.decodeApng(data);
                    } catch (e) {
                        decoded = null;
                    }
                }
                if (!decoded || !decoded.frames.length || !decoded.frames[0].glTexture) {
                    return original.call(this, resource);
                }
                var out = { delayTimes: [], textures: [] };
                for (var i = 0; i < decoded.frames.length; i++) {
                    var frame = decoded.frames[i];
                    var bt = new PIXI.BaseTexture(null);
                    bt.setRealSize(decoded.width, decoded.height);
                    /* The texture is this BaseTexture's own: destroy() deletes it. */
                    bt._glTexture = frame.glTexture;
                    bt._glTextureIsImageOwned = false;
                    bt._glTexW = decoded.width;
                    bt._glTexH = decoded.height;
                    bt.valid = true;
                    out.textures.push(new PIXI.Texture(bt,
                        new PIXI.Rectangle(0, 0, decoded.width, decoded.height)));
                    out.delayTimes.push(frame.delay);
                }
                return out;
            };
            lib.prototype.__nativeApng = true;
            return lib;
        };

        var _apngLibrary;
        Object.defineProperty(globalThis, "PixiApngAndGif", {
            configurable: true,
            enumerable: true,
            get: function() { return _apngLibrary; },
            set: function(value) { _apngLibrary = _patchApngLibrary(value); }
        });
    }

    /* Legacy RegExp static properties (RegExp.$1..$9, RegExp.lastMatch, ...).
       Browsers keep them for web compatibility and RPG Maker plugins lean on
       them heavily (VisuMZ reads its own version through RegExp.$1 after a
       String.prototype.match and calls SceneManager.exit() on a mismatch).
       QuickJS does not implement them, so record the last successful match
       here. Every regex operation (match, replace, test, split, search) goes
       through RegExp.prototype.exec, so wrapping it is sufficient. */

    if (typeof RegExp.$1 === "undefined") {
        var _nativeExec = RegExp.prototype.exec;
        var _lastMatch = null;
        var _lastInput = "";

        RegExp.prototype.exec = function(str) {
            var m = _nativeExec.call(this, str);
            if (m) {
                _lastMatch = m;
                _lastInput = String(str);
            }
            return m;
        };

        var _defineStatic = function(names, getter) {
            for (var i = 0; i < names.length; i++) {
                Object.defineProperty(RegExp, names[i], {
                    get: getter, configurable: true, enumerable: false
                });
            }
        };

        var _groupGetter = function(n) {
            return function() {
                return _lastMatch && _lastMatch[n] !== undefined ? _lastMatch[n] : "";
            };
        };
        for (var g = 1; g <= 9; g++) {
            _defineStatic(["$" + g], _groupGetter(g));
        }
        _defineStatic(["lastMatch", "$&"], function() {
            return _lastMatch ? _lastMatch[0] : "";
        });
        _defineStatic(["input", "$_"], function() {
            return _lastMatch ? _lastInput : "";
        });
        _defineStatic(["lastParen", "$+"], function() {
            if (!_lastMatch || _lastMatch.length < 2) return "";
            var v = _lastMatch[_lastMatch.length - 1];
            return v !== undefined ? v : "";
        });
        _defineStatic(["leftContext", "$`"], function() {
            return _lastMatch ? _lastInput.substring(0, _lastMatch.index) : "";
        });
        _defineStatic(["rightContext", "$'"], function() {
            return _lastMatch
                ? _lastInput.substring(_lastMatch.index + _lastMatch[0].length) : "";
        });
    }

})();
