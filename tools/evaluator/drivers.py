# Copyright (c) 2026 General Arcade (Pte. Ltd.)
# SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
"""Players the evaluator can drive.

Both drivers offer the same three operations, so the scenario runner does
not care which player it is talking to:

    evaluate(js, timeout)  -> JSON value of the expression (promises awaited)
    screenshot(png_path)   -> writes the current frame as PNG
    close()

NwjsDriver launches the game's own NW.js player (Game.exe next to
package.json) with a remote debugging port and talks Chrome DevTools
Protocol. OutsiderDriver launches outsider.exe --control and talks the
line protocol documented in src/engine/game_loop.h.
"""

import json
import os
import queue
import re
import socket
import subprocess
import threading
import time

from . import cdp


class DriverError(Exception):
    pass


class JsError(DriverError):
    """The evaluated script threw; message carries the JS error text."""


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def steam_app_id(game_dir):
    """App id of a game installed under steamapps/common/<dir>, from Steam's
    appmanifest files; None when the folder is not a Steam install."""
    parts = os.path.abspath(game_dir).replace("\\", "/").split("/")
    lower = [p.lower() for p in parts]
    if "steamapps" not in lower or "common" not in lower:
        return None
    i = lower.index("common")
    if i + 1 >= len(parts) or lower[i - 1] != "steamapps":
        return None
    install_dir = parts[i + 1]
    steamapps = "/".join(parts[:i])
    try:
        names = os.listdir(steamapps)
    except OSError:
        return None
    for name in names:
        if not (name.startswith("appmanifest_") and name.endswith(".acf")):
            continue
        try:
            with open(os.path.join(steamapps, name), "r", encoding="utf-8", errors="replace") as f:
                text = f.read()
        except OSError:
            continue
        m = re.search(r'"installdir"\s+"([^"]+)"', text)
        if m and m.group(1) == install_dir:
            m = re.search(r'"appid"\s+"(\d+)"', text)
            if m:
                return m.group(1)
    return None


def kill_tree(proc):
    """Terminate a process and (on Windows) its children."""
    if os.name == "nt":
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        proc.kill()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        pass


class NwjsDriver:
    name = "original"

    def __init__(self, game_dir, prelude, log_path=None, exe=None, port=None,
                 boot_timeout=60.0, profile_dir=None):
        self.game_dir = os.path.abspath(game_dir)
        exe = exe or self._find_exe(self.game_dir)
        self.port = port or free_port()
        args = [
            exe,
            "--remote-debugging-port=%d" % self.port,
            "--force-device-scale-factor=1",     # window pixels == game pixels
            "--force-color-profile=srgb",
            "--disable-backgrounding-occluded-windows",
            "--disable-renderer-backgrounding",
        ]
        if profile_dir:
            # Keep the run out of the player's real profile (and away from
            # any other instance of the same game that may still be running).
            args.append("--user-data-dir=%s" % os.path.abspath(profile_dir))
        # Greenworks initialises Steam with steam_appid.txt, which for a demo
        # may name the full game; tell the Steam API which app this really is
        # so the original does not stop on an error screen.
        env = dict(os.environ)
        app_id = steam_app_id(self.game_dir)
        if app_id:
            env["SteamAppId"] = app_id
            env["SteamGameId"] = app_id
        self.log = open(log_path, "wb") if log_path else subprocess.DEVNULL
        self.proc = subprocess.Popen(args, cwd=self.game_dir, stdout=self.log,
                                     stderr=subprocess.STDOUT, env=env)
        try:
            target = cdp.wait_for_page(self.port, timeout=boot_timeout)
            self.session = cdp.CdpSession(target["webSocketDebuggerUrl"])
            self.session.call("Page.enable")
            # The player has already started booting by the time we attach;
            # install the prelude for the next document and reload so the
            # game starts over with it in place.
            self.session.call("Page.addScriptToEvaluateOnNewDocument", source=prelude)
            self.session.call("Page.reload", ignoreCache=True)
            self._wait_for_prelude(boot_timeout)
        except Exception:
            self.close()
            raise

    @staticmethod
    def _find_exe(game_dir):
        for name in ("Game.exe", "nw.exe", "nw"):
            path = os.path.join(game_dir, name)
            if os.path.isfile(path):
                return path
        raise DriverError("No NW.js player (Game.exe/nw.exe) in %s" % game_dir)

    def _wait_for_prelude(self, timeout):
        """Wait until the reloaded document has the prelude and the core
        scripts (SceneManager) loaded."""
        deadline = time.time() + timeout
        last = None
        probe = "typeof __ev === 'object' && typeof SceneManager !== 'undefined'"
        while time.time() < deadline:
            try:
                if self.session.evaluate(probe, await_promise=False):
                    return
            except cdp.CdpError as e:
                last = e
            time.sleep(0.2)
        raise DriverError("Prelude never appeared after reload (%s)" % last)

    def evaluate(self, js, timeout=60.0):
        if self.proc.poll() is not None:
            raise DriverError("original: player exited (code %s)" % self.proc.poll())
        self.session.ws.sock.settimeout(timeout + 5.0)
        try:
            return self.session.evaluate(js)
        except cdp.CdpError as e:
            if "WebSocket closed" in str(e):
                raise DriverError("original: DevTools connection closed (player exited?)")
            raise JsError(str(e))
        except socket.timeout:
            raise DriverError("original: no reply within %.0fs" % timeout)
        except OSError as e:
            raise DriverError("original: DevTools connection lost (%s)" % e)

    def screenshot(self, png_path):
        data = self.session.screenshot_png()
        with open(png_path, "wb") as f:
            f.write(data)

    def close(self):
        session = getattr(self, "session", None)
        if session:
            try:
                session.call("Runtime.evaluate", expression="nw.App.quit()")
            except Exception:
                pass
            try:
                session.close()
            except Exception:
                pass
            self.session = None
        try:
            self.proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            pass
        kill_tree(self.proc)      # helper processes may outlive the main one
        if self.log is not subprocess.DEVNULL:
            self.log.close()


class OutsiderDriver:
    name = "outsider"

    def __init__(self, exe, game_dir, prelude, width, height, shims_dir=None,
                 log_path=None, cwd=None, extra_args=(), boot_timeout=60.0):
        args = [os.path.abspath(exe), os.path.abspath(game_dir), "--control",
                "--resolution", "%dx%d" % (width, height), "--no-audio"]
        if shims_dir:
            args += ["--shims", os.path.abspath(shims_dir)]
        args += list(extra_args)
        self.log = open(log_path, "wb") if log_path else None
        self.proc = subprocess.Popen(args, cwd=cwd, stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.replies = queue.Queue()
        self.pending = {}
        self.next_id = 1
        self.reader = threading.Thread(target=self._read_stdout, daemon=True)
        self.reader.start()
        # The first command unblocks frame 0, so the prelude runs before the
        # game has advanced at all.
        try:
            self.evaluate(prelude + "\n;typeof __ev", timeout=boot_timeout)
        except Exception:
            self.close()
            raise

    def _read_stdout(self):
        for raw in self.proc.stdout:
            line = raw.decode("utf-8", "replace").rstrip("\r\n")
            if line.startswith("@@ctl "):
                self.replies.put(line)
            elif self.log:
                self.log.write(raw)
                self.log.flush()
        self.replies.put(None)

    def _send(self, command, payload):
        msg_id = self.next_id
        self.next_id += 1
        line = "%d %s %s\n" % (msg_id, command, payload)
        try:
            self.proc.stdin.write(line.encode("utf-8"))
            self.proc.stdin.flush()
        except (OSError, ValueError):
            raise DriverError("outsider: process is gone (exit code %s)" % self.proc.poll())
        return msg_id

    def _wait_reply(self, msg_id, timeout):
        deadline = time.time() + timeout
        while True:
            if msg_id in self.pending:
                return self.pending.pop(msg_id)
            remaining = deadline - time.time()
            if remaining <= 0:
                raise DriverError("outsider: no reply to command %d within %.0fs" % (msg_id, timeout))
            try:
                line = self.replies.get(timeout=remaining)
            except queue.Empty:
                continue
            if line is None:
                raise DriverError("outsider: process exited (code %s)" % self.proc.poll())
            _, rid, status, body = line.split(" ", 3)
            self.pending[int(rid)] = (status, body)

    def _command(self, command, payload, timeout):
        msg_id = self._send(command, payload)
        status, body = self._wait_reply(msg_id, timeout)
        try:
            value = json.loads(body)
        except ValueError:
            value = body
        if status != "ok":
            raise JsError(str(value))
        return value

    def evaluate(self, js, timeout=60.0):
        return self._command("eval", json.dumps(js), timeout)

    def screenshot(self, png_path):
        from PIL import Image
        ppm_path = os.path.splitext(png_path)[0] + ".ppm"
        self._command("shot", os.path.abspath(ppm_path), 30.0)
        Image.open(ppm_path).save(png_path)
        os.remove(ppm_path)

    def close(self):
        if self.proc.poll() is None:
            try:
                self._command("quit", "", 5.0)
            except DriverError:
                pass
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                kill_tree(self.proc)
        try:
            self.proc.stdin.close()
        except OSError:
            pass
        if self.log:
            self.log.close()
            self.log = None
