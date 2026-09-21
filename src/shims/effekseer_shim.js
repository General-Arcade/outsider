/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

/*
 * Effekseer shim: provides the effekseer.min.js API surface that RPG Maker MZ
 * expects, backed by the native __native_effekseer bindings instead of WASM.
 */

(function() {
    "use strict";

    var _native = globalThis.__native_effekseer;

    /* EffekseerEffect */

    function EffekseerEffect(context, nativeHandle) {
        this.context = context;
        this.nativeHandle = nativeHandle;
        this.isLoaded = false;
        this.onload = null;
        this.onerror = null;
    }

    /* Check load status and fire callback when ready. */
    EffekseerEffect.prototype._checkLoaded = function() {
        if (this.isLoaded) return;
        if (_native && _native.isLoaded(this.nativeHandle)) {
            this.isLoaded = true;
            if (this.onload) {
                this.onload();
            }
        }
    };

    /* EffekseerHandle */

    function EffekseerHandle(context, nativeInstance) {
        this.context = context;
        this.native = nativeInstance;
    }

    Object.defineProperty(EffekseerHandle.prototype, "exists", {
        get: function() {
            if (!_native) return false;
            return _native.exists(this.native);
        }
    });

    EffekseerHandle.prototype.stop = function() {
        if (_native) _native.stop(this.native);
    };

    EffekseerHandle.prototype.stopRoot = function() {
        if (_native) _native.stopRoot(this.native);
    };

    EffekseerHandle.prototype.setLocation = function(x, y, z) {
        if (_native) _native.setPosition(this.native, x, y, z);
    };

    EffekseerHandle.prototype.setRotation = function(x, y, z) {
        if (_native) _native.setRotation(this.native, x, y, z);
    };

    EffekseerHandle.prototype.setScale = function(x, y, z) {
        if (_native) _native.setScale(this.native, x, y, z);
    };

    EffekseerHandle.prototype.setSpeed = function(speed) {
        if (_native) _native.setSpeed(this.native, speed);
    };

    /* EffekseerContext */

    function EffekseerContext() {
        this._initialized = false;
        this._effects = [];
    }

    EffekseerContext.prototype.init = function(gl, settings) {
        /* The gl context only matters to the real Effekseer SDK; the native
           backend just needs the viewport size. */
        if (_native) {
            var w = 816, h = 624;
            if (gl && gl.canvas) {
                w = gl.canvas.width || w;
                h = gl.canvas.height || h;
            }
            _native.init(w, h, 8000);
        }
        this._initialized = true;
    };

    EffekseerContext.prototype.setRestorationOfStatesFlag = function(flag) {
        if (_native) _native.setRestorationOfStatesFlag(flag);
    };

    EffekseerContext.prototype.loadEffect = function(url, scale, onLoad, onError) {
        if (scale === undefined) scale = 1.0;
        var effect = new EffekseerEffect(this, 0);
        effect.onload = onLoad || null;
        effect.onerror = onError || null;

        if (_native) {
            var handle = _native.load(url, scale);
            effect.nativeHandle = handle;

            if (handle === 0) {
                if (effect.onerror) {
                    setTimeout(function() {
                        effect.onerror("Failed to load effect: " + url, url);
                    }, 0);
                }
            } else {
                if (_native.isLoaded(handle)) {
                    effect.isLoaded = true;
                    if (effect.onload) {
                        /* Defer the callback to match the SDK's async behavior. */
                        var cb = effect.onload;
                        setTimeout(function() { cb(); }, 0);
                    }
                } else {
                    /* Not loaded yet; poll from update(). */
                    this._effects.push(effect);
                }
            }
        }
        return effect;
    };

    EffekseerContext.prototype.releaseEffect = function(effect) {
        if (!effect || !_native) return;
        _native.release(effect.nativeHandle);
        effect.nativeHandle = 0;
        effect.isLoaded = false;
        var idx = this._effects.indexOf(effect);
        if (idx >= 0) this._effects.splice(idx, 1);
    };

    EffekseerContext.prototype.play = function(effect, x, y, z) {
        if (!effect || !_native || !effect.nativeHandle) return null;
        if (x === undefined) x = 0;
        if (y === undefined) y = 0;
        if (z === undefined) z = 0;
        var inst = _native.play(effect.nativeHandle, x, y, z);
        if (inst < 0) return null;
        return new EffekseerHandle(this, inst);
    };

    EffekseerContext.prototype.stopAll = function() {
        if (_native) _native.stopAll();
    };

    EffekseerContext.prototype.update = function(deltaFrames) {
        if (!_native) return;
        if (deltaFrames === undefined) deltaFrames = 1.0;
        _native.update(deltaFrames);

        /* Check pending async loads. */
        for (var i = this._effects.length - 1; i >= 0; i--) {
            this._effects[i]._checkLoaded();
            if (this._effects[i].isLoaded) {
                this._effects.splice(i, 1);
            }
        }
    };

    EffekseerContext.prototype.beginDraw = function() {
        if (_native) _native.beginDraw();
    };

    EffekseerContext.prototype.drawHandle = function(handle) {
        if (_native && handle) _native.drawHandle(handle.native);
    };

    EffekseerContext.prototype.endDraw = function() {
        if (_native) _native.endDraw();
    };

    EffekseerContext.prototype.setProjectionMatrix = function(matrix) {
        if (_native) _native.setProjectionMatrix(matrix);
    };

    EffekseerContext.prototype.setCameraMatrix = function(matrix) {
        if (_native) _native.setCameraMatrix(matrix);
    };

    EffekseerContext.prototype.setBackground = function(glTexture) {
        if (_native) _native.setBackground(glTexture);
    };

    EffekseerContext.prototype.resetBackground = function() {
        if (_native) _native.resetBackground();
    };

    EffekseerContext.prototype.isVertexArrayObjectSupported = function() {
        return true;
    };

    /* Effekseer (top-level singleton) */

    function Effekseer() {}

    Effekseer.prototype.initRuntime = function(wasmPath, onLoad, onError) {
        /* No WASM to load; fire onLoad asynchronously as the SDK would. */
        if (onLoad) {
            setTimeout(function() { onLoad(); }, 0);
        }
    };

    Effekseer.prototype.createContext = function() {
        return new EffekseerContext();
    };

    /* Deprecated top-level API (used by older Effekseer for WebGL). */
    Effekseer.prototype.init = function(gl, settings) {
        this.defaultContext = new EffekseerContext();
        this.defaultContext.init(gl, settings);
    };

    Effekseer.prototype.loadEffect = function(url, scale, onLoad, onError) {
        if (!this.defaultContext) return null;
        return this.defaultContext.loadEffect(url, scale, onLoad, onError);
    };

    Effekseer.prototype.play = function(effect, x, y, z) {
        if (!this.defaultContext) return null;
        return this.defaultContext.play(effect, x, y, z);
    };

    Effekseer.prototype.stopAll = function() {
        if (this.defaultContext) this.defaultContext.stopAll();
    };

    Effekseer.prototype.update = function(deltaFrames) {
        if (this.defaultContext) this.defaultContext.update(deltaFrames);
    };

    Effekseer.prototype.beginDraw = function() {
        if (this.defaultContext) this.defaultContext.beginDraw();
    };

    Effekseer.prototype.drawHandle = function(handle) {
        if (this.defaultContext) this.defaultContext.drawHandle(handle);
    };

    Effekseer.prototype.endDraw = function() {
        if (this.defaultContext) this.defaultContext.endDraw();
    };

    Effekseer.prototype.setProjectionMatrix = function(matrix) {
        if (this.defaultContext) this.defaultContext.setProjectionMatrix(matrix);
    };

    Effekseer.prototype.setCameraMatrix = function(matrix) {
        if (this.defaultContext) this.defaultContext.setCameraMatrix(matrix);
    };

    Effekseer.prototype.releaseEffect = function(effect) {
        if (this.defaultContext) this.defaultContext.releaseEffect(effect);
    };

    Effekseer.prototype.setBackground = function(glTexture) {
        if (this.defaultContext) this.defaultContext.setBackground(glTexture);
    };

    Effekseer.prototype.resetBackground = function() {
        if (this.defaultContext) this.defaultContext.resetBackground();
    };

    Effekseer.prototype.isVertexArrayObjectSupported = function() {
        if (this.defaultContext) return this.defaultContext.isVertexArrayObjectSupported();
        return true;
    };

    globalThis.effekseer = new Effekseer();

    /* CommonJS compatibility. */
    if (typeof exports !== "undefined") {
        exports = globalThis.effekseer;
    }

})();
