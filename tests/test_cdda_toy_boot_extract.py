#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Sparse generated raw sectors exercise the bounded extractor; no game bytes."""
from dataclasses import replace
import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zlib


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("toy_boot_extractor", ROOT / "tools/extract_cdda_toy_boot.py")
extractor = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = extractor
SPEC.loader.exec_module(extractor)


def address(lba):
    # Independent arithmetic oracle, including the 121-minute GD address.
    fad = lba + 150
    parts = (fad // 4500, fad // 75 % 60, fad % 75)
    return bytes((((part // 10) * 16 + part % 10) & 255) for part in parts)


class ToyBootExtractTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="toy-boot-extract-")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.source = self.base / "original image"
        self.source.mkdir()
        (self.source / "data files").mkdir()
        self.backing = self.source / "data files/track 15.bin"
        self.gdi = self.source / "TOY_COMMANDER.gdi"
        self.output = self.base / "ToyCommander-1GUTH.BIN"
        rows = ["15"]
        for i in range(1, 15):
            rows.append(f'{i} {i * 1000} 0 2352 "unused track {i}.raw" 0')
        rows.append('15 377422 4 2352 "data files/track 15.bin" 0')
        self.descriptor = ("\n".join(rows) + "\n").encode()
        self.gdi.write_bytes(self.descriptor)
        self.payload = bytes((i * 73 + i // 101 + 19) & 255
                             for i in range(extractor.TOY_PROFILE.boot_bytes))
        self.profile = replace(
            extractor.TOY_PROFILE,
            descriptor_bytes=len(self.descriptor),
            descriptor_sha256=hashlib.sha256(self.descriptor).hexdigest(),
            boot_sha256=hashlib.sha256(self.payload).hexdigest(),
            boot_crc32=zlib.crc32(self.payload) & 0xFFFFFFFF,
        )
        self.first = (self.profile.boot_lba - self.profile.track_lba) * 2352
        sectors = (len(self.payload) + 2047) // 2048
        # A 403MB apparent file contains only the 366 generated boot sectors.
        with self.backing.open("wb") as handle:
            handle.truncate(self.profile.track_bytes)
            handle.seek(self.first)
            for sector in range(sectors):
                raw = bytearray(b"\xA6" * 2352)
                raw[:12] = b"\x00" + b"\xFF" * 10 + b"\x00"
                raw[12:15] = address(self.profile.boot_lba + sector)
                raw[15] = 1
                piece = self.payload[sector * 2048:(sector + 1) * 2048]
                raw[16:16 + len(piece)] = piece
                handle.write(raw)

    def extract(self, output=None, profile=None):
        return extractor._extract_boot(self.gdi, output or self.output, profile or self.profile)

    def assert_refused_without_output(self, message, profile=None):
        with self.assertRaisesRegex(extractor.ExtractionError, message):
            self.extract(profile=profile)
        self.assertFalse(os.path.lexists(self.output))
        self.assertFalse(list(self.base.glob(".toy-boot-*.tmp")))

    def change_byte(self, at, value):
        with self.backing.open("r+b") as handle:
            handle.seek(at)
            handle.write(bytes((value,)))

    def test_exact_payload_strips_headers_tail_and_sector_padding(self):
        # The first sector's minute address is C1, which is valid for GD-ROM.
        self.assertEqual(address(self.profile.boot_lba)[0], 0xC1)
        before = self.backing.stat()
        self.assertEqual(self.extract(), self.output)
        self.assertEqual(self.output.read_bytes(), self.payload)
        after = self.backing.stat()
        self.assertEqual((after.st_size, after.st_mtime_ns), (before.st_size, before.st_mtime_ns))
        self.assertEqual(self.gdi.read_bytes(), self.descriptor)
        self.assertFalse(list(self.base.glob(".toy-boot-*.tmp")))

    def test_descriptor_relative_quoted_backing_ignores_working_directory(self):
        previous = Path.cwd()
        try:
            os.chdir(self.base)
            self.extract()
        finally:
            os.chdir(previous)
        self.assertEqual(self.output.read_bytes(), self.payload)

    def test_only_exact_boot_sectors_are_read_and_short_read_is_refused(self):
        real_open = Path.open
        reads, seeks = [], []
        short = False

        class TrackedReader:
            def __init__(self, handle):
                self.handle = handle

            def __enter__(self):
                return self

            def __exit__(self, *args):
                self.handle.close()

            def fileno(self):
                return self.handle.fileno()

            def seek(self, offset):
                seeks.append(offset)
                return self.handle.seek(offset)

            def read(self, count):
                reads.append(count)
                data = self.handle.read(count)
                return data[:-1] if short else data

        def tracked_open(path, *args, **kwargs):
            handle = real_open(path, *args, **kwargs)
            return TrackedReader(handle) if path == self.backing else handle

        with mock.patch.object(Path, "open", new=tracked_open):
            self.extract()
        self.assertEqual(seeks, [402690624])
        self.assertEqual(reads, [2352] * 366)
        self.output.unlink()
        reads.clear()
        seeks.clear()
        short = True
        with mock.patch.object(Path, "open", new=tracked_open):
            self.assert_refused_without_output("Truncated raw boot sector")
        self.assertEqual(reads, [2352])

    def test_production_cli_rejects_synthetic_identity(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/extract_cdda_toy_boot.py"),
                                 str(self.gdi), str(self.output)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("preflight identity", result.stderr)
        self.assertFalse(self.output.exists())

    def test_descriptor_corruption_and_trailing_bytes_are_rejected(self):
        self.gdi.write_bytes(self.descriptor + b"\n")
        self.assert_refused_without_output("preflight identity")

    def test_backing_truncation_is_rejected(self):
        with self.backing.open("r+b") as handle:
            handle.truncate(self.first + 2352)
        self.assert_refused_without_output("backing size")

    def test_sync_mode_and_lba_header_corruption(self):
        for offset, value, message in ((1, 0, "sync"), (15, 2, "Mode 1"), (14, 0xFF, "address")):
            with self.subTest(offset=offset):
                with self.backing.open("rb") as handle:
                    handle.seek(self.first + offset)
                    saved = handle.read(1)[0]
                self.change_byte(self.first + offset, value)
                self.assert_refused_without_output(message)
                self.change_byte(self.first + offset, saved)

    def test_payload_corruption_is_rejected_before_publication(self):
        self.change_byte(self.first + 16, self.payload[0] ^ 1)
        with mock.patch.object(extractor, "_publish") as publish:
            self.assert_refused_without_output("SHA256/CRC32")
            publish.assert_not_called()

    def test_crc_is_checked_independently_of_sha(self):
        self.assert_refused_without_output("SHA256/CRC32",
                                          profile=replace(self.profile, boot_crc32=self.profile.boot_crc32 ^ 1))

    def test_existing_output_and_game_input_are_not_overwritten(self):
        self.output.write_bytes(b"existing output")
        with self.assertRaisesRegex(extractor.ExtractionError, "already exists"):
            self.extract()
        self.assertEqual(self.output.read_bytes(), b"existing output")
        with self.assertRaisesRegex(extractor.ExtractionError, "already exists"):
            self.extract(output=self.gdi)
        self.assertEqual(self.gdi.read_bytes(), self.descriptor)

    def test_nonexistent_other_track_path_cannot_become_output(self):
        with self.assertRaisesRegex(extractor.ExtractionError, "game backing path"):
            self.extract(output=self.source / "unused track 1.raw")
        self.assertFalse((self.source / "unused track 1.raw").exists())

    def test_dangling_output_symlink_is_not_followed(self):
        target = self.base / "missing target"
        try:
            self.output.symlink_to(target)
        except (NotImplementedError, OSError):
            self.skipTest("Symbolic links unavailable")
        with self.assertRaisesRegex(extractor.ExtractionError, "already exists"):
            self.extract()
        self.assertTrue(self.output.is_symlink())
        self.assertFalse(target.exists())

    def test_atomic_publication_race_cannot_clobber_another_file(self):
        real_link = os.link

        def competing_link(source, destination):
            Path(destination).write_bytes(b"concurrent output")
            return real_link(source, destination)

        with mock.patch.object(extractor.os, "link", side_effect=competing_link):
            with self.assertRaisesRegex(extractor.ExtractionError, "already exists"):
                self.extract()
        self.assertEqual(self.output.read_bytes(), b"concurrent output")
        self.assertFalse(list(self.base.glob(".toy-boot-*.tmp")))

    def test_failed_atomic_publication_leaves_no_partial_output(self):
        with mock.patch.object(extractor.os, "link", side_effect=OSError("links unavailable")):
            self.assert_refused_without_output("atomically publish")

    def test_extent_bounds_checked_before_opening_backing(self):
        profile = replace(self.profile, boot_lba=self.profile.track_lba - 1)
        self.assert_refused_without_output("outside", profile=profile)


if __name__ == "__main__":
    unittest.main()
