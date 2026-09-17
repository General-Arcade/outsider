#!/usr/bin/env python3
# Copyright (c) 2026 General Arcade (Pte. Ltd.)
# SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
"""RPG Maker MV/MZ asset decrypter and resource converter.

Decrypts .png_ and .ogg_ files encrypted by RPG Maker MV/MZ's built-in
encryption, and converts M4A files to OGG if needed.

Usage:
    python3 resource_converter.py <game_dir> <output_dir> [--dry-run] [--verbose]
"""

import argparse
import hashlib
import json
import os
import shutil
import struct
import sys
from pathlib import Path


RPGMV_HEADER_SIZE = 16
RPGMV_MAGIC = b"RPGMV"
ENCRYPTED_EXTENSIONS = {".png_": ".png", ".ogg_": ".ogg"}


def parse_encryption_key(hex_key: str) -> bytes:
    """Convert a hex string encryption key to bytes."""
    if len(hex_key) != 32:
        raise ValueError(f"Encryption key must be 32 hex chars, got {len(hex_key)}")
    return bytes.fromhex(hex_key)


# Arthran's "Art_Decrypterator3000" plugin variant: a 32-byte header, and the
# key is the MD5 of the stock key's hex string followed by the same bytes in
# reverse, XORed over the first 32 body bytes.
ART_MAGIC = b"ART\x00ENCRYPTER100FREE\x00VERSION\x00\x00\x00\x00"


def art_encrypter_key(hex_key: str) -> bytes:
    digest = hashlib.md5(hex_key.encode("ascii")).hexdigest()
    pairs = [digest[i:i + 2] for i in range(0, len(digest), 2)]
    pairs += list(reversed(pairs))
    return bytes(int(p, 16) for p in pairs)


def decrypt_file(src: Path, dst: Path, key_bytes: bytes, art_key: bytes = None) -> bool:
    """Decrypt a single RPG Maker encrypted file.

    Stock format: 16-byte RPGMV header + 16 XOR'd bytes + rest of file
    unchanged. Files with the ART header use art_encrypter_key() instead.
    """
    data = src.read_bytes()
    if len(data) < RPGMV_HEADER_SIZE + 16:
        return False

    if data.startswith(ART_MAGIC):
        if not art_key or len(data) < len(ART_MAGIC):
            return False
        block = bytearray(data[len(ART_MAGIC):len(ART_MAGIC) + len(art_key)])
        for i in range(len(block)):
            block[i] ^= art_key[i]
        dst.parent.mkdir(parents=True, exist_ok=True)
        with open(dst, "wb") as f:
            f.write(bytes(block))
            f.write(data[len(ART_MAGIC) + len(block):])
        return True

    header = data[:RPGMV_HEADER_SIZE]
    if not header.startswith(RPGMV_MAGIC):
        return False

    encrypted_block = bytearray(data[RPGMV_HEADER_SIZE:RPGMV_HEADER_SIZE + 16])
    for i in range(16):
        encrypted_block[i] ^= key_bytes[i]

    dst.parent.mkdir(parents=True, exist_ok=True)
    with open(dst, "wb") as f:
        f.write(bytes(encrypted_block))
        f.write(data[RPGMV_HEADER_SIZE + 16:])

    return True


def find_encrypted_files(game_dir: Path) -> list[tuple[Path, str]]:
    """Find all encrypted asset files and their target extensions."""
    results = []
    for ext, target_ext in ENCRYPTED_EXTENSIONS.items():
        for f in game_dir.rglob(f"*{ext}"):
            results.append((f, target_ext))
    return results


def ffmpeg_command() -> str:
    """ffmpeg executable: RMMZ_FFMPEG overrides the one on PATH."""
    return os.environ.get("RMMZ_FFMPEG") or "ffmpeg"


def find_m4a_files(game_dir: Path) -> list[Path]:
    """Find M4A files that need conversion to OGG."""
    return list(game_dir.rglob("*.m4a"))


def convert_m4a_to_ogg(src: Path, dst: Path) -> bool:
    """Convert M4A to OGG using ffmpeg."""
    import subprocess
    dst.parent.mkdir(parents=True, exist_ok=True)
    try:
        result = subprocess.run(
            [ffmpeg_command(), "-y", "-i", str(src), "-c:a", "libvorbis", "-q:a", "6", str(dst)],
            capture_output=True, timeout=60
        )
        return result.returncode == 0
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return False


MOVIE_EXTENSIONS = (".webm", ".mp4")


def find_movie_files(game_dir: Path) -> list[Path]:
    """Movies to transcode for the runtime's MPEG-1 player, one per title.

    RPG Maker MZ ships each movie as both .webm and .mp4; the .webm is used
    when both exist (it is usually the smaller encode)."""
    movies_dir = game_dir / "movies"
    if not movies_dir.is_dir():
        return []
    by_stem: dict[str, Path] = {}
    for path in sorted(movies_dir.iterdir()):
        if path.is_file() and path.suffix.lower() in MOVIE_EXTENSIONS:
            current = by_stem.get(path.stem)
            if current is None or path.suffix.lower() == ".webm":
                by_stem[path.stem] = path
    return [by_stem[k] for k in sorted(by_stem)]


def convert_movie_to_mpeg(src: Path, dst: Path) -> bool:
    """Transcode a movie to MPEG-1 video + MP2 audio in an MPEG-PS container.

    That is what the runtime's single-header decoder plays. MPEG-1 only
    allows a fixed set of frame rates and even dimensions, so the source is
    retried at 30 fps when its own rate is rejected."""
    import subprocess
    dst.parent.mkdir(parents=True, exist_ok=True)
    base = [ffmpeg_command(), "-y", "-i", str(src),
            "-vf", "scale=trunc(iw/2)*2:trunc(ih/2)*2",
            "-c:v", "mpeg1video", "-q:v", "4", "-bf", "0",
            "-c:a", "mp2", "-b:a", "192k", "-ar", "44100", "-ac", "2",
            "-f", "mpeg"]
    for extra in ([], ["-r", "30"]):
        try:
            result = subprocess.run(base + extra + [str(dst)], capture_output=True, timeout=1800)
        except (FileNotFoundError, subprocess.TimeoutExpired):
            return False
        if result.returncode == 0 and dst.exists() and dst.stat().st_size > 0:
            return True
    return False


def copy_unencrypted_assets(game_dir: Path, output_dir: Path, verbose: bool = False) -> int:
    """Copy all non-encrypted game assets to the output directory."""
    count = 0
    skip_extensions = set(ENCRYPTED_EXTENSIONS.keys()) | {".m4a"}
    skip_dirs = {"node_modules", ".git"}
    skip_files = {
        "Game.exe", "nw.dll", "nw_100_percent.pak", "nw_200_percent.pak",
        "nw_elf", "d3dcompiler_47.dll", "libEGL.dll", "libGLESv2.dll",
        "ffmpeg.dll", "node.dll", "icudtl.dat", "notification_helper.exe",
        "chromedriver.exe", "credits.html", "index.html", "package.json",
    }
    skip_dirs_full = {"locales", "lib", "icon", "css"}
    # JS libs we replace with native implementations
    skip_js_libs = {"pixi.js", "effekseer.min.js", "effekseer.wasm", "vorbisdecoder.js"}

    for src_path in game_dir.rglob("*"):
        if not src_path.is_file():
            continue

        rel = src_path.relative_to(game_dir)
        parts = rel.parts

        # Skip NW.js and platform-specific files
        if parts[0] in skip_dirs_full:
            continue
        if any(d in skip_dirs for d in parts):
            continue
        if src_path.name in skip_files:
            continue

        # Skip encrypted files (handled separately)
        if src_path.suffix in skip_extensions:
            continue

        # Skip JS libs we replace with native
        if len(parts) >= 3 and parts[0] == "js" and parts[1] == "libs":
            if src_path.name in skip_js_libs:
                continue

        dst_path = output_dir / rel
        dst_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src_path, dst_path)
        count += 1
        if verbose:
            print(f"  copy: {rel}")

    return count


def run_conversion(game_dir: Path, output_dir: Path, dry_run: bool = False,
                   verbose: bool = False) -> dict:
    """Run the full resource conversion pipeline."""
    stats = {"decrypted": 0, "m4a_converted": 0, "movies_converted": 0, "copied": 0, "errors": 0}

    # Load encryption key from System.json
    system_json = game_dir / "data" / "System.json"
    if not system_json.exists():
        print(f"Error: {system_json} not found", file=sys.stderr)
        return stats

    # utf-8-sig: some games save System.json with a byte order mark.
    with open(system_json, encoding="utf-8-sig") as f:
        system_data = json.load(f)

    has_encrypted_images = system_data.get("hasEncryptedImages", False)
    has_encrypted_audio = system_data.get("hasEncryptedAudio", False)
    encryption_key = system_data.get("encryptionKey", "")

    key_bytes = None
    art_key = None
    if (has_encrypted_images or has_encrypted_audio) and encryption_key:
        key_bytes = parse_encryption_key(encryption_key)
        art_key = art_encrypter_key(encryption_key)
        print(f"Encryption key found: {encryption_key[:8]}...")

    # Step 1: Decrypt encrypted assets
    encrypted_files = find_encrypted_files(game_dir)
    print(f"Found {len(encrypted_files)} encrypted files")

    for src_path, target_ext in encrypted_files:
        rel = src_path.relative_to(game_dir)
        # Change extension: file.png_ -> file.png
        dst_rel = rel.with_suffix(target_ext)
        dst_path = output_dir / dst_rel

        if dry_run:
            print(f"  [dry-run] decrypt: {rel} -> {dst_rel}")
            stats["decrypted"] += 1
            continue

        if key_bytes and decrypt_file(src_path, dst_path, key_bytes, art_key):
            stats["decrypted"] += 1
            if verbose:
                print(f"  decrypt: {rel} -> {dst_rel}")
        else:
            stats["errors"] += 1
            print(f"  ERROR: failed to decrypt: {rel}", file=sys.stderr)

    # Step 2: Convert M4A to OGG (if any exist)
    m4a_files = find_m4a_files(game_dir)
    if m4a_files:
        print(f"Found {len(m4a_files)} M4A files to convert")
        for src_path in m4a_files:
            rel = src_path.relative_to(game_dir)
            dst_rel = rel.with_suffix(".ogg")
            dst_path = output_dir / dst_rel

            if dry_run:
                print(f"  [dry-run] convert: {rel} -> {dst_rel}")
                stats["m4a_converted"] += 1
                continue

            if convert_m4a_to_ogg(src_path, dst_path):
                stats["m4a_converted"] += 1
                if verbose:
                    print(f"  convert: {rel} -> {dst_rel}")
            else:
                stats["errors"] += 1
                print(f"  ERROR: failed to convert M4A: {rel}", file=sys.stderr)
    else:
        print("No M4A files found (no conversion needed)")

    # Step 2b: Transcode movies to MPEG-1 for the runtime's video player.
    # Originals are still copied so an unconverted title just skips its movie.
    movie_files = find_movie_files(game_dir)
    if movie_files:
        print(f"Found {len(movie_files)} movies to transcode")
        for src_path in movie_files:
            rel = src_path.relative_to(game_dir)
            dst_rel = rel.with_suffix(".mpg")
            dst_path = output_dir / dst_rel

            if dry_run:
                print(f"  [dry-run] transcode: {rel} -> {dst_rel}")
                stats["movies_converted"] += 1
                continue

            if convert_movie_to_mpeg(src_path, dst_path):
                stats["movies_converted"] += 1
                if verbose:
                    print(f"  transcode: {rel} -> {dst_rel}")
            else:
                stats["errors"] += 1
                print(f"  ERROR: failed to transcode movie (is ffmpeg on PATH?): {rel}",
                      file=sys.stderr)
    else:
        print("No movies found (no transcoding needed)")

    # Step 3: Copy unencrypted assets
    if not dry_run:
        print("Copying unencrypted assets...")
        stats["copied"] = copy_unencrypted_assets(game_dir, output_dir, verbose)
    else:
        print("[dry-run] Would copy unencrypted assets")

    # Step 4: Patch System.json to disable encryption flags
    # (since assets are now decrypted)
    if not dry_run:
        dst_system = output_dir / "data" / "System.json"
        if dst_system.exists():
            with open(dst_system, encoding="utf-8-sig") as f:
                sys_data = json.load(f)
            sys_data["hasEncryptedImages"] = False
            sys_data["hasEncryptedAudio"] = False
            sys_data["encryptionKey"] = ""
            with open(dst_system, "w", encoding="utf-8") as f:
                json.dump(sys_data, f, ensure_ascii=False)
            print("Patched System.json: encryption flags disabled")

    return stats


def main():
    parser = argparse.ArgumentParser(
        description="Decrypt RPG Maker MV/MZ assets and prepare for native runtime"
    )
    parser.add_argument("game_dir", help="Path to RPG Maker MZ game directory")
    parser.add_argument("output_dir", help="Path to output directory for converted assets")
    parser.add_argument("--dry-run", action="store_true",
                        help="Show what would be done without making changes")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Print each file being processed")
    args = parser.parse_args()

    game_dir = Path(args.game_dir).resolve()
    output_dir = Path(args.output_dir).resolve()

    if not game_dir.is_dir():
        print(f"Error: game directory not found: {game_dir}", file=sys.stderr)
        sys.exit(1)

    if not (game_dir / "data" / "System.json").exists():
        print(f"Error: not a valid RPG Maker MZ project (no data/System.json)", file=sys.stderr)
        sys.exit(1)

    print(f"Game directory: {game_dir}")
    print(f"Output directory: {output_dir}")
    print()

    stats = run_conversion(game_dir, output_dir, args.dry_run, args.verbose)

    print()
    print("=== Conversion Summary ===")
    print(f"  Decrypted: {stats['decrypted']}")
    print(f"  M4A->OGG:  {stats['m4a_converted']}")
    print(f"  Movies:    {stats['movies_converted']} transcoded to MPEG-1")
    print(f"  Copied:    {stats['copied']}")
    print(f"  Errors:    {stats['errors']}")

    sys.exit(1 if stats["errors"] > 0 else 0)


if __name__ == "__main__":
    main()
