# SPDX-License-Identifier: GPL-3.0-only
"""Original synthetic sectors; check conversion data and failure isolation."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
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

    def test_bom_blank_lines_and_cr_endings_normalize_without_changing_tracks(self):
        text = self.gdi.read_text()
        self.gdi.write_bytes(b"\xef\xbb\xbf" + ("\r \t\r" + text.replace("\n", "\r\r") + "\r").encode("utf-8"))
        before = self.snapshot()
        converter.optimize(self.gdi, self.output)
        self.assertEqual(self.snapshot(), before)
        self.assertEqual((self.output / "track03.iso").read_bytes(), b"".join(self.payloads[1:]))
        self.assertEqual((self.output / "track02.raw").read_bytes(), self.audio)
        descriptor = (self.output / self.gdi.name).read_bytes()
        self.assertTrue(descriptor.startswith(b"3\n1 0 4 2048"))
        self.assertNotIn(b"\r", descriptor)
        self.assertNotIn(b"\xef\xbb\xbf", descriptor)

    def test_utf8_source_names_are_normalized_to_portable_output_tracks(self):
        (self.source / "high.bin").rename(self.source / "évolution.bin")
        self.gdi.write_text(self.gdi.read_text().replace("high.bin", "évolution.bin"), encoding="utf-8")
        self.gdi = self.gdi.rename(self.source / "Évolution.gdi")
        before = self.snapshot()
        report = converter.optimize(self.gdi, self.output)
        self.assertEqual(self.snapshot(), before)
        self.assertEqual(report["tracks"][2]["source"]["file"], "évolution.bin")
        self.assertEqual((self.output / "track03.iso").read_bytes(), b"".join(self.payloads[1:]))
        self.assertTrue((self.output / "Évolution.gdi").is_file())
        self.assertTrue(converter.converted_directory(self.output))

    def test_case_mismatched_track_reference_records_actual_file_name(self):
        self.gdi.write_text(self.gdi.read_text().replace("high.bin", "HIGH.BIN"))
        before = self.snapshot()
        report = converter.optimize(self.gdi, self.output)
        self.assertEqual(self.snapshot(), before)
        self.assertEqual(report["tracks"][2]["source"]["descriptor_file"], "HIGH.BIN")
        self.assertEqual((self.source / report["tracks"][2]["source"]["file"]).read_bytes(),
                         (self.source / "high.bin").read_bytes())
        self.assertEqual((self.output / "track03.iso").read_bytes(), b"".join(self.payloads[1:]))

    def test_ambiguous_case_match_and_case_matched_symlink_are_refused(self):
        if (self.source / "HIGH.BIN").exists():
            self.skipTest("case-insensitive host filesystem")
        self.gdi.write_text(self.gdi.read_text().replace("high.bin", "HIGH.BIN"))
        duplicate = self.source / "High.Bin"
        shutil.copyfile(self.source / "high.bin", duplicate)
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, "ambiguous case-insensitive"):
            converter.optimize(self.gdi, self.output)
        self.assertEqual(self.snapshot(), before)
        self.assert_not_published()
        duplicate.unlink()
        actual = self.root / "actual.bin"
        (self.source / "high.bin").rename(actual)
        (self.source / "high.bin").symlink_to(actual)
        with self.assertRaisesRegex(ValueError, "regular file"):
            converter.optimize(self.gdi, self.output)
        self.assert_not_published()

    def test_missing_track_reports_track_number_and_descriptor_name(self):
        (self.source / "high.bin").unlink()
        with self.assertRaisesRegex(ValueError, "Track 3: track file missing: high.bin"):
            converter.optimize(self.gdi, self.output)
        self.assert_not_published()

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


class GdiOptimizeBatchTests(unittest.TestCase):
    setUp = GdiOptimizeTests.setUp
    write_mixed = GdiOptimizeTests.write_mixed
    snapshot = GdiOptimizeTests.snapshot

    def clone(self, directory):
        directory.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(self.source, directory)
        return directory / self.gdi.name

    def tree_hashes(self, directory):
        return {str(path.relative_to(directory)): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in directory.rglob("*") if path.is_file() and not path.is_symlink()}

    def assert_report_hashes(self, source, output):
        report = json.loads((output / "conversion.json").read_text())
        for track in report["tracks"]:
            for side, directory in (("source", source), ("output", output)):
                data = (directory / track[side]["file"]).read_bytes()
                self.assertEqual(track[side]["sha256"], hashlib.sha256(data).hexdigest())
                self.assertEqual(track[side]["crc32"], f"{zlib.crc32(data):08x}")
                self.assertEqual(track[side]["bytes"], len(data))

    def test_batch_recursive_raw_cooked_audio_exact_bytes_and_originals(self):
        second = self.root / "collection" / "Second game"
        second_gdi = self.clone(second)
        (second / "data one.bin").write_bytes(self.payloads[0])
        second_gdi.write_text(second_gdi.read_text().replace("1 0 4 2352", "1 0 4 2048"))
        before = self.tree_hashes(self.root)
        result = converter.optimize_batch(self.root)
        self.assertEqual(result["converted"], 2)
        self.assertEqual(result["failed"], 0)
        self.assertEqual(result["saved_data_bytes"], 304 * 5)
        for source in (self.source, second):
            output = source.with_name(source.name + "-2048")
            self.assertEqual((output / "track01.iso").read_bytes(), self.payloads[0])
            self.assertEqual((output / "track03.iso").read_bytes(), b"".join(self.payloads[1:]))
            self.assertEqual((output / "track02.raw").read_bytes(), self.audio)
            self.assert_report_hashes(source, output)
        after = self.tree_hashes(self.root)
        for path, digest in before.items():
            self.assertEqual(after[path], digest)
        self.assertEqual(list(self.root.rglob("*-2048-2048")), [])

    def test_batch_rerun_leaves_outputs_unchanged_without_reconversion(self):
        self.assertEqual(converter.optimize_batch(self.root)["converted"], 1)
        before = self.tree_hashes(self.root)
        messages = []
        result = converter.optimize_batch(self.root, messages.append)
        self.assertEqual(result["converted"], 0)
        self.assertEqual(result["failed"], 0)
        self.assertEqual(result["skipped"], 2)
        self.assertTrue(all("not reverified" in message for message in messages))
        self.assertEqual(self.tree_hashes(self.root), before)
        self.assertFalse((self.root / "original-2048-2048").exists())

    def test_batch_root_can_be_one_game(self):
        result = converter.optimize_batch(self.source)
        self.assertEqual(result["converted"], 1)
        self.assertTrue((self.root / "original-2048" / self.gdi.name).is_file())

    def test_batch_non_gdi_images_are_reported_instead_of_silently_missed(self):
        other = self.root / "Evolution 1"
        other.mkdir()
        (other / "disc.cdi").write_bytes(b"unparsed disc image")
        (other / "cover.jpg").write_bytes(b"unparsed cover")
        before = self.tree_hashes(other)
        result = converter.optimize_batch(self.root)
        self.assertEqual((result["converted"], result["skipped"], result["failed"]), (1, 1, 0))
        entry = next(entry for entry in result["entries"] if entry["path"] == str(other))
        self.assertEqual(entry["status"], "skipped")
        self.assertIn("no GDI descriptor", entry["message"])
        self.assertIn("disc.cdi", entry["message"])
        self.assertNotIn("cover.jpg", entry["message"])
        self.assertEqual(self.tree_hashes(other), before)
        self.assertFalse((self.root / "Evolution 1-2048").exists())

    def test_batch_invalid_game_and_ambiguous_folder_do_not_block_good_game(self):
        bad = self.root / "Bad game"
        self.clone(bad)
        damaged = bytearray((bad / "high.bin").read_bytes())
        damaged[15] = 2
        (bad / "high.bin").write_bytes(damaged)
        ambiguous = self.root / "Ambiguous"
        ambiguous_gdi = self.clone(ambiguous)
        shutil.copyfile(ambiguous_gdi, ambiguous / "Other.gdi")
        before = self.tree_hashes(self.root)
        messages = []
        result = converter.optimize_batch(self.root, messages.append)
        self.assertEqual(result["converted"], 1)
        self.assertEqual(result["failed"], 2)
        self.assertTrue(any("unsupported sector mode 2" in message for message in messages))
        self.assertTrue(any("multiple GDI descriptors" in message for message in messages))
        for path, digest in before.items():
            self.assertEqual(self.tree_hashes(self.root)[path], digest)
        self.assertFalse((self.root / "Bad game-2048").exists())
        self.assertFalse((self.root / "Ambiguous-2048").exists())
        self.assertEqual(list(self.root.rglob(".*.staging-*")), [])

    def test_batch_never_follows_symbolic_directories_descriptors_or_tracks(self):
        (self.root / "Linked directory").symlink_to(self.source, target_is_directory=True)
        (self.root / "Linked.gdi").symlink_to(self.gdi)
        (self.root / "Broken.gdi").symlink_to(self.root / "missing.gdi")
        symbolic = self.root / "Symbolic track game"
        self.clone(symbolic)
        (symbolic / "high.bin").unlink()
        (symbolic / "high.bin").symlink_to(self.source / "high.bin")
        result = converter.optimize_batch(self.root)
        self.assertEqual(result["converted"], 1)
        self.assertEqual(result["failed"], 1)
        self.assertGreaterEqual(result["skipped"], 3)
        self.assertFalse((self.root / "Linked directory-2048").exists())
        self.assertFalse((self.root / "Symbolic track game-2048").exists())
        with self.assertRaisesRegex(ValueError, "Symbolic-link directory"):
            converter.optimize_batch(self.root / "Linked directory")

    def test_batch_hidden_staging_and_generated_trees_are_pruned(self):
        self.clone(self.root / ".hidden" / "Never convert")
        self.clone(self.root / ".unfinished.staging-123")
        converter.optimize(self.gdi, self.output)
        self.clone(self.output / "Nested source")
        result = converter.optimize_batch(self.root)
        self.assertEqual(result["converted"], 1)
        self.assertEqual(result["failed"], 0)
        self.assertEqual(result["skipped"], 3)
        self.assertFalse((self.root / ".hidden" / "Never convert-2048").exists())
        self.assertFalse((self.output / "Nested source-2048").exists())
        self.assertFalse((self.root / "optimized-2048").exists())

    def test_batch_invalid_conversion_markers_are_left_unchanged_without_complete_claim(self):
        cases = ("[]", "not json", json.dumps({"schema": 1, "profile": converter.PROFILE,
                                              "complete": True, "output_gdi": {"file": "Missing.gdi"}}))
        for content in cases:
            with self.subTest(content=content):
                (self.source / "conversion.json").write_text(content)
                before = self.snapshot()
                messages = []
                result = converter.optimize_batch(self.root, messages.append)
                self.assertEqual(result["converted"], 0)
                self.assertEqual(result["skipped"], 1)
                self.assertIn("invalid/unrecognized", messages[0])
                self.assertIn("not verified", messages[0])
                self.assertEqual(self.snapshot(), before)
                self.assertFalse((self.root / "original-2048").exists())

    def test_batch_corrupted_generated_descriptor_is_not_recognized_as_complete(self):
        output = self.root / "original-2048"
        converter.optimize(self.gdi, output)
        (output / self.gdi.name).write_text("corrupted output")
        messages = []
        before = self.tree_hashes(self.root)
        result = converter.optimize_batch(self.root, messages.append)
        self.assertEqual(result["failed"], 1)
        self.assertEqual(result["skipped"], 1)
        self.assertTrue(any("invalid/unrecognized" in message for message in messages))
        self.assertEqual(self.tree_hashes(self.root), before)
        self.assertFalse((self.root / "original-2048-2048").exists())

    def test_batch_existing_case_folded_output_never_overwritten(self):
        output = self.root / "ORIGINAL-2048"
        output.mkdir()
        (output / "keep.txt").write_text("existing user data")
        result = converter.optimize_batch(self.root)
        self.assertEqual(result["converted"], 0)
        self.assertEqual(result["failed"], 1)
        self.assertFalse((self.root / "original-2048").exists())
        self.assertEqual((output / "keep.txt").read_text(), "existing user data")

    def test_batch_planned_case_folded_names_fail_both_and_continue(self):
        self.clone(self.root / "Game")
        self.clone(self.root / "game")
        result = converter.optimize_batch(self.root)
        self.assertEqual(result["converted"], 1)
        self.assertEqual(result["failed"], 2)
        self.assertFalse((self.root / "Game-2048").exists())
        self.assertFalse((self.root / "game-2048").exists())

    def test_batch_cancel_preserves_completed_games_and_cleans_current_staging(self):
        self.clone(self.root / "z-last")
        before = self.tree_hashes(self.root)

        def interrupted(message):
            if "z-last: Track 2/3" in message:
                raise KeyboardInterrupt

        result = converter.optimize_batch(self.root, interrupted)
        self.assertTrue(result["cancelled"])
        self.assertEqual(result["converted"], 1)
        self.assertTrue((self.root / "original-2048").is_dir())
        self.assertFalse((self.root / "z-last-2048").exists())
        self.assertEqual(list(self.root.rglob(".*.staging-*")), [])
        after = self.tree_hashes(self.root)
        for path, digest in before.items():
            self.assertEqual(after[path], digest)

    def test_batch_cli_summary_and_partial_failure_status(self):
        command = [sys.executable, str(ROOT / "tools/gdi_optimize.py"), "--batch", str(self.root)]
        run = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("1 converted, 0 skipped, 0 failed", run.stdout)
        repeat = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(repeat.returncode, 0, repeat.stderr)
        self.assertIn("0 converted, 2 skipped, 0 failed", repeat.stdout)
        bad = self.clone(self.root / "Bad")
        bad.write_text("invalid GDI")
        failure = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(failure.returncode, 1)
        self.assertIn("1 failed", failure.stdout)
        self.assertIn("Failed games/folders", failure.stderr)
        self.assertIn(str(bad), failure.stderr)
        self.assertIn("Invalid GDI track count", failure.stderr)

    def test_batch_json_report_lists_successes_and_failure_reasons(self):
        collection = self.root / "collection"
        good = self.clone(collection / "Good")
        bad = self.clone(collection / "Evolution 2")
        bad.write_text(bad.read_text().replace("high.bin", "missing.bin"))
        before = self.tree_hashes(collection)
        report_path = self.root / "batch-report.json"
        command = [sys.executable, str(ROOT / "tools/gdi_optimize.py"), "--batch", str(collection),
                   "--report", str(report_path)]
        run = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(run.returncode, 1, run.stderr)
        self.assertIn(f"Batch report: {report_path}", run.stdout)
        result = json.loads(report_path.read_text(encoding="utf-8"))
        self.assertEqual((result["converted"], result["failed"]), (1, 1))
        jobs = {entry["path"]: entry for entry in result["entries"]}
        self.assertEqual(jobs[str(good)]["status"], "converted")
        self.assertEqual(jobs[str(bad)]["status"], "failed")
        self.assertIn("Track 3: track file missing: missing.bin", jobs[str(bad)]["message"])
        self.assertFalse(Path(jobs[str(bad)]["output"]).exists())
        for path, digest in before.items():
            self.assertEqual(self.tree_hashes(collection)[path], digest)

    def test_batch_report_collision_and_inside_source_path_fail_before_conversion(self):
        collection = self.root / "collection"
        self.clone(collection / "Good")
        report_path = self.root / "existing-report.json"
        report_path.write_text("keep existing user data")
        tool = [sys.executable, str(ROOT / "tools/gdi_optimize.py"), "--batch", str(collection)]
        before = self.tree_hashes(collection)
        for path, reason in ((report_path, "Report already exists"),
                             (collection / "report.json", "outside the original collection")):
            with self.subTest(path=path):
                run = subprocess.run(tool + ["--report", str(path)], capture_output=True, text=True)
                self.assertEqual(run.returncode, 1)
                self.assertIn(reason, run.stderr)
                self.assertEqual(self.tree_hashes(collection), before)
                self.assertFalse((collection / "Good-2048").exists())
        self.assertEqual(report_path.read_text(), "keep existing user data")

    def test_report_publication_race_never_overwrites_existing_file(self):
        result = converter.optimize_batch(self.source)
        report_path = self.root / "report.json"
        publish = converter.publish_directory

        def intervening_file(stage, output):
            output.write_text("keep existing user data")
            publish(stage, output)

        with patch.object(converter, "publish_directory", intervening_file):
            with self.assertRaises(FileExistsError):
                converter.write_batch_report(result, report_path)
        self.assertEqual(report_path.read_text(), "keep existing user data")
        self.assertEqual(list(self.root.glob(".*.staging-*")), [])

    def test_cancel_report_keeps_completed_and_unattempted_jobs_distinct(self):
        collection = self.root / "collection"
        for name in ("a-first", "b-interrupted", "c-last"):
            self.clone(collection / name)
        before = self.tree_hashes(collection)

        def interrupted(message):
            if "b-interrupted: Track 2/3" in message:
                raise KeyboardInterrupt

        result = converter.optimize_batch(collection, interrupted)
        self.assertTrue(result["cancelled"])
        jobs = {Path(entry["path"]).parent.name: entry for entry in result["entries"]}
        self.assertEqual(jobs["a-first"]["status"], "converted")
        self.assertEqual(jobs["b-interrupted"]["status"], "cancelled")
        self.assertEqual(jobs["c-last"]["status"], "not-started")
        report_path = self.root / "cancelled-report.json"
        converter.write_batch_report(result, report_path)
        self.assertEqual(json.loads(report_path.read_text()), result)
        self.assertTrue((collection / "a-first-2048").is_dir())
        self.assertFalse((collection / "b-interrupted-2048").exists())
        self.assertFalse((collection / "c-last-2048").exists())
        self.assertEqual(list(collection.glob(".*.staging-*")), [])
        for path, digest in before.items():
            self.assertEqual(self.tree_hashes(collection)[path], digest)

    def test_batch_cli_invalid_invocations_fail_without_writes(self):
        tool = [sys.executable, str(ROOT / "tools/gdi_optimize.py")]
        for arguments in ([], [str(self.gdi)], ["--batch", str(self.root), str(self.gdi)],
                          ["--batch", str(self.gdi)], ["--batch", str(self.root / "absent")],
                          [str(self.gdi), str(self.output), "--report", str(self.root / "report.json")]):
            with self.subTest(arguments=arguments):
                run = subprocess.run(tool + arguments, capture_output=True, text=True)
                self.assertNotEqual(run.returncode, 0)
        self.assertFalse((self.root / "original-2048").exists())


if __name__ == "__main__":
    unittest.main()
