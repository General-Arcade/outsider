# Copyright (c) 2026 General Arcade (Pte. Ltd.)
# SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
"""Command line for the visual evaluator (see tools/visual_eval.py)."""

import argparse
import json
import os
import subprocess
import sys
import time

from . import compare, report as report_mod
from .drivers import NwjsDriver, OutsiderDriver
from .scenario import Runner, load_scenario

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def game_resolution(game_dir):
    """Game resolution from data/System.json (advanced.screenWidth/Height)."""
    path = os.path.join(game_dir, "data", "System.json")
    try:
        with open(path, "r", encoding="utf-8") as f:
            system = json.load(f)
        adv = system.get("advanced", {})
        return int(adv.get("screenWidth", 816)), int(adv.get("screenHeight", 624))
    except (OSError, ValueError):
        return 816, 624


def pick_troop(game_dir):
    """First troop whose enemies all have an existing battler image, so the
    battle scene does not stop on a load error (default-database troops
    often reference images the game never shipped)."""
    try:
        with open(os.path.join(game_dir, "data", "Troops.json"), "r", encoding="utf-8") as f:
            troops = json.load(f)
        with open(os.path.join(game_dir, "data", "Enemies.json"), "r", encoding="utf-8") as f:
            enemies = json.load(f)
        with open(os.path.join(game_dir, "data", "System.json"), "r", encoding="utf-8") as f:
            side_view = bool(json.load(f).get("optSideView"))
    except (OSError, ValueError):
        return None
    folder = "sv_enemies" if side_view else "enemies"
    for troop in troops:
        if not troop or not troop.get("members"):
            continue
        ok = True
        for member in troop["members"]:
            enemy = enemies[member["enemyId"]] if member["enemyId"] < len(enemies) else None
            name = enemy and enemy.get("battlerName")
            if not name or not os.path.isfile(os.path.join(game_dir, "img", folder, name + ".png")):
                ok = False
                break
        if ok:
            return troop["id"]
    return None


def find_outsider():
    for candidate in ("build-release/outsider.exe", "build/outsider.exe",
                      "build-release/outsider", "build/outsider"):
        path = os.path.join(ROOT, candidate)
        if os.path.isfile(path):
            return path
    return None


def ensure_converted(original, game_dir, log):
    if os.path.isfile(os.path.join(game_dir, "data", "System.json")):
        return
    log("Converting %s -> %s" % (original, game_dir))
    converter = os.path.join(ROOT, "tools", "resource_converter.py")
    subprocess.run([sys.executable, converter, original, game_dir], check=True)


def run_side(make_driver, side, steps, out_dir, log, env):
    side_dir = os.path.join(out_dir, side)
    os.makedirs(side_dir, exist_ok=True)
    for old in os.listdir(side_dir):          # no stale shots from earlier runs
        if old.endswith(".png") or old.endswith(".ppm"):
            os.remove(os.path.join(side_dir, old))
    log("Launching %s ..." % side)
    started = time.time()
    driver = make_driver(os.path.join(out_dir, side + ".log"))
    try:
        results = Runner(driver, side_dir, log, env).run(steps)
    finally:
        driver.close()
    log("%s done in %.0fs" % (side, time.time() - started))
    return [r.as_dict() for r in results]


def build_report(args, scenario, steps_by_side, width, height, out_dir):
    shots = []
    counts = {"match": 0, "minor": 0, "differ": 0, "missing": 0, "skipped": 0}
    diff_dir = os.path.join(out_dir, "diff")
    os.makedirs(diff_dir, exist_ok=True)
    for index, step in enumerate(scenario["steps"]):
        name = step.get("shot")
        if not name:
            continue
        entry = {"name": name, "step": step}
        a_path = os.path.join(out_dir, "original", name + ".png")
        b_path = os.path.join(out_dir, "outsider", name + ".png")
        have_a, have_b = os.path.isfile(a_path), os.path.isfile(b_path)
        if have_a:
            entry["original"] = "original/%s.png" % name
        if have_b:
            entry["outsider"] = "outsider/%s.png" % name
        skipped = [side for side, results in steps_by_side.items()
                   if index < len(results) and results[index]["status"] == "skipped"]
        if skipped and not (have_a and have_b):
            entry["verdict"] = "skipped"
            entry["note"] = "; ".join("%s: %s" % (side, steps_by_side[side][index]["detail"])
                                      for side in skipped)
            counts["skipped"] += 1
            shots.append(entry)
            continue
        if not (have_a and have_b):
            entry["verdict"] = "missing"
            entry["note"] = "no screenshot from " + ", ".join(
                s for s, ok in (("original", have_a), ("outsider", have_b)) if not ok)
            counts["missing"] += 1
            shots.append(entry)
            continue
        a = compare.load_rgb(a_path)
        b = compare.load_rgb(b_path)
        metrics, mask = compare.compare_images(a, b, args.tolerance)
        entry["metrics"] = metrics
        entry["verdict"] = compare.verdict(metrics, args.minor_pct, args.major_pct)
        counts[entry["verdict"]] += 1
        diff_path = os.path.join(diff_dir, name + ".png")
        compare.diff_visual(a, b, mask).save(diff_path)
        entry["diff"] = "diff/%s.png" % name
        shots.append(entry)
    return {
        "game": args.name, "scenario": scenario["name"], "width": width, "height": height,
        "tolerance": args.tolerance, "original_dir": args.original, "game_dir": args.game,
        "generated": time.strftime("%Y-%m-%d %H:%M:%S"),
        "shots": shots, "summary": counts, "steps": steps_by_side,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="visual_eval.py",
        description="Run the same scenes in the original NW.js player and in Outsider, "
                    "screenshot both, and report the visual differences.")
    parser.add_argument("original", help="game folder with Game.exe and package.json (e.g. the Steam install)")
    parser.add_argument("--game", help="converted (decrypted) copy for Outsider; default build/games/<name>, "
                                       "created with tools/resource_converter.py when missing")
    parser.add_argument("--name", help="game name for the report and default folders")
    parser.add_argument("--outsider", help="outsider executable (default: build-release or build)")
    parser.add_argument("--scenario", default=os.path.join(HERE, "scenarios", "default.json"),
                        help="scenario JSON (default: scenarios/default.json)")
    parser.add_argument("--out", help="output folder (default build/eval/<name>)")
    parser.add_argument("--skip-original", action="store_true",
                        help="reuse the original's screenshots from a previous run")
    parser.add_argument("--skip-outsider", action="store_true",
                        help="reuse Outsider's screenshots from a previous run")
    parser.add_argument("--tolerance", type=int, default=24,
                        help="per-channel difference a pixel may have and still count as equal (0-255)")
    parser.add_argument("--minor-pct", type=float, default=0.5, help="changed-pixel %% up to which a shot matches")
    parser.add_argument("--major-pct", type=float, default=5.0, help="changed-pixel %% above which a shot differs")
    parser.add_argument("--outsider-arg", action="append", default=[],
                        help="extra argument for outsider.exe (repeatable)")
    args = parser.parse_args(argv)

    log = print
    args.original = os.path.abspath(args.original)
    if not os.path.isfile(os.path.join(args.original, "package.json")):
        parser.error("%s has no package.json; point at the folder holding Game.exe" % args.original)
    args.name = args.name or os.path.basename(args.original.rstrip("\\/"))
    args.game = os.path.abspath(args.game or os.path.join(ROOT, "build", "games", args.name))
    out_dir = os.path.abspath(args.out or os.path.join(ROOT, "build", "eval", args.name))
    os.makedirs(out_dir, exist_ok=True)

    if not args.skip_outsider:
        ensure_converted(args.original, args.game, log)
        args.outsider = args.outsider or find_outsider()
        if not args.outsider or not os.path.isfile(args.outsider):
            parser.error("outsider executable not found; build it or pass --outsider")

    width, height = game_resolution(args.game if os.path.isdir(args.game) else args.original)
    scenario = load_scenario(args.scenario)
    with open(os.path.join(HERE, "prelude.js"), "r", encoding="utf-8") as f:
        prelude = f.read()
    env = {"auto_troop": pick_troop(args.game)}
    log("Game %s at %dx%d, scenario %s (%d steps), output %s" % (
        args.name, width, height, scenario["name"], len(scenario["steps"]), out_dir))
    log("Battle troop: %s" % (env["auto_troop"] if env["auto_troop"] else "none with shipped battler images"))

    steps_by_side = {}
    if not args.skip_original:
        steps_by_side["original"] = run_side(
            lambda log_path: NwjsDriver(args.original, prelude, log_path=log_path,
                                        profile_dir=os.path.join(out_dir, "nw-profile")),
            "original", scenario["steps"], out_dir, log, env)
    if not args.skip_outsider:
        steps_by_side["outsider"] = run_side(
            lambda log_path: OutsiderDriver(args.outsider, args.game, prelude, width, height,
                                            shims_dir=os.path.join(ROOT, "src", "shims"),
                                            log_path=log_path, cwd=ROOT, extra_args=args.outsider_arg),
            "outsider", scenario["steps"], out_dir, log, env)
    for side, results in steps_by_side.items():
        with open(os.path.join(out_dir, "steps_%s.json" % side), "w", encoding="utf-8") as f:
            json.dump(results, f, indent=2)
    for side in ("original", "outsider"):
        if side not in steps_by_side:
            path = os.path.join(out_dir, "steps_%s.json" % side)
            if os.path.isfile(path):
                with open(path, "r", encoding="utf-8") as f:
                    steps_by_side[side] = json.load(f)

    report = build_report(args, scenario, steps_by_side, width, height, out_dir)
    report_mod.write_json(os.path.join(out_dir, "report.json"), report)
    report_mod.write_html(os.path.join(out_dir, "report.html"), report)
    print()
    print(report_mod.console_summary(report))
    print()
    print("Report: %s" % os.path.join(out_dir, "report.html"))

    failed = report["summary"]["missing"] > 0 or any(
        r["status"] == "error" for results in steps_by_side.values() for r in results)
    return 1 if failed else 0
