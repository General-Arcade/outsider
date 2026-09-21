# Copyright (c) 2026 General Arcade (Pte. Ltd.)
# SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
"""Scenario steps and the runner that plays them on one driver.

A scenario is a JSON file: {"name": ..., "steps": [step, ...]}. Each step is
an object with one action key plus optional modifiers. Actions:

    wait_scene: "Scene_Title"     run until that scene (or a subclass) is
                                  active and not fading
    wait: 30                      advance exactly N game frames
    key: "ok" | keys: [...]       press keys (hold/after modifiers, frames)
    scene: "Scene_Item"           push the scene, wait for it, screenshot,
                                  pop it again (unless keep: true);
                                  args: [..] -> SceneManager.prepareNextScene
    pop: true                     SceneManager.pop() and wait for the scene
    goto: "Scene_Map"             SceneManager.goto(...)
    new_game: true                DataManager.setupNewGame + goto Scene_Map
    battle: 1 | "auto"            start a battle against troop N ("auto":
                                  first troop whose battler images exist)
    message: "text"               show a message window on the map
    eval: "js"                    run arbitrary JS (unfreezes the game first)

Modifiers: shot: "name" takes a screenshot after the action (after settle
frames, default 30, plus up to `stabilize` more frames, off by default,
until the scene stops changing: useful for menus that animate themselves in,
harmful for scenes that never stand still, since the extra frames let the two
players drift apart); if: "js expr" skips the step when false; timeout in
seconds for waits (default 60); nudge: "ok" presses a key every nudge_every
frames (default 120) while a wait_scene has not been reached, for "press any
key" or pre-title scenes (a single key is chosen per press: "down" when the
active window's cursor rests on nothing, else the key given; a list is
pressed in rotation).

The game only advances inside actions and is frozen between them, so the
frame a screenshot shows depends on the steps alone, not on how long the
driver takes to send the next command.
"""

import json
import os
import time

from .drivers import DriverError, JsError

DEFAULT_SETTLE = 30
DEFAULT_STABILIZE = 0     # extra frames a shot may wait for animations to end
DEFAULT_TIMEOUT = 60.0
FPS = 60


def load_scenario(path):
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    if "steps" not in data or not isinstance(data["steps"], list):
        raise ValueError("%s: scenario needs a \"steps\" list" % path)
    for i, step in enumerate(data["steps"]):
        if not isinstance(step, dict):
            raise ValueError("%s: step %d is not an object" % (path, i))
        if not [k for k in step if k in ACTIONS]:
            raise ValueError("%s: step %d has no action (%s)" % (path, i, ", ".join(sorted(ACTIONS))))
    data.setdefault("name", os.path.splitext(os.path.basename(path))[0])
    return data


def js_str(value):
    return json.dumps(value)


class StepResult:
    def __init__(self, index, step):
        self.index = index
        self.step = step
        self.status = "ok"      # ok | skipped | error
        self.detail = ""
        self.shot = None        # screenshot name when one was taken
        self.seconds = 0.0

    def as_dict(self):
        return {"index": self.index, "step": self.step, "status": self.status,
                "detail": self.detail, "shot": self.shot, "seconds": round(self.seconds, 2)}


class Runner:
    """Plays scenario steps on one driver, saving screenshots to out_dir."""

    def __init__(self, driver, out_dir, log=print, env=None):
        self.driver = driver
        self.out_dir = out_dir
        self.log = log
        self.env = env or {}     # values resolved by the CLI, e.g. auto_troop
        self.results = []
        os.makedirs(out_dir, exist_ok=True)

    # --- primitives ------------------------------------------------------

    def _frames_timeout(self, step):
        return int(float(step.get("timeout", DEFAULT_TIMEOUT)) * FPS)

    def _eval(self, js, step=None, timeout=None):
        # Waits are counted in game frames; give the wall clock room for a
        # game running well below 60 fps before declaring the driver dead.
        if timeout is None:
            timeout = float(step.get("timeout", DEFAULT_TIMEOUT)) * 1.5 + 15.0 if step else DEFAULT_TIMEOUT
        return self.driver.evaluate(js, timeout=timeout)

    def wait_scene(self, name, step):
        nudge = step.get("nudge")
        args = "%s, %d" % (js_str(name), self._frames_timeout(step))
        if nudge:
            args += ", %s, %d" % (json.dumps(nudge), int(step.get("nudge_every", 120)))
        return self._eval("__ev.waitScene(%s)" % args, step)

    def wait_idle(self, step):
        return self._eval("__ev.waitIdle(%d)" % self._frames_timeout(step), step)

    def wait_frames(self, n, step):
        return self._eval("__ev.waitFrames(%d)" % int(n), step, timeout=int(n) / FPS * 1.5 + 30.0)

    def press(self, key, step):
        hold = int(step.get("hold", 2))
        after = int(step.get("after", 6))
        return self._eval("__ev.pressKey(%s, %d, %d)" % (js_str(key), hold, after), step,
                          timeout=(hold + after) / FPS * 1.5 + 30.0)

    def screenshot(self, name):
        path = os.path.join(self.out_dir, name + ".png")
        self.driver.screenshot(path)
        return path

    # --- actions ---------------------------------------------------------

    def run(self, steps):
        for index, step in enumerate(steps):
            result = StepResult(index, step)
            started = time.time()
            self.log("  [%s] step %d: %s" % (self.driver.name, index, describe(step)))
            try:
                self._run_step(step, result)
            except JsError as e:
                result.status = "error"
                result.detail = first_line(str(e), 3)
                self.log("    error: %s" % result.detail)
            except DriverError as e:
                result.status = "error"
                result.detail = str(e)
                result.seconds = time.time() - started
                self.results.append(result)
                self.log("    driver failed: %s" % e)
                self._abort_remaining(steps, index + 1, str(e))
                return self.results
            result.seconds = time.time() - started
            self.results.append(result)
        return self.results

    def _abort_remaining(self, steps, start, reason):
        for index in range(start, len(steps)):
            result = StepResult(index, steps[index])
            result.status = "error"
            result.detail = "not run: " + reason
            self.results.append(result)

    def _run_step(self, step, result):
        if "if" in step:
            if not self._eval("!!(%s)" % step["if"], step):
                result.status = "skipped"
                result.detail = "condition false: %s" % step["if"]
                return

        if "wait_scene" in step:
            self.wait_scene(step["wait_scene"], step)
        elif "wait" in step and "key" not in step and "keys" not in step:
            self.wait_frames(step["wait"], step)
        elif "key" in step or "keys" in step:
            keys = step.get("keys") or [step["key"]]
            for key in keys:
                self.press(key, step)
            if "wait" in step:
                self.wait_frames(step["wait"], step)
        elif "scene" in step:
            if not self._scene_step(step, result):
                return
        elif "pop" in step:
            self._eval("__ev.popScene()", step)
            self.wait_idle(step)
        elif "goto" in step:
            if not self._eval("__ev.gotoScene(%s, %s)" % (js_str(step["goto"]), self._args(step)), step):
                result.status = "skipped"
                result.detail = "scene class %s not defined" % step["goto"]
                return
            self.wait_scene(step["goto"], step)
        elif "new_game" in step:
            self._eval("__ev.newGame()", step)
            self.wait_scene("Scene_Map", step)
        elif "battle" in step:
            troop = step["battle"]
            if troop == "auto":
                troop = self.env.get("auto_troop")
                if not troop:
                    result.status = "skipped"
                    result.detail = "no troop with shipped battler images"
                    return
            if not self._eval("__ev.startBattle(%d)" % int(troop), step):
                result.status = "skipped"
                result.detail = "troop %s does not exist" % troop
                return
            self.wait_scene("Scene_Battle", step)
        elif "message" in step:
            self._eval("__ev.showMessage(%s)" % js_str(step["message"]), step)
        elif "eval" in step:
            value = self._eval("(__ev.unfreeze(), (%s))" % step["eval"], step)
            result.detail = "-> %s" % first_line(json.dumps(value))
            if "wait" in step:
                self.wait_frames(step["wait"], step)

        if "shot" in step:
            self._settle_and_shoot(step, result)

        if "scene" in step and not step.get("keep"):
            self._eval("__ev.popScene()", step)
            self.wait_idle(step)

    def _args(self, step):
        args = step.get("args")
        if args is None:
            return "null"
        if isinstance(args, str):
            return args           # a JS expression producing the array
        return json.dumps(args)

    def _scene_step(self, step, result):
        name = step["scene"]
        pushed = self._eval("__ev.pushScene(%s, %s)" % (js_str(name), self._args(step)), step)
        if not pushed:
            result.status = "skipped"
            result.detail = "scene class %s not defined" % name
            return False
        self.wait_scene(name, step)
        return True

    def _settle_and_shoot(self, step, result):
        settle = int(step.get("settle", DEFAULT_SETTLE))
        if settle > 0:
            self.wait_frames(settle, step)
        else:
            self._eval("__ev.freeze()", step)
        # Scenes that animate themselves in (menus built with tween libraries)
        # are not done after a fixed number of frames, and the same count of
        # frames is not the same amount of animation on both players. Give
        # them extra frames until the picture stops changing.
        stabilize = int(step.get("stabilize", DEFAULT_STABILIZE))
        if stabilize > 0:
            try:
                self._eval("__ev.waitStable(%d, 8)" % stabilize, step,
                           timeout=stabilize / FPS * 1.5 + 30.0)
            except JsError as e:
                # A scene that never stops moving is still worth a picture.
                result.detail = "unsettled: %s" % first_line(str(e))
        scene = self._eval("__ev.sceneName()", step)
        self.screenshot(step["shot"])
        result.shot = step["shot"]
        result.detail = (result.detail + " " if result.detail else "") + "scene=%s" % scene


def describe(step):
    parts = []
    for key, value in step.items():
        text = json.dumps(value) if not isinstance(value, str) else value
        if len(text) > 50:
            text = text[:47] + "..."
        parts.append("%s=%s" % (key, text))
    return " ".join(parts)


def first_line(text, lines=1):
    """The first line of an error, or the first few (message plus the top of
    the stack) when ``lines`` is larger."""
    parts = [p.strip() for p in text.splitlines() if p.strip()] if text else []
    return " | ".join(parts[:lines])


ACTIONS = {"wait_scene", "wait", "key", "keys", "scene", "pop", "goto",
           "new_game", "battle", "message", "eval", "shot"}
