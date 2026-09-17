/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

/*
 * XMLHttpRequest shim: serves requests from the local game directory via native file I/O.
 */

(function() {
    "use strict";

    var UNSENT = 0;
    var OPENED = 1;
    var HEADERS_RECEIVED = 2;
    var LOADING = 3;
    var DONE = 4;

    function XMLHttpRequest() {
        this.readyState = UNSENT;
        this.status = 0;
        this.statusText = "";
        this.response = null;
        this.responseText = "";
        this.responseType = "";
        this.responseURL = "";
        this.timeout = 0;
        this.withCredentials = false;
        this.onload = null;
        this.onerror = null;
        this.onreadystatechange = null;
        this.onprogress = null;
        this.onloadend = null;
        this.onloadstart = null;
        this.ontimeout = null;
        this.onabort = null;
        this._method = "";
        this._url = "";
        this._async = true;
        this._mimeOverride = null;
        this._requestHeaders = {};
        this._aborted = false;
        this._settled = false;
    }

    XMLHttpRequest.UNSENT = UNSENT;
    XMLHttpRequest.OPENED = OPENED;
    XMLHttpRequest.HEADERS_RECEIVED = HEADERS_RECEIVED;
    XMLHttpRequest.LOADING = LOADING;
    XMLHttpRequest.DONE = DONE;

    XMLHttpRequest.prototype.open = function(method, url, async) {
        this._method = method || "GET";
        this._url = url;
        this._async = (async !== false);
        this._aborted = false;
        this._settled = false;
        /* Generation counter so a stale timeout timer from a previous request
           on a reused XHR object cannot fire for this one. */
        this._gen = (this._gen || 0) + 1;
        this.readyState = OPENED;
        this.status = 0;
        this.statusText = "";
        this.response = null;
        this.responseText = "";
        this._fireReadyStateChange();
    };

    XMLHttpRequest.prototype.send = function(body) {
        var self = this;

        var doLoad = function() {
            if (self._aborted) return;

            /* Only a failure of the native read is an XHR "error". Exceptions from
               the game's own load handlers propagate as in a browser, with the
               request still reported as a successful 200. */
            var url, isBinary, found, data;
            try {
                /* RPG Maker MZ percent-encodes filenames; the filesystem needs literal names. */
                url = decodeURIComponent(self._url);
                isBinary = (self.responseType === "arraybuffer");
                found = __native_io.existsSync(url);
                if (found) {
                    if (isBinary) {
                        data = __native_io.readFileBinary(url);
                    } else {
                        data = __native_io.readFileSync(url);
                        /* Browsers strip a leading UTF-8 BOM from responseText;
                           JSON.parse would reject it. */
                        if (typeof data === "string" && data.charCodeAt(0) === 0xFEFF) {
                            data = data.slice(1);
                        }
                    }
                }
            } catch (e) {
                self.readyState = DONE;
                self.status = 0;
                self.statusText = "";
                self.response = null;
                self.responseText = "";
                self._fireReadyStateChange();
                self._fireEvent("error");
                self._fireEvent("loadend");
                return;
            }

            if (!found) {
                self.readyState = HEADERS_RECEIVED;
                self.status = 404;
                self.statusText = "Not Found";
                self._fireReadyStateChange();
                self.readyState = LOADING;
                self._fireReadyStateChange();
                self.readyState = DONE;
                self.response = null;
                self.responseText = "";
                self._fireReadyStateChange();
                self._fireEvent("load");
                self._fireEvent("loadend");
                return;
            }

            self.readyState = HEADERS_RECEIVED;
            self._fireReadyStateChange();
            self.readyState = LOADING;
            self._fireReadyStateChange();

            self.readyState = DONE;
            self.status = 200;
            self.statusText = "OK";
            self.responseURL = url;

            if (self.responseType === "json") {
                try {
                    self.response = JSON.parse(data);
                } catch (e) {
                    self.response = null;
                }
                self.responseText = data;
            } else if (isBinary) {
                self.response = data;
                self.responseText = "";
            } else {
                self.response = data;
                self.responseText = data;
            }

            self._fireReadyStateChange();
            /* As in a browser, loadend fires even if a load handler throws;
               the handler's exception is then re-raised to the caller. */
            var handlerError = null;
            try {
                self._fireEvent("load");
            } catch (e) {
                handlerError = e;
            }
            self._fireEvent("loadend");
            if (handlerError) throw handlerError;
        };

        if (this._async) {
            Promise.resolve().then(doLoad);
            /* Local reads settle on the next microtask, so the timeout rarely fires. */
            if (this.timeout > 0 && typeof setTimeout === "function") {
                var gen = this._gen;
                setTimeout(function() {
                    if (self._gen !== gen || self._settled || self._aborted) return;
                    self._aborted = true;
                    self.readyState = DONE;
                    self.status = 0;
                    self.statusText = "";
                    self._fireReadyStateChange();
                    self._fireEvent("timeout");
                    self._fireEvent("loadend");
                    self.readyState = UNSENT;
                }, this.timeout);
            }
        } else {
            doLoad();
        }
    };

    XMLHttpRequest.prototype.abort = function() {
        var wasActive = this.readyState !== UNSENT && this.readyState !== DONE;
        this._aborted = true;
        this.status = 0;
        this.statusText = "";
        this.response = null;
        this.responseText = "";
        /* Per spec, an in-flight request fires abort + loadend (with a
           transient DONE readystatechange) before settling back to UNSENT. */
        if (wasActive) {
            this.readyState = DONE;
            this._fireReadyStateChange();
            this._fireEvent("abort");
            this._fireEvent("loadend");
        }
        this.readyState = UNSENT;
    };

    XMLHttpRequest.prototype.overrideMimeType = function(mime) {
        this._mimeOverride = mime;
    };

    XMLHttpRequest.prototype.setRequestHeader = function(name, value) {
        this._requestHeaders[name] = value;
    };

    XMLHttpRequest.prototype.getResponseHeader = function(name) {
        if (this.readyState < HEADERS_RECEIVED) return null;
        var lower = name.toLowerCase();
        if (lower === "content-type") {
            if (this._mimeOverride) return this._mimeOverride;
            return "text/plain";
        }
        return null;
    };

    XMLHttpRequest.prototype.getAllResponseHeaders = function() {
        if (this.readyState < HEADERS_RECEIVED) return "";
        return "content-type: text/plain\r\n";
    };

    XMLHttpRequest.prototype._createEvent = function(type) {
        return { type: type, target: this, currentTarget: this,
                 lengthComputable: false, loaded: 0, total: 0 };
    };

    XMLHttpRequest.prototype._fireReadyStateChange = function() {
        var evt = this._createEvent("readystatechange");
        if (typeof this.onreadystatechange === "function") {
            this.onreadystatechange(evt);
        }
        if (this._listeners && this._listeners["readystatechange"]) {
            var arr = this._listeners["readystatechange"];
            for (var i = 0; i < arr.length; i++) {
                arr[i].call(this, evt);
            }
        }
    };

    XMLHttpRequest.prototype._fireEvent = function(type) {
        if (type === "load" || type === "error" || type === "abort" || type === "timeout") {
            this._settled = true;
        }
        var evt = this._createEvent(type);
        var handler = this["on" + type];
        if (typeof handler === "function") {
            handler.call(this, evt);
        }
        if (this._listeners && this._listeners[type]) {
            var arr = this._listeners[type];
            for (var i = 0; i < arr.length; i++) {
                arr[i].call(this, evt);
            }
        }
    };

    XMLHttpRequest.prototype.addEventListener = function(type, listener) {
        if (!this._listeners) this._listeners = {};
        if (!this._listeners[type]) this._listeners[type] = [];
        this._listeners[type].push(listener);
    };

    XMLHttpRequest.prototype.removeEventListener = function(type, listener) {
        if (!this._listeners || !this._listeners[type]) return;
        var arr = this._listeners[type];
        for (var i = 0; i < arr.length; i++) {
            if (arr[i] === listener) {
                arr.splice(i, 1);
                return;
            }
        }
    };

    globalThis.XMLHttpRequest = XMLHttpRequest;
})();
