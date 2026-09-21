/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

/*
 * CSS Font Loading API shim: FontFace, FontFaceSet and document.fonts,
 * backed by the native font loader.
 */

(function() {
    "use strict";

    /* FontFace */

    function FontFace(family, source, descriptors) {
        this.family = family || "";
        this.style = (descriptors && descriptors.style) || "normal";
        this.weight = (descriptors && descriptors.weight) || "normal";
        this.stretch = (descriptors && descriptors.stretch) || "normal";
        this.unicodeRange = (descriptors && descriptors.unicodeRange) || "U+0-10FFFF";
        this.variant = (descriptors && descriptors.variant) || "normal";
        this.featureSettings = (descriptors && descriptors.featureSettings) || "normal";
        this.display = (descriptors && descriptors.display) || "auto";

        this.status = "unloaded";
        this._source = source;
        this._family = family;

        var self = this;
        this.loaded = new Promise(function(resolve, reject) {
            self._resolve = resolve;
            self._reject = reject;
        });

        /* Loading starts eagerly; a string source may be url("path") or a bare path. */
        if (source instanceof ArrayBuffer) {
            this._loadFromBuffer(source);
        } else if (typeof source === "string") {
            var m = source.match(/url\s*\(\s*['"]?([^'")\s]+)['"]?\s*\)/);
            if (m) {
                this._loadFromUrl(m[1]);
            } else {
                this._loadFromUrl(source);
            }
        }
    }

    FontFace.prototype._loadFromBuffer = function(buffer) {
        this.status = "loading";
        var ok = false;
        if (typeof __native_font !== "undefined") {
            ok = __native_font.loadFont(this._family, buffer);
        } else if (typeof __native_canvas2d !== "undefined") {
            ok = __native_canvas2d.loadFont(this._family, buffer);
        }
        if (ok) {
            this.status = "loaded";
            if (this._resolve) this._resolve(this);
        } else {
            this.status = "error";
            if (this._reject) this._reject(new Error("Failed to load font: " + this._family));
        }
    };

    FontFace.prototype._loadFromUrl = function(url) {
        this.status = "loading";
        var ok = false;
        if (typeof __native_font !== "undefined") {
            ok = __native_font.loadFontFile(this._family, url);
        } else if (typeof __native_canvas2d !== "undefined") {
            ok = __native_canvas2d.loadFontFile(this._family, url);
        }
        if (ok) {
            this.status = "loaded";
            if (this._resolve) this._resolve(this);
        } else {
            this.status = "error";
            if (this._reject) this._reject(new Error("Failed to load font file: " + url));
        }
    };

    FontFace.prototype.load = function() {
        if (this.status === "loaded") return Promise.resolve(this);
        if (this.status === "error") return Promise.reject(new Error("Font load failed"));
        return this.loaded;
    };

    /* FontFaceSet */

    function FontFaceSet() {
        this._fonts = [];
        this._listeners = {};
        this.status = "loaded";

        var self = this;
        this.ready = new Promise(function(resolve) {
            self._readyResolve = resolve;
            resolve(self);
        });
    }

    FontFaceSet.prototype.add = function(fontFace) {
        if (!(fontFace instanceof FontFace)) return;
        /* Same family/style/weight replaces the existing entry. */
        for (var i = 0; i < this._fonts.length; i++) {
            if (this._fonts[i].family === fontFace.family &&
                this._fonts[i].style === fontFace.style &&
                this._fonts[i].weight === fontFace.weight) {
                this._fonts[i] = fontFace;
                return;
            }
        }
        this._fonts.push(fontFace);
        this._updateStatus();
    };

    FontFaceSet.prototype.delete = function(fontFace) {
        var idx = this._fonts.indexOf(fontFace);
        if (idx >= 0) {
            this._fonts.splice(idx, 1);
            this._updateStatus();
            return true;
        }
        return false;
    };

    FontFaceSet.prototype.clear = function() {
        this._fonts = [];
        this.status = "loaded";
    };

    FontFaceSet.prototype.has = function(fontFace) {
        return this._fonts.indexOf(fontFace) >= 0;
    };

    FontFaceSet.prototype.forEach = function(callback, thisArg) {
        for (var i = 0; i < this._fonts.length; i++) {
            callback.call(thisArg || this, this._fonts[i], this._fonts[i], this);
        }
    };

    Object.defineProperty(FontFaceSet.prototype, "size", {
        get: function() { return this._fonts.length; }
    });

    FontFaceSet.prototype.check = function(font, text) {
        var family = _parseFontFamily(font);
        if (!family) return true;

        if (typeof __native_font !== "undefined") {
            return __native_font.hasFont(family);
        }
        for (var i = 0; i < this._fonts.length; i++) {
            if (this._fonts[i].family === family && this._fonts[i].status === "loaded") {
                return true;
            }
        }
        return false;
    };

    FontFaceSet.prototype.load = function(font, text) {
        var family = _parseFontFamily(font);
        var results = [];

        for (var i = 0; i < this._fonts.length; i++) {
            if (this._fonts[i].family === family || !family) {
                results.push(this._fonts[i].loaded);
            }
        }

        if (results.length === 0) {
            return Promise.resolve([]);
        }
        return Promise.all(results);
    };

    FontFaceSet.prototype._updateStatus = function() {
        var loading = false;
        for (var i = 0; i < this._fonts.length; i++) {
            if (this._fonts[i].status === "loading") {
                loading = true;
                break;
            }
        }
        this.status = loading ? "loading" : "loaded";

        if (!loading) {
            var self = this;
            this.ready = Promise.resolve(self);
            this._dispatchEvent("loadingdone");
        }
    };

    FontFaceSet.prototype.addEventListener = function(type, fn) {
        if (!this._listeners[type]) this._listeners[type] = [];
        this._listeners[type].push(fn);
    };

    FontFaceSet.prototype.removeEventListener = function(type, fn) {
        var list = this._listeners[type];
        if (!list) return;
        var idx = list.indexOf(fn);
        if (idx >= 0) list.splice(idx, 1);
    };

    FontFaceSet.prototype._dispatchEvent = function(type) {
        var list = this._listeners[type];
        if (!list) return;
        var event = { type: type, target: this };
        for (var i = 0; i < list.length; i++) {
            list[i](event);
        }
    };

    /* Extracts the family name from a CSS font string ("bold 12px Foo" -> "Foo"). */
    function _parseFontFamily(fontStr) {
        if (!fontStr) return null;
        var idx = fontStr.indexOf("px");
        if (idx >= 0) {
            var rest = fontStr.substring(idx + 2).trim();
            if (rest.length > 0) {
                return rest.replace(/['"]/g, "").trim();
            }
        }
        return fontStr.replace(/['"]/g, "").trim();
    }

    var _fontFaceSet = new FontFaceSet();

    /* Replaces the document.fonts stub from dom_shim.js. */
    if (typeof document !== "undefined") {
        document.fonts = _fontFaceSet;
    }

    globalThis.FontFace = FontFace;
    globalThis.FontFaceSet = FontFaceSet;
    globalThis.__fontFaceSet = _fontFaceSet;

})();
