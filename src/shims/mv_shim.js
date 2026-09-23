/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
 */

/*
 * mv_shim.js — Post-core overrides for RPG Maker MV, the counterpart to
 * tilemap_shim.js. Loads after rpg_core.js and the plugins.
 *
 * MV targets PIXI 4, where a display object draws itself from _renderWebGL();
 * the shim's scene graph calls _render(), PIXI 5's name for the same hook.
 * The classes MV gives a custom body to therefore need routing here.
 */
(function() {
    "use strict";

    if (typeof Graphics === "undefined") {
        return;
    }

    try {

    /* PIXI 4 drew through _renderWebGL; bridge it to the hook the scene graph
       actually calls, so an MV class with a custom body is not skipped. */
    if (typeof PIXI !== "undefined" && PIXI.Container) {
        var proto = PIXI.Container.prototype;
        var base_render = proto._render;
        proto._render = function(renderer) {
            if (typeof this._renderWebGL === "function") {
                this._renderWebGL(renderer);
                return;
            }
            if (base_render) base_render.call(this, renderer);
        };
    }

    /* ---- Fonts ----
       MV declares its fonts in fonts/gamefont.css and links that from
       index.html, so a browser has them registered before any script runs.
       Nothing here parses CSS, and Scene_Boot will not advance until
       Graphics.isFontLoaded('GameFont') is true, so read the file and
       register each @font-face through the FontFace API the shim provides.
       MZ needs none of this: it loads fonts from JS through FontManager. */
    function loadGameFontCss(cssPath) {
        var text = null;
        try {
            var xhr = new XMLHttpRequest();
            xhr.open("GET", cssPath, false);
            xhr.send();
            if (xhr.status === 0 || (xhr.status >= 200 && xhr.status < 300)) {
                text = xhr.responseText;
            }
        } catch (e) {
            return 0;
        }
        if (!text) return 0;

        /* url() in the stylesheet is relative to the stylesheet itself. */
        var baseDir = cssPath.replace(/[^/]*$/, "");
        var count = 0;
        var faceRe = /@font-face\s*\{([^}]*)\}/g;
        var face;

        while ((face = faceRe.exec(text)) !== null) {
            var body = face[1];
            var famM = body.match(/font-family\s*:\s*['"]?([^;'"]+?)['"]?\s*(?:;|$)/);
            var srcM = body.match(/url\s*\(\s*['"]?([^'")]+?)['"]?\s*\)/);
            if (!famM || !srcM) continue;

            var family = famM[1].trim();
            var url = srcM[1].trim();
            /* Leave absolute and already-rooted urls alone. */
            if (!/^([a-z]+:)?\//i.test(url)) url = baseDir + url;

            try {
                var ff = new FontFace(family, 'url(' + url + ')');
                if (document.fonts && document.fonts.add) document.fonts.add(ff);
                count++;
            } catch (e) {
                console.log("[mv_shim] font '" + family + "' failed: " + e);
            }
        }
        return count;
    }

    var fontsLoaded = loadGameFontCss("fonts/gamefont.css");

    /* ---- WindowLayer ----
       MV masks each window to the layer by driving PIXI 4's maskManager
       (pushScissorMask / popScissorMask) and poking renderer.currentRenderer
       between children. None of that exists here. Windows already clip their
       own contents, and Container.render applies a scissor for filterArea, so
       rendering the children in order gives the same picture -- which is the
       same resolution tilemap_shim.js reaches for MZ. */
    if (typeof WindowLayer !== "undefined") {
        WindowLayer.prototype.render = function(renderer) {
            if (!this.visible) return;
            for (var i = 0; i < this.children.length; i++) {
                var child = this.children[i];
                if (child.visible) child.render(renderer);
            }
        };
        /* The bridge above routes _render to _renderWebGL, so both names have
           to lead somewhere sane. */
        WindowLayer.prototype._renderWebGL = function(renderer) {
            for (var i = 0; i < this.children.length; i++) {
                var child = this.children[i];
                if (child.visible) child.render(renderer);
            }
        };
        WindowLayer.prototype._renderCanvas = WindowLayer.prototype._renderWebGL;
    }

    console.log("[mv_shim] loaded (" + fontsLoaded + " font faces)");

    } catch (e) {
        console.log("[mv_shim] error: " + e);
    }
})();
