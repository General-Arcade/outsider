#!/usr/bin/env python3
# Copyright (c) 2026 General Arcade (Pte. Ltd.)
# SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
"""Tests for tools/evaluator (the visual evaluator): WebSocket framing,
screenshot comparison, scenario validation, troop selection and the
Outsider control-protocol client. Nothing here launches a game.

Run: python tests/test_visual_eval.py
"""

import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent.parent / "tools"
sys.path.insert(0, str(TOOLS_DIR))

from evaluator import cdp, compare, scenario  # noqa: E402
from evaluator.cli import pick_troop, game_resolution  # noqa: E402
from evaluator.drivers import OutsiderDriver, JsError, DriverError  # noqa: E402

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    Image = None


class TestWebSocketFraming(unittest.TestCase):
    def roundtrip(self, payload):
        frame = cdp.encode_frame(payload)
        decoded = cdp.decode_frame(bytearray(frame))
        self.assertIsNotNone(decoded)
        fin, opcode, data, consumed = decoded
        self.assertTrue(fin)
        self.assertEqual(opcode, 0x1)
        self.assertEqual(consumed, len(frame))
        return data

    def test_short_medium_long_lengths(self):
        for n in (0, 1, 125, 126, 1000, 65535, 65536, 200000):
            payload = ("x" * n).encode()
            self.assertEqual(self.roundtrip(payload), payload)

    def test_client_frames_are_masked(self):
        frame = cdp.encode_frame("hello")
        self.assertTrue(frame[1] & 0x80)
        self.assertNotEqual(frame[6:], b"hello")

    def test_unmasked_server_frame(self):
        payload = b'{"id":1}'
        frame = bytes([0x81, len(payload)]) + payload
        fin, opcode, data, consumed = cdp.decode_frame(bytearray(frame))
        self.assertEqual(data, payload)
        self.assertEqual(consumed, len(frame))

    def test_incomplete_frame_returns_none(self):
        frame = cdp.encode_frame("hello world")
        self.assertIsNone(cdp.decode_frame(bytearray(frame[:-3])))
        self.assertIsNone(cdp.decode_frame(bytearray(b"\x81")))

    def test_sixteen_bit_length_header(self):
        payload = b"a" * 300
        frame = bytes([0x81, 126]) + struct.pack(">H", 300) + payload
        self.assertEqual(cdp.decode_frame(bytearray(frame))[2], payload)


@unittest.skipIf(Image is None, "Pillow not installed")
class TestCompare(unittest.TestCase):
    def make(self, color, size=(96, 72)):
        return Image.new("RGB", size, color)

    def test_identical_images_match(self):
        a = self.make((10, 20, 30))
        metrics, mask = compare.compare_images(a, a.copy(), tolerance=0)
        self.assertEqual(metrics["changed_pixels"], 0)
        self.assertEqual(metrics["changed_pct"], 0.0)
        self.assertIsNone(metrics["bbox"])
        self.assertEqual(metrics["regions"], [])
        self.assertEqual(compare.verdict(metrics), "match")

    def test_tolerance_hides_small_differences(self):
        a = self.make((100, 100, 100))
        b = self.make((110, 100, 100))
        metrics, _ = compare.compare_images(a, b, tolerance=24)
        self.assertEqual(metrics["changed_pixels"], 0)
        metrics, _ = compare.compare_images(a, b, tolerance=5)
        self.assertEqual(metrics["changed_pixels"], 96 * 72)
        self.assertEqual(compare.verdict(metrics), "differ")

    def test_region_is_located(self):
        a = self.make((0, 0, 0))
        b = a.copy()
        b.paste((255, 255, 255), (48, 24, 72, 48))     # 24x24 block
        metrics, mask = compare.compare_images(a, b)
        self.assertEqual(metrics["changed_pixels"], 24 * 24)
        self.assertEqual(metrics["bbox"], [48, 24, 72, 48])
        self.assertEqual(len(metrics["regions"]), 1)
        x, y, w, h, pct = metrics["regions"][0]
        self.assertEqual((x, y, w, h), (48, 24, 24, 24))
        self.assertEqual(pct, 100.0)
        self.assertEqual(compare.verdict(metrics), "differ")   # 8.3% of pixels

    def test_separate_regions_are_separate(self):
        a = self.make((0, 0, 0), (240, 240))
        b = a.copy()
        b.paste((255, 0, 0), (0, 0, 24, 24))
        b.paste((255, 0, 0), (216, 216, 240, 240))
        metrics, _ = compare.compare_images(a, b)
        self.assertEqual(len(metrics["regions"]), 2)

    def test_size_mismatch_is_reported(self):
        a = self.make((0, 0, 0), (96, 72))
        b = self.make((0, 0, 0), (48, 36))
        metrics, _ = compare.compare_images(a, b)
        self.assertEqual(metrics["size_mismatch"], [48, 36])
        self.assertEqual(compare.verdict(metrics), "differ")

    def test_minor_verdict(self):
        a = self.make((0, 0, 0), (100, 100))
        b = a.copy()
        b.paste((255, 255, 255), (0, 0, 10, 10))       # 1% of pixels
        metrics, _ = compare.compare_images(a, b)
        self.assertEqual(compare.verdict(metrics), "minor")

    def test_diff_visual_layout(self):
        a = self.make((0, 0, 0))
        b = self.make((255, 255, 255))
        metrics, mask = compare.compare_images(a, b)
        out = compare.diff_visual(a, b, mask)
        self.assertEqual(out.size, (96 * 3 + 8, 72))
        self.assertEqual(out.getpixel((96 * 2 + 8 + 5, 5)), (255, 40, 40))


class TestScenario(unittest.TestCase):
    def write(self, data):
        path = os.path.join(self.tmp, "s.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f)
        return path

    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def test_default_scenario_loads(self):
        data = scenario.load_scenario(str(TOOLS_DIR / "evaluator" / "scenarios" / "default.json"))
        self.assertEqual(data["name"], "default")
        shots = [s["shot"] for s in data["steps"] if "shot" in s]
        self.assertIn("title", shots)
        self.assertEqual(len(shots), len(set(shots)), "shot names must be unique")

    def test_rejects_step_without_action(self):
        path = self.write({"steps": [{"settle": 30}]})
        with self.assertRaises(ValueError):
            scenario.load_scenario(path)

    def test_rejects_missing_steps(self):
        path = self.write({"name": "x"})
        with self.assertRaises(ValueError):
            scenario.load_scenario(path)

    def test_name_defaults_to_filename(self):
        path = self.write({"steps": [{"shot": "a"}]})
        self.assertEqual(scenario.load_scenario(path)["name"], "s")


class FakeDriver:
    """Records the JS the runner sends and answers from a script."""
    name = "fake"

    def __init__(self, answers=None):
        self.calls = []
        self.shots = []
        self.answers = answers or {}

    def evaluate(self, js, timeout=60.0):
        self.calls.append(js)
        for key, value in self.answers.items():
            if key in js:
                if isinstance(value, Exception):
                    raise value
                return value
        return True

    def screenshot(self, path):
        self.shots.append(os.path.basename(path))
        with open(path, "wb") as f:
            f.write(b"png")


class TestRunner(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def run_steps(self, steps, driver=None, env=None):
        driver = driver or FakeDriver()
        runner = scenario.Runner(driver, self.tmp, log=lambda *a: None, env=env)
        return driver, runner.run(steps)

    def test_scene_step_pushes_waits_shoots_pops(self):
        driver, results = self.run_steps([{"scene": "Scene_Item", "shot": "item"}])
        self.assertEqual(results[0].status, "ok")
        self.assertEqual(driver.shots, ["item.png"])
        joined = "\n".join(driver.calls)
        self.assertIn('__ev.pushScene("Scene_Item", null)', joined)
        self.assertIn('__ev.waitScene("Scene_Item"', joined)
        self.assertIn("__ev.waitFrames(30)", joined)          # default settle
        self.assertIn("__ev.popScene()", joined)
        self.assertLess(joined.index("waitFrames"), joined.index("popScene"))

    def test_missing_scene_class_is_skipped(self):
        driver = FakeDriver({"pushScene": False})
        _, results = self.run_steps([{"scene": "Scene_Nope", "shot": "nope"}], driver)
        self.assertEqual(results[0].status, "skipped")
        self.assertEqual(driver.shots, [])
        self.assertNotIn("__ev.popScene()", driver.calls)

    def test_js_error_is_recorded_and_run_continues(self):
        driver = FakeDriver({"waitScene": JsError("Error: timed out\nat x")})
        _, results = self.run_steps([{"wait_scene": "Scene_Title"}, {"shot": "after"}], driver)
        self.assertEqual(results[0].status, "error")
        self.assertEqual(results[0].detail, "Error: timed out | at x")
        self.assertEqual(results[1].status, "ok")

    def test_driver_error_aborts_remaining_steps(self):
        driver = FakeDriver({"waitFrames": DriverError("gone")})
        _, results = self.run_steps([{"wait": 10}, {"shot": "x"}, {"shot": "y"}], driver)
        self.assertEqual([r.status for r in results], ["error", "error", "error"])
        self.assertTrue(results[2].detail.startswith("not run"))

    def test_keys_and_condition(self):
        driver = FakeDriver({"!!(": False})
        _, results = self.run_steps([
            {"keys": ["ok", "down"], "hold": 3, "after": 4},
            {"key": "ok", "if": "false", "shot": "skipped"},
        ], driver)
        self.assertIn('__ev.pressKey("ok", 3, 4)', driver.calls)
        self.assertIn('__ev.pressKey("down", 3, 4)', driver.calls)
        self.assertEqual(results[1].status, "skipped")
        self.assertEqual(driver.shots, [])

    def test_battle_auto_uses_env(self):
        driver, results = self.run_steps([{"battle": "auto"}], env={"auto_troop": 7})
        self.assertIn("__ev.startBattle(7)", driver.calls)
        driver, results = self.run_steps([{"battle": "auto"}], env={"auto_troop": None})
        self.assertEqual(results[0].status, "skipped")

    def test_settle_zero_freezes_without_waiting(self):
        driver, _ = self.run_steps([{"shot": "now", "settle": 0}])
        self.assertIn("__ev.freeze()", driver.calls)
        self.assertFalse(any("waitFrames" in c for c in driver.calls))


class TestGameData(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        os.makedirs(os.path.join(self.tmp, "data"))
        os.makedirs(os.path.join(self.tmp, "img", "enemies"))

    def write_json(self, name, data):
        with open(os.path.join(self.tmp, "data", name), "w", encoding="utf-8") as f:
            json.dump(data, f)

    def test_pick_troop_skips_missing_battlers(self):
        self.write_json("System.json", {"optSideView": False,
                                        "advanced": {"screenWidth": 1280, "screenHeight": 720}})
        self.write_json("Enemies.json", [None, {"id": 1, "battlerName": "Goblin"},
                                         {"id": 2, "battlerName": "Ghost"}])
        self.write_json("Troops.json", [None, {"id": 1, "members": [{"enemyId": 1}]},
                                        {"id": 2, "members": [{"enemyId": 2}, {"enemyId": 1}]},
                                        {"id": 3, "members": [{"enemyId": 2}]}])
        self.assertIsNone(pick_troop(self.tmp))
        open(os.path.join(self.tmp, "img", "enemies", "Ghost.png"), "wb").close()
        self.assertEqual(pick_troop(self.tmp), 3)
        self.assertEqual(game_resolution(self.tmp), (1280, 720))

    def test_resolution_defaults(self):
        self.assertEqual(game_resolution(os.path.join(self.tmp, "nope")), (816, 624))


class TestControlProtocol(unittest.TestCase):
    """OutsiderDriver's line protocol, exercised with a Python stand-in for
    outsider.exe that echoes eval sources back through the same format."""

    FAKE_RUNTIME = r'''
import json, sys
for line in sys.stdin:
    parts = line.rstrip("\n").split(" ", 2) + [""]
    msg_id, cmd, payload = parts[0], parts[1], parts[2]
    if cmd == "eval":
        src = json.loads(payload)
        if "throw" in src:
            print("@@ctl %s err %s" % (msg_id, json.dumps("boom: " + src)))
        else:
            print("noise line that is not a reply")
            print("@@ctl %s ok %s" % (msg_id, json.dumps({"src": src})))
    elif cmd == "shot":
        with open(payload, "wb") as f:
            f.write(b"P6\n2 2\n255\n" + bytes([255, 0, 0] * 4))
        print("@@ctl %s ok null" % msg_id)
    elif cmd == "quit":
        print("@@ctl %s ok null" % msg_id)
        break
    sys.stdout.flush()
'''

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.fake = os.path.join(self.tmp, "fake_runtime.py")
        with open(self.fake, "w", encoding="utf-8") as f:
            f.write(self.FAKE_RUNTIME)

    def make_driver(self):
        # exe = python, game_dir = the fake script (becomes argv[1]); the
        # remaining flags are ignored by the stand-in.
        return OutsiderDriver(sys.executable, self.fake, "var prelude = 1;", 816, 624,
                              log_path=os.path.join(self.tmp, "run.log"))

    def test_eval_shot_quit(self):
        driver = self.make_driver()
        try:
            self.assertEqual(driver.evaluate("1 + 1"), {"src": "1 + 1"})
            self.assertEqual(driver.evaluate("line1\nline2"), {"src": "line1\nline2"})
            with self.assertRaises(JsError) as ctx:
                driver.evaluate("throw new Error()")
            self.assertIn("boom", str(ctx.exception))
            png = os.path.join(self.tmp, "shot.png")
            driver.screenshot(png)
            if Image is not None:
                self.assertEqual(Image.open(png).size, (2, 2))
            self.assertFalse(os.path.exists(os.path.join(self.tmp, "shot.ppm")))
        finally:
            driver.close()
        self.assertEqual(driver.proc.poll(), 0)
        with open(os.path.join(self.tmp, "run.log"), "rb") as f:
            self.assertIn(b"noise line", f.read())

    def test_process_exit_is_reported(self):
        driver = self.make_driver()
        driver.proc.stdin.close()          # stand-in sees EOF and exits
        driver.proc.wait(timeout=10)
        with self.assertRaises(DriverError):
            driver.evaluate("1")
        driver.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
