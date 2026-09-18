#!/usr/bin/env python
"""Run one scenario through two Outsider builds and diff the screenshots.

The visual evaluator (tools/visual_eval.py) compares Outsider against the
game's original NW.js player. This runs the same machinery with Outsider on
both sides, which is how the SDL3 GPU backend is checked against the OpenGL
one: same engine, same scenario, same frames, so any difference is the
renderer.

Usage:
  python tools/compare_backends.py <game-name> \
      --a build/outsider.exe --b build-gpu/outsider.exe
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from evaluator import compare, report as report_mod          # noqa: E402
from evaluator.cli import build_report, game_resolution, pick_troop, run_side  # noqa: E402
from evaluator.drivers import OutsiderDriver                 # noqa: E402
from evaluator.scenario import load_scenario                 # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


class Args:
    """Stands in for the argparse namespace build_report expects."""
    pass


def main(argv=None):
    p = argparse.ArgumentParser()
    p.add_argument("name", help="game folder name under build/games")
    p.add_argument("--a", default=os.path.join(ROOT, "build", "outsider.exe"),
                   help="baseline build (shown as 'original' in the report)")
    p.add_argument("--b", default=os.path.join(ROOT, "build-gpu", "outsider.exe"),
                   help="build under test (shown as 'outsider')")
    p.add_argument("--game", help="converted game directory (default build/games/<name>)")
    p.add_argument("--scenario",
                   default=os.path.join(HERE, "evaluator", "scenarios", "default.json"))
    p.add_argument("--out", help="output folder (default build/backend-eval/<name>)")
    p.add_argument("--tolerance", type=int, default=24)
    p.add_argument("--minor-pct", type=float, default=0.5)
    p.add_argument("--major-pct", type=float, default=5.0)
    p.add_argument("--skip-a", action="store_true", help="reuse the baseline's shots")
    p.add_argument("--skip-b", action="store_true", help="reuse the test build's shots")
    opts = p.parse_args(argv)

    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="replace")
    log = print

    game = os.path.abspath(opts.game or os.path.join(ROOT, "build", "games", opts.name))
    out_dir = os.path.abspath(opts.out or os.path.join(ROOT, "build", "backend-eval", opts.name))
    os.makedirs(out_dir, exist_ok=True)

    width, height = game_resolution(game)
    scenario = load_scenario(opts.scenario)
    with open(os.path.join(HERE, "evaluator", "prelude.js"), "r", encoding="utf-8") as f:
        prelude = f.read()
    env = {"auto_troop": pick_troop(game)}

    log("Game %s at %dx%d, scenario %s (%d steps)" %
        (opts.name, width, height, scenario["name"], len(scenario["steps"])))
    log("  A (baseline): %s" % opts.a)
    log("  B (test):     %s" % opts.b)

    def driver_for(exe):
        return lambda log_path: OutsiderDriver(
            exe, game, prelude, width, height,
            shims_dir=os.path.join(ROOT, "src", "shims"),
            log_path=log_path, cwd=ROOT)

    steps_by_side = {}
    if not opts.skip_a:
        steps_by_side["original"] = run_side(driver_for(opts.a), "original",
                                             scenario["steps"], out_dir, log, env)
    if not opts.skip_b:
        steps_by_side["outsider"] = run_side(driver_for(opts.b), "outsider",
                                             scenario["steps"], out_dir, log, env)
    for side, results in steps_by_side.items():
        with open(os.path.join(out_dir, "steps_%s.json" % side), "w", encoding="utf-8") as f:
            json.dump(results, f, indent=2)
    for side in ("original", "outsider"):
        if side not in steps_by_side:
            path = os.path.join(out_dir, "steps_%s.json" % side)
            if os.path.isfile(path):
                with open(path, "r", encoding="utf-8") as f:
                    steps_by_side[side] = json.load(f)

    args = Args()
    args.name = opts.name
    args.tolerance = opts.tolerance
    args.minor_pct = opts.minor_pct
    args.major_pct = opts.major_pct
    args.original = opts.a
    args.game = game

    report = build_report(args, scenario, steps_by_side, width, height, out_dir)
    report_mod.write_json(os.path.join(out_dir, "report.json"), report)
    report_mod.write_html(os.path.join(out_dir, "report.html"), report)
    print()
    print(report_mod.console_summary(report))
    print("Report: %s" % os.path.join(out_dir, "report.html"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
