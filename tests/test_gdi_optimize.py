# SPDX-License-Identifier: GPL-3.0-only
"""Original synthetic sectors; check conversion data and failure isolation."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

from games_fixture import raw_sector

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("gdi_optimize", ROOT / "tools/gdi_optimize.py")
converter = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(converter)


class GdiOptimizeTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="kui-cooked-gdi-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "original"
        self.source.mkdir()
        self.output = self.root / "optimized"
        self.gdi = self.source / "Synthetic game.gdi"
        self.payloads = [bytes((i * 17 + salt) & 255 for i in range(2048)) for salt in (5, 11, 43)]
        self.audio = bytes((i * 31 + 97) & 255 for i in range(2352 * 2))
        self.write_mixed()

    def write_mixed(self):
        (self.source / "data one.bin").write_bytes(raw_sector(0, self.payloads[0]))
        (self.source / "music track.raw").write_bytes(self.audio)
        (self.source / "high.bin").write_bytes(
            raw_sector(45000, self.payloads[1]) + raw_sector(45001, self.payloads[2]))
        self.gdi.write_text('3\n1 0 4 2352 "data one.bin" 0\n'
                            '2 150 0 2352 "music track.raw" 0\n'
                            '3 45000 4 2352 high.bin 0\n', encoding="ascii")

    def snapshot(self):
        return {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in self.source.iterdir()}

    def assert_not_published(self):
        self.assertFalse(self.output.exists())
        self.assertEqual(list(self.root.glob(".*.staging-*")), [])

    def test_mixed_image_exact_payload_audio_gaps_and_provenance(self):
        before = self.snapshot()
        report = converter.optimize(self.gdi, self.output)
        self.assertEqual(self.snapshot(), before)
        self.assertEqual((self.output / "track01.iso").read_bytes(), self.payloads[0])
        self.assertEqual((self.output / "track03.iso").read_bytes(), b"".join(self.payloads[1:]))
        self.assertEqual((self.output / "track02.raw").read_bytes(), self.audio)
        self.assertEqual((self.output / self.gdi.name).read_text(),
                         '3\n1 0 4 2048 "track01.iso" 0\n2 150 0 2352 "track02.raw" 0\n'
                         '3 45000 4 2048 "track03.iso" 0\n')
        saved = json.loads((self.output / "conversion.json").read_text())
        self.assertEqual(saved, report)
        self.assertEqual(report["source_data_bytes"], 2352 * 3)
        self.assertEqual(report["output_data_bytes"], 2048 * 3)
        self.assertEqual(report["saved_data_bytes"], 304 * 3)
        self.assertFalse(report["verification"]["catalogue_match_checked"])
        self.assertFalse(report["verification"]["edc_ecc_checked"])
        for track in report["tracks"]:
            for side, directory in (("source", self.source), ("output", self.output)):
                data = (directory / track[side]["file"]).read_bytes()
                self.assertEqual(track[side]["sha256"], hashlib.sha256(data).hexdigest())
                self.assertEqual(track[side]["crc32"], f"{zlib.crc32(data):08x}")
                self.assertEqual(track[side]["bytes"], len(data))
        self.assertNotEqual(report["tracks"][0]["source"]["sha256"], report["tracks"][0]["output"]["sha256"])
        self.assertEqual(report["tracks"][1]["source"]["sha256"], report["tracks"][1]["output"]["sha256"])
        self.assertTrue(report["tracks"][0]["raw_mode1_headers_checked"])
        self.assertFalse(report["tracks"][1]["raw_mode1_headers_checked"])

    def test_existing_2048_data_is_copied_and_audio_remains_2352(self):
        converter.optimize(self.gdi, self.output)
        second = self.root / "second-copy"
        report = converter.optimize(self.output / self.gdi.name, second)
        self.assertEqual(report["saved_data_bytes"], 0)
        self.assertTrue(all(track["operation"] == "copy" for track in report["tracks"]))
        for name in ("track01.iso", "track02.raw", "track03.iso", self.gdi.name):
            self.assertEqual((self.output / name).read_bytes(), (second / name).read_bytes())

    def test_one_sector_extended_minute_address(self):
        # A GD-ROM can pass minute 99; don't mistake extended packed decimal
        # 0xA0 for an invalid CD header and reject the end of a real dump.
        self.gdi.write_text("1\n1 449850 4 2352 late.bin 0\n")
        (self.source / "late.bin").write_bytes(raw_sector(449850, self.payloads[0]))
        report = converter.optimize(self.gdi, self.output)
        self.assertEqual(report["tracks"][0]["sectors"], 1)
        self.assertEqual((self.output / "track01.iso").read_bytes(), self.payloads[0])

    def test_streaming_chunk_boundaries(self):
        sectors = 129
        self.gdi.write_text("1\n1 45000 4 2352 big.bin 0\n")
        payloads = [bytes([index]) * 2048 for index in range(sectors)]
        (self.source / "big.bin").write_bytes(b"".join(
            raw_sector(45000 + index, data) for index, data in enumerate(payloads)))
        converter.optimize(self.gdi, self.output)
        self.assertEqual((self.output / "track01.iso").read_bytes(), b"".join(payloads))

    def test_invalid_sync_mode_and_sector_address_cleanup(self):
        original = (self.source / "high.bin").read_bytes()
        for offset, value, message in ((0, 1, "sync"), (15, 2, "mode"), (13, 0, "address")):
            with self.subTest(message=message):
                bad = bytearray(original)
                bad[offset] = value
                (self.source / "high.bin").write_bytes(bad)
                before = self.snapshot()
                with self.assertRaisesRegex(ValueError, message):
                    converter.optimize(self.gdi, self.output)
                self.assertEqual(self.snapshot(), before)
                self.assert_not_published()
        (self.source / "high.bin").write_bytes(original)

    def test_partial_sector_and_empty_file_rejected_before_output(self):
        target = self.source / "high.bin"
        for data in (b"", b"x" * 2351, b"x" * 2353):
            target.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "whole number of sectors"):
                converter.optimize(self.gdi, self.output)
            self.assert_not_published()

    def test_track_cannot_cross_density_session_boundary(self):
        self.gdi.write_text("1\n1 44999 4 2352 high.bin 0\n")
        (self.source / "high.bin").write_bytes(
            raw_sector(44999, self.payloads[0]) + raw_sector(45000, self.payloads[1]))
        with self.assertRaisesRegex(ValueError, "session boundary"):
            converter.optimize(self.gdi, self.output)
        self.assert_not_published()

    def test_descriptor_rejects_unsupported_offsets_audio_sizes_and_bounds(self):
        valid = self.gdi.read_text()
        cases = {
            "offset": valid.replace('high.bin 0', 'high.bin 1'),
            "audio-size": valid.replace('2 150 0 2352', '2 150 0 2048'),
            "missing-row": valid.rsplit("3 45000", 1)[0],
            "extra-row": valid + "4 47000 4 2352 high.bin 0\n",
            "overlap": valid.replace("2 150", "2 0"),
            "unsafe-name": valid.replace("high.bin", "../high.bin"),
            "bad-number": valid.replace("3 45000", "4 45000"),
            "duplicate": valid.replace("high.bin", '"DATA ONE.BIN"'),
            "out-of-range": valid.replace("3 45000", f"3 {converter.LBA_LIMIT}"),
            "overflow-count": "100\n",
            "oversized-gdi": valid + " " * converter.GDI_LIMIT,
        }
        for label, content in cases.items():
            with self.subTest(label=label):
                self.gdi.write_text(content)
                with self.assertRaises(ValueError):
                    converter.optimize(self.gdi, self.output)
                self.assert_not_published()
        self.gdi.write_text(valid)

    def test_existing_empty_or_populated_directory_never_overwritten(self):
        self.output.mkdir()
        for populated in (False, True):
            if populated:
                (self.output / "keep.txt").write_text("existing user data")
            with self.assertRaisesRegex(ValueError, "already exists"):
                converter.optimize(self.gdi, self.output)
            self.assertTrue(self.output.is_dir())
        self.assertEqual((self.output / "keep.txt").read_text(), "existing user data")
        self.assertEqual(list(self.root.glob(".*.staging-*")), [])

    def test_source_directory_and_descendant_output_are_rejected(self):
        before = self.snapshot()
        for output in (self.source, self.source / "optimized"):
            with self.assertRaises(ValueError):
                converter.optimize(self.gdi, output)
        self.assertEqual(self.snapshot(), before)
        self.assertFalse((self.source / "optimized").exists())

    def test_existing_target_created_at_publication_is_not_replaced(self):
        publish = converter.publish_directory

        def intervening_directory(stage, output):
            output.mkdir()
            publish(stage, output)

        with patch.object(converter, "publish_directory", intervening_directory):
            with self.assertRaises(FileExistsError):
                converter.optimize(self.gdi, self.output)
        self.assertTrue(self.output.is_dir())
        self.assertEqual(list(self.output.iterdir()), [])
        self.assertEqual(list(self.root.glob(".*.staging-*")), [])

    def test_keyboard_interrupt_cleans_staging_and_preserves_originals(self):
        before = self.snapshot()

        def interrupted(message):
            if "2/3" in message:
                raise KeyboardInterrupt

        with self.assertRaises(KeyboardInterrupt):
            converter.optimize(self.gdi, self.output, progress=interrupted)
        self.assertEqual(self.snapshot(), before)
        self.assert_not_published()

    def test_source_changed_after_track_copy_fails_without_publication(self):
        copy_track = converter.stream_track

        def changed_source(track, output):
            record = copy_track(track, output)
            if track.number == 2:
                first = self.source / "data one.bin"
                first.write_bytes(first.read_bytes()[:-1] + b"!")
            return record

        with patch.object(converter, "stream_track", changed_source):
            with self.assertRaisesRegex(ValueError, "source changed"):
                converter.optimize(self.gdi, self.output)
        self.assert_not_published()

    def test_symbolic_track_is_rejected(self):
        source = self.source / "high.bin"
        actual = self.root / "actual.bin"
        source.rename(actual)
        source.symlink_to(actual)
        with self.assertRaisesRegex(ValueError, "regular file"):
            converter.optimize(self.gdi, self.output)
        self.assert_not_published()

    def test_cli_outputs_new_copy_and_fails_for_existing_target(self):
        command = [sys.executable, str(ROOT / "tools/gdi_optimize.py"), str(self.gdi), str(self.output)]
        run = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("912 data bytes saved", run.stdout)
        repeat = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(repeat.returncode, 1)
        self.assertIn("already exists", repeat.stderr)


if __name__ == "__main__":
    unittest.main()
