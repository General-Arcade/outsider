#!/usr/bin/env python3
# Copyright (c) 2026 General Arcade (Pte. Ltd.)
# SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
"""Tests for the build pipeline: resource_converter.py and build_game.py.

Tests the decryption algorithm, resource copying, M4A detection,
System.json patching, and build script structure.
"""

import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

# Add tools/ to path
TOOLS_DIR = Path(__file__).resolve().parent.parent / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import resource_converter


class TestEncryptionKey(unittest.TestCase):
    """Test encryption key parsing."""

    def test_valid_key(self):
        key = resource_converter.parse_encryption_key("d0013f0834670ba59194d07647473ad9")
        self.assertEqual(len(key), 16)
        self.assertEqual(key[0], 0xd0)
        self.assertEqual(key[1], 0x01)
        self.assertEqual(key[15], 0xd9)

    def test_all_zeros_key(self):
        key = resource_converter.parse_encryption_key("0" * 32)
        self.assertEqual(key, b"\x00" * 16)

    def test_invalid_length(self):
        with self.assertRaises(ValueError):
            resource_converter.parse_encryption_key("abcd")

    def test_invalid_hex(self):
        with self.assertRaises(ValueError):
            resource_converter.parse_encryption_key("zz" * 16)


class TestDecryptFile(unittest.TestCase):
    """Test RPG Maker MV/MZ file decryption."""

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.key_hex = "d0013f0834670ba59194d07647473ad9"
        self.key_bytes = resource_converter.parse_encryption_key(self.key_hex)

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def _make_encrypted_file(self, original_data: bytes, filename: str) -> Path:
        """Create an encrypted RPG Maker file from original data."""
        header = b"RPGMV\x00\x00\x00\x00\x03\x01\x00\x00\x00\x00\x00"

        # XOR first 16 bytes with key
        encrypted_block = bytearray(original_data[:16])
        for i in range(min(16, len(encrypted_block))):
            encrypted_block[i] ^= self.key_bytes[i]

        src = Path(self.tmpdir) / "src" / filename
        src.parent.mkdir(parents=True, exist_ok=True)
        with open(src, "wb") as f:
            f.write(header)
            f.write(bytes(encrypted_block))
            if len(original_data) > 16:
                f.write(original_data[16:])

        return src

    def test_decrypt_png(self):
        """Test decryption of a PNG-like file."""
        # Minimal PNG signature + IHDR
        png_data = b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR" + b"\x00" * 100

        src = self._make_encrypted_file(png_data, "test.png_")
        dst = Path(self.tmpdir) / "out" / "test.png"

        result = resource_converter.decrypt_file(src, dst, self.key_bytes)
        self.assertTrue(result)
        self.assertTrue(dst.exists())

        decrypted = dst.read_bytes()
        # First 16 bytes should match original
        self.assertEqual(decrypted[:16], png_data[:16])
        # Rest should match original
        self.assertEqual(decrypted[16:], png_data[16:])

    def test_art_encrypter_key(self):
        """Arthran's variant derives a 32-byte key: MD5 of the stock key's
        hex string, followed by the same bytes reversed."""
        key = resource_converter.art_encrypter_key(self.key_hex)
        import hashlib
        digest = hashlib.md5(self.key_hex.encode()).digest()
        self.assertEqual(len(key), 32)
        self.assertEqual(key[:16], digest)
        self.assertEqual(key[16:], digest[::-1])

    def test_decrypt_art_encrypter_file(self):
        """Files with the ART header use the derived key over 32 bytes and
        need it passed in; without it they are refused."""
        png_data = b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR" + bytes(range(40)) + b"\x00" * 50
        art_key = resource_converter.art_encrypter_key(self.key_hex)
        block = bytearray(png_data[:32])
        for i in range(32):
            block[i] ^= art_key[i]
        src = Path(self.tmpdir) / "src" / "art.png_"
        src.parent.mkdir(parents=True, exist_ok=True)
        src.write_bytes(resource_converter.ART_MAGIC + bytes(block) + png_data[32:])
        dst = Path(self.tmpdir) / "out" / "art.png"

        self.assertFalse(resource_converter.decrypt_file(src, dst, self.key_bytes))
        self.assertTrue(resource_converter.decrypt_file(src, dst, self.key_bytes, art_key))
        self.assertEqual(dst.read_bytes(), png_data)

    def test_decrypt_ogg(self):
        """Test decryption of an OGG-like file."""
        ogg_data = b"OggS\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" + b"\xff" * 200

        src = self._make_encrypted_file(ogg_data, "test.ogg_")
        dst = Path(self.tmpdir) / "out" / "test.ogg"

        result = resource_converter.decrypt_file(src, dst, self.key_bytes)
        self.assertTrue(result)

        decrypted = dst.read_bytes()
        self.assertEqual(decrypted[:4], b"OggS")
        self.assertEqual(decrypted, ogg_data)

    def test_decrypt_creates_directories(self):
        """Test that decrypt_file creates parent directories."""
        data = b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR" + b"\x00" * 50

        src = self._make_encrypted_file(data, "img/test.png_")
        dst = Path(self.tmpdir) / "deep" / "nested" / "dir" / "test.png"

        result = resource_converter.decrypt_file(src, dst, self.key_bytes)
        self.assertTrue(result)
        self.assertTrue(dst.exists())

    def test_reject_non_rpgmv(self):
        """Test that non-RPGMV files are rejected."""
        src = Path(self.tmpdir) / "bad.png_"
        src.write_bytes(b"NOT_RPGMV_HEADER_DATA_" + b"\x00" * 50)
        dst = Path(self.tmpdir) / "out" / "bad.png"

        result = resource_converter.decrypt_file(src, dst, self.key_bytes)
        self.assertFalse(result)
        self.assertFalse(dst.exists())

    def test_reject_too_small(self):
        """Test that files smaller than header + encrypted block are rejected."""
        src = Path(self.tmpdir) / "tiny.png_"
        src.write_bytes(b"RPGMV\x00\x00\x00\x00\x03\x01\x00")
        dst = Path(self.tmpdir) / "out" / "tiny.png"

        result = resource_converter.decrypt_file(src, dst, self.key_bytes)
        self.assertFalse(result)

    def test_roundtrip_preserves_data(self):
        """Test that encrypt->decrypt roundtrip preserves original data exactly."""
        original = bytes(range(256)) * 4  # 1024 bytes of test data

        src = self._make_encrypted_file(original, "data.png_")
        dst = Path(self.tmpdir) / "out" / "data.png"

        result = resource_converter.decrypt_file(src, dst, self.key_bytes)
        self.assertTrue(result)
        self.assertEqual(dst.read_bytes(), original)


class TestFindEncryptedFiles(unittest.TestCase):
    """Test encrypted file discovery."""

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_finds_png_and_ogg(self):
        game_dir = Path(self.tmpdir)
        (game_dir / "img").mkdir()
        (game_dir / "audio").mkdir()
        (game_dir / "img" / "test.png_").write_bytes(b"data")
        (game_dir / "audio" / "test.ogg_").write_bytes(b"data")
        (game_dir / "data.json").write_bytes(b"data")

        results = resource_converter.find_encrypted_files(game_dir)
        extensions = {ext for _, ext in results}
        self.assertEqual(len(results), 2)
        self.assertIn(".png", extensions)
        self.assertIn(".ogg", extensions)

    def test_empty_directory(self):
        results = resource_converter.find_encrypted_files(Path(self.tmpdir))
        self.assertEqual(len(results), 0)

    def test_nested_files(self):
        game_dir = Path(self.tmpdir)
        (game_dir / "img" / "titles1").mkdir(parents=True)
        (game_dir / "img" / "titles1" / "Title.png_").write_bytes(b"data")

        results = resource_converter.find_encrypted_files(game_dir)
        self.assertEqual(len(results), 1)


class TestFindM4aFiles(unittest.TestCase):
    """Test M4A file discovery."""

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_no_m4a(self):
        results = resource_converter.find_m4a_files(Path(self.tmpdir))
        self.assertEqual(len(results), 0)

    def test_finds_m4a(self):
        game_dir = Path(self.tmpdir)
        (game_dir / "audio" / "bgm").mkdir(parents=True)
        (game_dir / "audio" / "bgm" / "theme.m4a").write_bytes(b"data")

        results = resource_converter.find_m4a_files(game_dir)
        self.assertEqual(len(results), 1)


class TestFindMovieFiles(unittest.TestCase):
    """Test movie discovery for MPEG-1 transcoding."""

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.game_dir = Path(self.tmpdir)

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_no_movies_dir(self):
        self.assertEqual(resource_converter.find_movie_files(self.game_dir), [])

    def test_prefers_webm_over_mp4_per_title(self):
        movies = self.game_dir / "movies"
        movies.mkdir()
        (movies / "scene1.webm").write_bytes(b"w")
        (movies / "scene1.mp4").write_bytes(b"m")
        (movies / "credits.mp4").write_bytes(b"m")
        (movies / "notes.txt").write_bytes(b"x")

        results = resource_converter.find_movie_files(self.game_dir)
        self.assertEqual([r.name for r in results], ["credits.mp4", "scene1.webm"])

    def test_transcode_with_ffmpeg(self):
        """End to end through ffmpeg; skipped when it is not installed."""
        import shutil
        import subprocess
        ffmpeg = resource_converter.ffmpeg_command()
        if shutil.which(ffmpeg) is None:
            self.skipTest("ffmpeg not available")
        movies = self.game_dir / "movies"
        movies.mkdir()
        src = movies / "clip.mp4"
        # A 15 fps source: not a legal MPEG-1 rate, so the 30 fps retry must kick in.
        subprocess.run([ffmpeg, "-y", "-v", "error",
                        "-f", "lavfi", "-i", "testsrc=size=34x18:rate=15:duration=0.5",
                        "-c:v", "libx264", "-pix_fmt", "yuv420p", str(src)],
                       check=True, capture_output=True, timeout=120)
        dst = self.game_dir / "out" / "movies" / "clip.mpg"
        self.assertTrue(resource_converter.convert_movie_to_mpeg(src, dst))
        self.assertTrue(dst.exists())
        # MPEG program stream pack header.
        self.assertEqual(dst.read_bytes()[:4], b"\x00\x00\x01\xba")


class TestCopyUnencryptedAssets(unittest.TestCase):
    """Test unencrypted asset copying."""

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.game_dir = Path(self.tmpdir) / "game"
        self.out_dir = Path(self.tmpdir) / "out"
        self.game_dir.mkdir()
        self.out_dir.mkdir()

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_copies_json(self):
        (self.game_dir / "data").mkdir()
        (self.game_dir / "data" / "Actors.json").write_text('{"test": true}')

        count = resource_converter.copy_unencrypted_assets(self.game_dir, self.out_dir)
        self.assertEqual(count, 1)
        self.assertTrue((self.out_dir / "data" / "Actors.json").exists())

    def test_copies_fonts(self):
        (self.game_dir / "fonts").mkdir()
        (self.game_dir / "fonts" / "game.ttf").write_bytes(b"font_data")

        count = resource_converter.copy_unencrypted_assets(self.game_dir, self.out_dir)
        self.assertEqual(count, 1)
        self.assertTrue((self.out_dir / "fonts" / "game.ttf").exists())

    def test_skips_nwjs_files(self):
        (self.game_dir / "Game.exe").write_bytes(b"exe")
        (self.game_dir / "nw.dll").write_bytes(b"dll")
        (self.game_dir / "d3dcompiler_47.dll").write_bytes(b"dll")

        count = resource_converter.copy_unencrypted_assets(self.game_dir, self.out_dir)
        self.assertEqual(count, 0)

    def test_skips_encrypted_files(self):
        (self.game_dir / "img").mkdir()
        (self.game_dir / "img" / "test.png_").write_bytes(b"encrypted")
        (self.game_dir / "audio").mkdir()
        (self.game_dir / "audio" / "test.ogg_").write_bytes(b"encrypted")

        count = resource_converter.copy_unencrypted_assets(self.game_dir, self.out_dir)
        self.assertEqual(count, 0)

    def test_skips_replaced_js_libs(self):
        """pixi.js and effekseer are replaced by native, should not be copied."""
        libs_dir = self.game_dir / "js" / "libs"
        libs_dir.mkdir(parents=True)
        (libs_dir / "pixi.js").write_bytes(b"pixi")
        (libs_dir / "effekseer.min.js").write_bytes(b"effekseer")
        (libs_dir / "effekseer.wasm").write_bytes(b"wasm")
        (libs_dir / "vorbisdecoder.js").write_bytes(b"vorbis")
        # pako and localforage should be copied
        (libs_dir / "pako.min.js").write_bytes(b"pako")
        (libs_dir / "localforage.min.js").write_bytes(b"localforage")

        count = resource_converter.copy_unencrypted_assets(self.game_dir, self.out_dir)
        self.assertEqual(count, 2)
        self.assertTrue((self.out_dir / "js" / "libs" / "pako.min.js").exists())
        self.assertTrue((self.out_dir / "js" / "libs" / "localforage.min.js").exists())
        self.assertFalse((self.out_dir / "js" / "libs" / "pixi.js").exists())
        self.assertFalse((self.out_dir / "js" / "libs" / "effekseer.min.js").exists())

    def test_copies_js_core_and_plugins(self):
        js_dir = self.game_dir / "js"
        js_dir.mkdir()
        (js_dir / "rmmz_core.js").write_text("// core")
        (js_dir / "main.js").write_text("// main")
        plugins_dir = js_dir / "plugins"
        plugins_dir.mkdir()
        (plugins_dir / "MyPlugin.js").write_text("// plugin")

        count = resource_converter.copy_unencrypted_assets(self.game_dir, self.out_dir)
        self.assertEqual(count, 3)

    def test_copies_effects(self):
        (self.game_dir / "effects").mkdir()
        (self.game_dir / "effects" / "Slash.efkefc").write_bytes(b"effect")
        (self.game_dir / "effects" / "tex.png").write_bytes(b"texture")

        count = resource_converter.copy_unencrypted_assets(self.game_dir, self.out_dir)
        self.assertEqual(count, 2)

    def test_skips_platform_dirs(self):
        (self.game_dir / "locales").mkdir()
        (self.game_dir / "locales" / "en-US.pak").write_bytes(b"locale")
        (self.game_dir / "lib").mkdir()
        (self.game_dir / "lib" / "something.so").write_bytes(b"lib")

        count = resource_converter.copy_unencrypted_assets(self.game_dir, self.out_dir)
        self.assertEqual(count, 0)


class TestRunConversion(unittest.TestCase):
    """Test the full conversion pipeline."""

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.game_dir = Path(self.tmpdir) / "game"
        self.out_dir = Path(self.tmpdir) / "out"
        self._setup_game_dir()

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def _setup_game_dir(self):
        """Create a minimal RPG Maker MZ game directory."""
        self.game_dir.mkdir(parents=True)
        data_dir = self.game_dir / "data"
        data_dir.mkdir()

        # System.json with encryption key
        system_data = {
            "hasEncryptedImages": True,
            "hasEncryptedAudio": True,
            "encryptionKey": "d0013f0834670ba59194d07647473ad9",
            "gameTitle": "Test Game",
        }
        (data_dir / "System.json").write_text(json.dumps(system_data))
        (data_dir / "Actors.json").write_text('[null, {"name": "Hero"}]')

        # Create an encrypted PNG
        key_bytes = resource_converter.parse_encryption_key(system_data["encryptionKey"])
        png_data = b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR" + b"\xAB" * 100
        header = b"RPGMV\x00\x00\x00\x00\x03\x01\x00\x00\x00\x00\x00"
        encrypted_block = bytearray(png_data[:16])
        for i in range(16):
            encrypted_block[i] ^= key_bytes[i]

        img_dir = self.game_dir / "img" / "titles1"
        img_dir.mkdir(parents=True)
        with open(img_dir / "Title.png_", "wb") as f:
            f.write(header)
            f.write(bytes(encrypted_block))
            f.write(png_data[16:])

        # Create font files
        fonts_dir = self.game_dir / "fonts"
        fonts_dir.mkdir()
        (fonts_dir / "game.ttf").write_bytes(b"font_data")

        # Create JS files
        js_dir = self.game_dir / "js"
        js_dir.mkdir()
        (js_dir / "rmmz_core.js").write_text("// core")
        (js_dir / "main.js").write_text("// main")

    def test_full_pipeline(self):
        stats = resource_converter.run_conversion(self.game_dir, self.out_dir)

        self.assertEqual(stats["decrypted"], 1)
        self.assertEqual(stats["errors"], 0)
        self.assertGreater(stats["copied"], 0)

        # Decrypted PNG should exist with correct extension
        decrypted = self.out_dir / "img" / "titles1" / "Title.png"
        self.assertTrue(decrypted.exists())
        data = decrypted.read_bytes()
        self.assertTrue(data.startswith(b"\x89PNG"))

        # System.json should have encryption disabled
        sys_json = self.out_dir / "data" / "System.json"
        self.assertTrue(sys_json.exists())
        with open(sys_json) as f:
            sys_data = json.load(f)
        self.assertFalse(sys_data["hasEncryptedImages"])
        self.assertFalse(sys_data["hasEncryptedAudio"])
        self.assertEqual(sys_data["encryptionKey"], "")

    def test_dry_run_no_files_created(self):
        stats = resource_converter.run_conversion(self.game_dir, self.out_dir, dry_run=True)

        self.assertEqual(stats["decrypted"], 1)
        self.assertFalse(self.out_dir.exists() and any(self.out_dir.iterdir()))

    def test_preserves_directory_structure(self):
        resource_converter.run_conversion(self.game_dir, self.out_dir)

        self.assertTrue((self.out_dir / "data" / "System.json").exists())
        self.assertTrue((self.out_dir / "data" / "Actors.json").exists())
        self.assertTrue((self.out_dir / "fonts" / "game.ttf").exists())
        self.assertTrue((self.out_dir / "js" / "rmmz_core.js").exists())


class TestActualGameFiles(unittest.TestCase):
    """Test with actual game files if available (integration tests)."""

    GAME_DIR = Path(os.environ.get("RMMZ_TEST_GAME_DIR", "") or "/nonexistent")

    @unittest.skipUnless(
        (GAME_DIR / "data" / "System.json").exists(),
        "Game directory not available"
    )
    def test_decrypt_actual_png(self):
        """Verify decryption works on an actual game PNG file."""
        system_json = self.GAME_DIR / "data" / "System.json"
        with open(system_json) as f:
            sys_data = json.load(f)

        key = resource_converter.parse_encryption_key(sys_data["encryptionKey"])

        # Find a real encrypted PNG
        encrypted_files = list((self.GAME_DIR / "img" / "titles1").glob("*.png_"))
        if not encrypted_files:
            self.skipTest("No encrypted PNG files found")

        src = encrypted_files[0]
        with tempfile.TemporaryDirectory() as tmpdir:
            dst = Path(tmpdir) / "decrypted.png"
            result = resource_converter.decrypt_file(src, dst, key)
            self.assertTrue(result)

            data = dst.read_bytes()
            # Should start with PNG signature
            self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
            # Should have valid IHDR chunk
            self.assertEqual(data[12:16], b"IHDR")

    @unittest.skipUnless(
        (GAME_DIR / "data" / "System.json").exists(),
        "Game directory not available"
    )
    def test_decrypt_actual_ogg(self):
        """Verify decryption works on an actual game OGG file."""
        system_json = self.GAME_DIR / "data" / "System.json"
        with open(system_json) as f:
            sys_data = json.load(f)

        key = resource_converter.parse_encryption_key(sys_data["encryptionKey"])

        # Find a real encrypted OGG
        encrypted_files = list((self.GAME_DIR / "audio" / "bgm").glob("*.ogg_"))
        if not encrypted_files:
            self.skipTest("No encrypted OGG files found")

        src = encrypted_files[0]
        with tempfile.TemporaryDirectory() as tmpdir:
            dst = Path(tmpdir) / "decrypted.ogg"
            result = resource_converter.decrypt_file(src, dst, key)
            self.assertTrue(result)

            data = dst.read_bytes()
            # Should start with OGG signature
            self.assertEqual(data[:4], b"OggS")

    @unittest.skipUnless(
        (GAME_DIR / "data" / "System.json").exists(),
        "Game directory not available"
    )
    def test_no_m4a_files(self):
        """Verify the game has no M4A files (all audio is OGG)."""
        m4a = resource_converter.find_m4a_files(self.GAME_DIR)
        self.assertEqual(len(m4a), 0)

    @unittest.skipUnless(
        (GAME_DIR / "data" / "System.json").exists(),
        "Game directory not available"
    )
    def test_encrypted_file_count(self):
        """Verify we find the expected number of encrypted files."""
        files = resource_converter.find_encrypted_files(self.GAME_DIR)
        png_count = sum(1 for _, ext in files if ext == ".png")
        ogg_count = sum(1 for _, ext in files if ext == ".ogg")
        # The game has ~2430 encrypted PNGs and ~628 encrypted OGGs
        self.assertGreater(png_count, 2000)
        self.assertGreater(ogg_count, 500)

    @unittest.skipUnless(
        (GAME_DIR / "data" / "System.json").exists(),
        "Game directory not available"
    )
    def test_effekseer_files_not_encrypted(self):
        """Verify Effekseer effects are plain files (not encrypted)."""
        efkefc_files = list(self.GAME_DIR.rglob("*.efkefc"))
        self.assertGreater(len(efkefc_files), 0)
        # First few bytes should NOT be RPGMV
        data = efkefc_files[0].read_bytes()[:16]
        self.assertFalse(data.startswith(b"RPGMV"))


if __name__ == "__main__":
    unittest.main()
