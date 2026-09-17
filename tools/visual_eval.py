#!/usr/bin/env python3
# Copyright (c) 2026 General Arcade (Pte. Ltd.)
# SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
"""Compare how a game looks in its original NW.js player and in Outsider.

    python tools/visual_eval.py "C:/Program Files (x86)/Steam/steamapps/common/Ann/Ann"

Launches both players, plays tools/evaluator/scenarios/default.json (title,
options, map, menus, message window, shop, battle ...) in each, screenshots
every scene at the same game frame, and writes build/eval/<game>/report.html
with side-by-side images and the differing regions. Needs Pillow.
Use --scenario for a game-specific step list; see tools/evaluator/scenario.py
for the step format.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from evaluator.cli import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
