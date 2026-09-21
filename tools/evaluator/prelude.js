/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

/*
 * Visual evaluator prelude. Injected into both players before the first
 * game frame (NW.js: Page.addScriptToEvaluateOnNewDocument; Outsider: the
 * first --control command). It makes the two runs comparable:
 *
 *   - Math.random is seeded, so random layouts agree when call order agrees;
 *   - config and global save info are not read from disk, so options and
 *     the file list start from engine defaults on both sides;
 *   - the game advances only while a scripted action is running. Every
 *     action ends with the scene frozen (SceneManager.updateMain skipped),
 *     so a screenshot taken afterwards sees the exact frame the action
 *     stopped on, whatever the driver's own latency is. Frames are counted
 *     in updateMain calls, i.e. logical game frames, not wall-clock time.
 *
 * Everything lives under __ev. Written in ES5 so QuickJS and old NW.js
 * builds both run it unchanged.
 */

(function() {
    "use strict";

    var ev = {
        frame: 0,            /* logical frames advanced since installation */
        frozen: false,
        installed: false,
        waiter: null,        /* { test, timeout, resolve, reject, freeze } */
        injectedAt: null
    };
    ev.injectedAt = typeof SceneManager !== "undefined" && SceneManager._scene
        ? SceneManager._scene.constructor.name : "boot";

    /* --- Deterministic Math.random (mulberry32) ------------------------ */
    (function seedRandom(seed) {
        var a = seed >>> 0;
        Math.random = function() {
            a = (a + 0x6D2B79F5) >>> 0;
            var t = a;
            t = Math.imul(t ^ (t >>> 15), t | 1);
            t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
            return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
        };
    })(0x5EED1234);

    /* --- Storage isolation ---------------------------------------------
       RMMZ's StorageManager is declared after this prelude runs in NW.js
       (and the browser has an unrelated global of the same name), so the
       patch is applied on the first requestAnimationFrame call, which
       Graphics.startGameLoop issues before Scene_Boot loads player data. */
    var storagePatched = false;
    function patchStorage() {
        if (storagePatched) return true;
        if (typeof StorageManager === "undefined" ||
            typeof StorageManager.loadObject !== "function") return false;
        storagePatched = true;
        StorageManager.loadObject = function() {
            return Promise.reject(new Error("visual-eval: storage reads disabled"));
        };
        StorageManager.saveObject = function() {
            return Promise.resolve();
        };
        StorageManager.exists = function() { return false; };
        return true;
    }
    if (!patchStorage()) {
        var rafOrig = window.requestAnimationFrame;
        window.requestAnimationFrame = function(cb) {
            if (patchStorage()) window.requestAnimationFrame = rafOrig;
            return rafOrig.call(window, cb);
        };
    }

    /* --- Stay windowed --------------------------------------------------
       Both players are launched at the game's own resolution; a plugin that
       goes fullscreen at boot would make every screenshot a stretched copy
       of the screen instead. */
    (function disableFullscreen() {
        var noop = function() { return Promise ? Promise.resolve() : undefined; };
        var names = ["requestFullscreen", "requestFullScreen", "webkitRequestFullscreen",
                     "webkitRequestFullScreen", "mozRequestFullScreen", "msRequestFullscreen"];
        var protos = [];
        if (typeof Element !== "undefined") protos.push(Element.prototype);
        if (typeof HTMLElement !== "undefined") protos.push(HTMLElement.prototype);
        for (var p = 0; p < protos.length; p++) {
            for (var n = 0; n < names.length; n++) {
                if (typeof protos[p][names[n]] === "function") protos[p][names[n]] = noop;
            }
        }
        try {
            if (typeof nw !== "undefined" && nw.Window) {
                var win = nw.Window.get();
                win.enterFullscreen = function() {};
                win.enterKioskMode = function() {};
                win.maximize = function() {};
            }
        } catch (e) {}
    })();

    /* --- Frame-exact stepping ------------------------------------------ */
    function install() {
        if (ev.installed) return true;
        if (typeof SceneManager === "undefined" ||
            typeof SceneManager.updateMain !== "function") return false;
        var updateMainOrig = SceneManager.updateMain;
        SceneManager.updateMain = function() {
            if (ev.frozen) return;
            updateMainOrig.call(this);
            ev.frame++;
            tick();
        };
        ev.installed = true;
        return true;
    }

    function tick() {
        var w = ev.waiter;
        if (!w) return;
        var done = false, err = null;
        try {
            done = !!w.test();
        } catch (e) {
            err = e;
        }
        if (err) {
            ev.waiter = null;
            w.reject(err);
        } else if (done) {
            ev.waiter = null;
            if (w.freeze) ev.freeze();
            w.resolve(ev.frame);
        } else if (--w.timeout <= 0) {
            ev.waiter = null;
            if (w.freeze) ev.freeze();
            w.reject(new Error("visual-eval: timed out waiting for " + w.label +
                               " (" + JSON.stringify(ev.status()) + ")"));
        }
    }

    /* Resolves once the core scripts have defined SceneManager (after a
       reload the driver may start stepping before they have loaded). */
    function engineReady() {
        return new Promise(function(resolve, reject) {
            var tries = 0;
            (function poll() {
                if (install()) resolve();
                else if (++tries > 600) reject(new Error("visual-eval: SceneManager not ready"));
                else setTimeout(poll, 100);
            })();
        });
    }

    /* A game whose loop stopped (error screen, SceneManager.stop) never
       runs updateMain again, so a frame-counted wait would hang forever.
       Fail it after 5 s without progress and report the error on screen. */
    var stallCheckStarted = false;
    function startStallCheck() {
        if (stallCheckStarted) return;
        stallCheckStarted = true;
        var lastFrame = ev.frame, stalledMs = 0;
        setInterval(function() {
            if (!ev.waiter) { lastFrame = ev.frame; stalledMs = 0; return; }
            if (ev.frame !== lastFrame) { lastFrame = ev.frame; stalledMs = 0; return; }
            stalledMs += 1000;
            if (stalledMs >= 5000) {
                var w = ev.waiter;
                ev.waiter = null;
                var printed = "";
                try {
                    var el = typeof Graphics !== "undefined" && Graphics._errorPrinter;
                    printed = el ? String(el.innerHTML || el.textContent || "").replace(/<[^>]*>/g, " ") : "";
                } catch (e) {}
                w.reject(new Error("visual-eval: game loop stalled while waiting for " + w.label +
                                   (printed ? " (error screen: " + printed.trim() + ")" : "")));
            }
        }, 1000);
    }

    /* Run the game until test() is true, then freeze. */
    ev.waitFor = function(test, timeoutFrames, label) {
        if (ev.waiter) return Promise.reject(new Error("visual-eval: another wait is active"));
        return engineReady().then(function() {
            startStallCheck();
            ev.unfreeze();
            return new Promise(function(resolve, reject) {
                ev.waiter = {
                    test: test, timeout: timeoutFrames || 3600, label: label || "condition",
                    resolve: resolve, reject: reject, freeze: true
                };
            });
        });
    };

    /* A cheap fingerprint of what the scene looks like: every node's
       position, scale, opacity, visibility and source rectangle. Two frames
       with the same fingerprint are the same picture as far as the display
       list is concerned. */
    function sceneSignature() {
        var parts = [];
        function walk(c, depth) {
            if (!c || depth > 6 || parts.length > 400) return;
            var f = c._frame;
            parts.push(
                c.constructor.name + "|" +
                Math.round(c.x) + "," + Math.round(c.y) + "," +
                Math.round(c.width || 0) + "," + Math.round(c.height || 0) + "," +
                (c.scale ? (+c.scale.x).toFixed(2) + "," + (+c.scale.y).toFixed(2) : "") + "," +
                (+c.alpha).toFixed(2) + "," + (c.opacity !== undefined ? Math.round(c.opacity) : "") + "," +
                (c.rotation ? (+c.rotation).toFixed(3) : "") + "," + (c.visible ? 1 : 0) + "," +
                (f ? f.x + ":" + f.y + ":" + f.width + ":" + f.height : ""));
            if (c.children) {
                for (var i = 0; i < c.children.length; i++) walk(c.children[i], depth + 1);
            }
        }
        try {
            walk(SceneManager._scene, 0);
        } catch (e) {
            return "error";
        }
        return parts.join(";");
    }

    /* Run until the scene has looked the same for `quiet` frames (animations
       finished), or `maxFrames` have passed, then freeze. Screenshots taken
       after this show the same picture on both players even when the scene
       animates its way in on a wall-clock timer. */
    ev.waitStable = function(maxFrames, quiet) {
        var quietNeeded = quiet || 8;
        var last = null, same = 0, spent = 0;
        return ev.waitFor(function() {
            var sig = sceneSignature();
            same = (sig === last) ? same + 1 : 0;
            last = sig;
            spent++;
            return same >= quietNeeded || spent >= (maxFrames || 180);
        }, (maxFrames || 180) + 60, "a still scene");
    };

    /* Advance exactly n logical frames, then freeze. */
    ev.waitFrames = function(n) {
        var target = ev.frame + Math.max(1, n | 0);
        return ev.waitFor(function() { return ev.frame >= target; }, n + 60, n + " frames");
    };

    ev.unfreeze = function() { ev.frozen = false; };
    ev.freeze = function() { ev.frozen = true; };

    /* --- Scene helpers ------------------------------------------------- */
    ev.sceneName = function() {
        var s = typeof SceneManager !== "undefined" ? SceneManager._scene : null;
        return s ? s.constructor.name : "none";
    };

    /* True if the active scene is `name` or a subclass of it (plugins often
       replace scenes with subclasses that are not globals). */
    ev.sceneIs = function(name) {
        var s = SceneManager._scene;
        if (!s) return false;
        var cls = window[name];
        if (typeof cls === "function" && s instanceof cls) return true;
        for (var p = Object.getPrototypeOf(s); p; p = Object.getPrototypeOf(p)) {
            if (p.constructor && p.constructor.name === name) return true;
        }
        return false;
    };

    ev.sceneReady = function(name) {
        var s = SceneManager._scene;
        return !!s && (!name || ev.sceneIs(name)) && s.isActive() &&
            !SceneManager.isSceneChanging() && !(s.isBusy && s.isBusy());
    };

    /* Wait for a scene; with nudgeKey, press that key every nudgeEvery
       frames while waiting (title screens behind "press any key" or
       pre-title map scenes). */
    /* The key a nudge should press now: on an active selectable window
       whose cursor rests on nothing yet, "down" (puts it on the first
       entry); otherwise "ok" (confirms the first entry, or dismisses a
       "press any key" screen). Same choice on both players, whatever frame
       the nudge lands on. */
    function nudgeKeyFor(fallback) {
        var s = SceneManager._scene;
        var layer = s && s._windowLayer;
        if (layer && layer.children) {
            for (var i = 0; i < layer.children.length; i++) {
                var w = layer.children[i];
                if (w && w.active && typeof w.index === "function" && w.visible !== false) {
                    return w.index() < 0 ? "down" : "ok";
                }
            }
        }
        return fallback;
    }

    ev.waitScene = function(name, timeoutFrames, nudgeKeys, nudgeEvery) {
        var every = nudgeEvery || 120;
        var keys = !nudgeKeys ? [] : (Array.isArray(nudgeKeys) ? nudgeKeys : [nudgeKeys]);
        var lastPress = -every, next = 0, code = 0;
        return ev.waitFor(function() {
            if (ev.sceneReady(name)) return true;
            /* Once the wanted scene exists, let it finish opening: a key
               pressed while it fades in would act on it (a title screen
               would start the game). */
            if (keys.length && !ev.sceneIs(name)) {
                if (ev.frame - lastPress >= every) {
                    var key = keys.length === 1 ? nudgeKeyFor(keys[0]) : keys[next % keys.length];
                    code = keyCodeOf(key);
                    next++;
                    keyEvent("keydown", code);
                    lastPress = ev.frame;
                } else if (ev.frame - lastPress === 2) {
                    keyEvent("keyup", code);
                }
            }
            return false;
        }, timeoutFrames, name);
    };

    ev.waitIdle = function(timeoutFrames) {
        return ev.waitFor(function() { return ev.sceneReady(null); }, timeoutFrames, "idle scene");
    };

    ev.hasScene = function(name) {
        return typeof window[name] === "function";
    };

    /* Scenes that show one actor read $gameParty.menuActor(), which the menu
       sets when the player picks a character. Pushing them straight from a
       script would leave it null and crash the scene (in either player), so
       select the first party member first, as the menu would. */
    var ACTOR_SCENES = { Scene_Skill: 1, Scene_Equip: 1, Scene_Status: 1 };
    function prepareActorScene(name) {
        if (!ACTOR_SCENES[name]) return;
        try {
            if (typeof $gameParty !== "undefined" && $gameParty && !$gameParty.menuActor()) {
                var members = $gameParty.members();
                if (members.length) $gameParty.setMenuActor(members[0]);
            }
        } catch (e) {}
    }

    /* Push a scene by class name; args go to SceneManager.prepareNextScene. */
    ev.pushScene = function(name, args) {
        if (!ev.hasScene(name)) return false;
        prepareActorScene(name);
        ev.unfreeze();
        SceneManager.push(window[name]);
        if (args && args.length) SceneManager.prepareNextScene.apply(SceneManager, args);
        return true;
    };

    ev.gotoScene = function(name, args) {
        if (!ev.hasScene(name)) return false;
        prepareActorScene(name);
        ev.unfreeze();
        SceneManager.goto(window[name]);
        if (args && args.length) SceneManager.prepareNextScene.apply(SceneManager, args);
        return true;
    };

    ev.popScene = function() {
        ev.unfreeze();
        SceneManager.pop();
        return true;
    };

    ev.newGame = function() {
        ev.unfreeze();
        DataManager.setupNewGame();
        SceneManager.goto(Scene_Map);
        return true;
    };

    ev.startBattle = function(troopId) {
        if (!$dataTroops || !$dataTroops[troopId]) return false;
        ev.unfreeze();
        BattleManager.setup(troopId, true, true);
        $gamePlayer.makeEncounterCount();
        SceneManager.push(Scene_Battle);
        return true;
    };

    ev.showMessage = function(text) {
        ev.unfreeze();
        var lines = String(text).split("\n");
        for (var i = 0; i < lines.length; i++) $gameMessage.add(lines[i]);
        return true;
    };

    /* First `count` items with a name, as Scene_Shop goods entries. */
    ev.shopGoods = function(count) {
        var goods = [];
        for (var i = 1; i < $dataItems.length && goods.length < count; i++) {
            if ($dataItems[i] && $dataItems[i].name) goods.push([0, i, 0, 0]);
        }
        return goods;
    };

    /* --- Input --------------------------------------------------------- */
    var KEYS = {
        ok: 90, cancel: 88, shift: 16, menu: 27, escape: 27, enter: 13, space: 32,
        up: 38, down: 40, left: 37, right: 39, pageup: 33, pagedown: 34,
        control: 17, tab: 9, debug: 120
    };

    function keyEvent(type, code) {
        var e;
        try {
            e = new KeyboardEvent(type, { keyCode: code, bubbles: true, cancelable: true });
        } catch (err) {
            e = document.createEvent("Event");
            e.initEvent(type, true, true);
        }
        if (e.keyCode !== code) {
            try { Object.defineProperty(e, "keyCode", { value: code }); } catch (err2) {}
        }
        document.dispatchEvent(e);
    }

    function keyCodeOf(name) {
        return KEYS[name] !== undefined ? KEYS[name] : parseInt(name, 10);
    }

    /* Hold a key for `hold` frames, then run `after` frames more, frozen at
       the end. Key names map to RMMZ's default keyboard layout. */
    ev.pressKey = function(name, hold, after) {
        var code = keyCodeOf(name);
        if (!code) return Promise.reject(new Error("visual-eval: unknown key " + name));
        hold = Math.max(1, hold | 0);
        keyEvent("keydown", code);
        return ev.waitFrames(hold).then(function() {
            keyEvent("keyup", code);
            return ev.waitFrames(Math.max(1, after | 0));
        });
    };

    ev.status = function() {
        var s = typeof SceneManager !== "undefined" ? SceneManager._scene : null;
        var status = {
            scene: ev.sceneName(), frame: ev.frame, frozen: ev.frozen,
            installed: ev.installed, storagePatched: storagePatched,
            injectedAt: ev.injectedAt,
            width: typeof Graphics !== "undefined" ? Graphics.width : 0,
            height: typeof Graphics !== "undefined" ? Graphics.height : 0
        };
        if (s) {
            try {
                status.changing = SceneManager.isSceneChanging();
                status.next = SceneManager._nextScene ? SceneManager._nextScene.constructor.name : null;
                status.started = s.isStarted();
                status.active = s.isActive();
                status.ready = s.isReady();
                status.busy = !!(s.isBusy && s.isBusy());
                status.imagesReady = ImageManager.isReady();
                status.fontsReady = FontManager.isReady();
            } catch (e) {
                status.error = String(e);
            }
        }
        return status;
    };

    window.__ev = ev;
})();
