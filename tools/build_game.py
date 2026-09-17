#!/usr/bin/env python3
# Copyright (c) 2026 General Arcade (Pte. Ltd.)
# SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
"""Build pipeline for Outsider, the RPG Maker MZ native runtime.

Orchestrates the full build process:
1. Copies and converts game assets (decryption, M4A->OGG)
2. Bundles JS shim files alongside game JS
3. Compiles the native runtime via CMake
4. Packages everything into a distributable folder

Usage:
    python3 build_game.py <game_dir> [--output-dir <dir>] [--build-type Release] [--skip-compile] [--skip-assets]
"""

import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

# This script lives in tools/, project root is one level up
SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
SHIMS_DIR = PROJECT_ROOT / "src" / "shims"


def run_cmd(cmd: list[str], cwd: Path | None = None, env: dict | None = None) -> bool:
    """Run a command and return True on success."""
    print(f"  $ {' '.join(str(c) for c in cmd)}")
    merged_env = dict(os.environ)
    if env:
        merged_env.update(env)
    result = subprocess.run(cmd, cwd=cwd, env=merged_env)
    return result.returncode == 0


def step_convert_assets(game_dir: Path, dist_game_dir: Path, verbose: bool) -> bool:
    """Step 1: Convert and copy game assets."""
    print("\n=== Step 1: Converting game assets ===")

    converter = SCRIPT_DIR / "resource_converter.py"
    cmd = [sys.executable, str(converter), str(game_dir), str(dist_game_dir)]
    if verbose:
        cmd.append("--verbose")

    return run_cmd(cmd)


def step_bundle_shims(dist_game_dir: Path) -> bool:
    """Step 2: Copy JS shim files into the distribution."""
    print("\n=== Step 2: Bundling JS shim files ===")

    shims_dst = dist_game_dir / "shims"
    shims_dst.mkdir(parents=True, exist_ok=True)

    shim_files = sorted(SHIMS_DIR.glob("*.js"))
    if not shim_files:
        print(f"  Warning: no shim files found in {SHIMS_DIR}")
        return False

    count = 0
    for shim in shim_files:
        shutil.copy2(shim, shims_dst / shim.name)
        count += 1
        print(f"  shim: {shim.name}")

    print(f"  Bundled {count} shim files")
    return True


def step_compile(build_dir: Path, build_type: str, jobs: int) -> bool:
    """Step 3: Compile the native runtime."""
    print("\n=== Step 3: Compiling native runtime ===")

    build_dir.mkdir(parents=True, exist_ok=True)

    cmake_cmd = [
        "cmake",
        str(PROJECT_ROOT),
        f"-DCMAKE_BUILD_TYPE={build_type}",
        "-DRMMZ_BUILD_TESTS=OFF",
    ]
    if not run_cmd(cmake_cmd, cwd=build_dir):
        print("  CMake configure failed")
        return False

    build_cmd = ["cmake", "--build", str(build_dir), "--config", build_type]
    if jobs > 0:
        build_cmd += ["--parallel", str(jobs)]
    else:
        build_cmd += ["--parallel"]

    if not run_cmd(build_cmd, cwd=build_dir):
        print("  Build failed")
        return False

    print("  Build succeeded")
    return True


def step_package(build_dir: Path, dist_dir: Path, build_type: str) -> bool:
    """Step 4: Package runtime + assets into distribution folder."""
    print("\n=== Step 4: Packaging distribution ===")

    # Find the compiled executable
    exe_name = "outsider.exe" if platform.system() == "Windows" else "outsider"
    exe_path = build_dir / exe_name
    if not exe_path.exists():
        # Try in build type subdirectory (MSVC)
        exe_path = build_dir / build_type / exe_name
    if not exe_path.exists():
        print(f"  Error: executable not found at {exe_path}")
        return False

    # Ensure dist directory exists (may not if --skip-assets was used).
    dist_dir.mkdir(parents=True, exist_ok=True)

    # Copy executable to dist root
    dst_exe = dist_dir / exe_name
    shutil.copy2(exe_path, dst_exe)
    if platform.system() != "Windows":
        os.chmod(dst_exe, 0o755)
    print(f"  Copied executable: {exe_name}")

    # Copy SDL2 shared library if present
    for lib_pattern in ["libSDL2*", "SDL2.dll"]:
        for lib_file in build_dir.rglob(lib_pattern):
            if lib_file.is_file() and lib_file.suffix in (".dll", ".so", ".dylib"):
                shutil.copy2(lib_file, dist_dir / lib_file.name)
                print(f"  Copied library: {lib_file.name}")

    # Verify game assets are in place
    game_dir = dist_dir / "game"
    if not game_dir.exists():
        print("  Warning: game assets directory not found at dist/game/")

    return True


def compute_dist_size(dist_dir: Path) -> str:
    """Compute total size of distribution directory."""
    total = 0
    for f in dist_dir.rglob("*"):
        if f.is_file():
            total += f.stat().st_size
    if total > 1024 * 1024 * 1024:
        return f"{total / (1024 * 1024 * 1024):.1f} GB"
    elif total > 1024 * 1024:
        return f"{total / (1024 * 1024):.1f} MB"
    elif total > 1024:
        return f"{total / 1024:.1f} KB"
    return f"{total} bytes"


def main():
    parser = argparse.ArgumentParser(
        description="Build RPG Maker MZ native runtime distribution"
    )
    parser.add_argument("game_dir", help="Path to RPG Maker MZ game directory")
    parser.add_argument("--output-dir", "-o", default=None,
                        help="Output directory (default: <project>/dist)")
    parser.add_argument("--build-type", default="Release",
                        choices=["Debug", "Release", "RelWithDebInfo", "MinSizeRel"],
                        help="CMake build type (default: Release)")
    parser.add_argument("--jobs", "-j", type=int, default=0,
                        help="Parallel build jobs (0 = auto)")
    parser.add_argument("--skip-compile", action="store_true",
                        help="Skip compilation step")
    parser.add_argument("--skip-assets", action="store_true",
                        help="Skip asset conversion step")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")
    args = parser.parse_args()

    game_dir = Path(args.game_dir).resolve()
    if args.output_dir:
        dist_dir = Path(args.output_dir).resolve()
    else:
        dist_dir = PROJECT_ROOT / "dist"

    build_dir = PROJECT_ROOT / "build-release"
    dist_game_dir = dist_dir / "game"

    if not game_dir.is_dir():
        print(f"Error: game directory not found: {game_dir}", file=sys.stderr)
        sys.exit(1)

    print(f"Project root:  {PROJECT_ROOT}")
    print(f"Game directory: {game_dir}")
    print(f"Distribution:   {dist_dir}")
    print(f"Build type:     {args.build_type}")

    success = True

    # Step 1: Convert and copy game assets
    if not args.skip_assets:
        if not step_convert_assets(game_dir, dist_game_dir, args.verbose):
            print("\nAsset conversion failed!")
            success = False
    else:
        print("\n=== Step 1: Skipped (--skip-assets) ===")

    # Step 2: Bundle shim files
    if not args.skip_assets:
        if not step_bundle_shims(dist_game_dir):
            print("\nShim bundling failed!")
            success = False
    else:
        print("\n=== Step 2: Skipped (--skip-assets) ===")

    # Step 3: Compile native runtime
    if not args.skip_compile:
        if not step_compile(build_dir, args.build_type, args.jobs):
            print("\nCompilation failed!")
            success = False
    else:
        print("\n=== Step 3: Skipped (--skip-compile) ===")

    # Step 4: Package distribution
    if not args.skip_compile:
        if not step_package(build_dir, dist_dir, args.build_type):
            print("\nPackaging failed!")
            success = False
    else:
        print("\n=== Step 4: Skipped (--skip-compile) ===")

    # Summary
    print("\n=== Build Summary ===")
    if dist_dir.exists():
        print(f"  Distribution size: {compute_dist_size(dist_dir)}")
        file_count = sum(1 for _ in dist_dir.rglob("*") if _.is_file())
        print(f"  Total files: {file_count}")
    print(f"  Result: {'SUCCESS' if success else 'FAILED'}")

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
