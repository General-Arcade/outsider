/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

/*
 * Control mode (--control): the game loop reads one command per line from
 * stdin and answers on stdout with lines of the form
 *
 *     @@ctl <id> ok <json>
 *     @@ctl <id> err <json string>
 *
 * "eval" commands arrive here as a JSON-encoded source string. The source is
 * evaluated in global scope; a returned promise is awaited before replying.
 * Only loaded when the runtime starts in control mode.
 */

(function() {
    "use strict";

    function send(id, status, value) {
        var text;
        try {
            text = JSON.stringify(value === undefined ? null : value);
        } catch (e) {
            text = JSON.stringify(String(value));
        }
        if (text === undefined) text = "null";
        __native_control_send("@@ctl " + id + " " + status + " " + text);
    }

    function errorText(e) {
        if (e && e.stack) return String(e.message || e) + "\n" + e.stack;
        return String(e);
    }

    globalThis.__control_eval = function(id, json) {
        var value;
        try {
            value = (0, eval)(JSON.parse(json));
        } catch (e) {
            send(id, "err", errorText(e));
            return;
        }
        if (value && typeof value.then === "function") {
            value.then(function(result) {
                send(id, "ok", result);
            }, function(e) {
                send(id, "err", errorText(e));
            });
        } else {
            send(id, "ok", value);
        }
    };
})();
