/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

/*
 * navigator shim: user agent, platform info and the Gamepad API, with
 * gamepad slots populated from native input.
 */

(function() {
    "use strict";

    var _gamepads = [null, null, null, null];

    var navigator = {
        /* RPG Maker MZ looks for "Chrome" in the user agent to enable WebGL;
           "nwjs" is deliberately absent so NW.js-only paths stay off. */
        userAgent: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Outsider/1.0 Chrome/120.0.0.0",

        /* RPG Maker MZ checks this for standalone app detection. */
        standalone: true,

        platform: "Win32",
        language: "en-US",
        languages: ["en-US", "en"],
        maxTouchPoints: 0,
        hardwareConcurrency: 4,
        onLine: true,
        cookieEnabled: false,
        vendor: "",
        vendorSub: "",
        product: "Gecko",
        productSub: "20030107",
        appName: "Netscape",
        appVersion: "5.0 (Windows NT 10.0; Win64; x64) Outsider/1.0",
        appCodeName: "Mozilla",

        getGamepads: function() {
            return _gamepads.slice();
        },

        vibrate: function() { return false; },

        clipboard: {
            writeText: function(text) { return Promise.resolve(); },
            readText: function() { return Promise.resolve(""); }
        },

        mediaDevices: {
            getUserMedia: function() { return Promise.reject(new Error("Not supported")); },
            enumerateDevices: function() { return Promise.resolve([]); }
        },

        permissions: {
            query: function() { return Promise.resolve({ state: "denied" }); }
        },

        storage: {
            estimate: function() { return Promise.resolve({ quota: 0, usage: 0 }); },
            persist: function() { return Promise.resolve(false); },
            persisted: function() { return Promise.resolve(false); }
        },

        serviceWorker: null,
        geolocation: null,

        sendBeacon: function() { return false; }
    };

    /* Gamepad slot updates, called from native input. */

    globalThis.__navigator_setGamepad = function(index, gamepad) {
        if (index >= 0 && index < 4) {
            _gamepads[index] = gamepad;
        }
    };

    globalThis.__navigator_clearGamepad = function(index) {
        if (index >= 0 && index < 4) {
            _gamepads[index] = null;
        }
    };

    globalThis.navigator = navigator;
    if (globalThis.window) {
        globalThis.window.navigator = navigator;
    }

})();
