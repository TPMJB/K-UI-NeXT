#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generated sparse raw images test the bounded driver export without game data."""
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
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location("toy_driver_extractor", ROOT / "tools/extract_cdda_toy_driver.py")
extractor = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = extractor
SPEC.loader.exec_module(extractor)


def both(number, width):
    return number.to_bytes(width, "little") + number.to_bytes(width, "big")


def record(name, lba, size, flags=0):
    name = bytes(name)
    length = 33 + len(name) + (0 if len(name) % 2 else 1)
    result = bytearray(length)
    result[0] = length
    result[2:10] = both(lba, 4)
    result[10:18] = both(size, 4)
    result[25] = flags
    result[28:32] = both(1, 2)
    result[32] = len(name)
    result[33:33 + len(name)] = name
    return bytes(result)


def directory(lba, parent_lba, entries, size=2048, parent_size=2048):
    data = (record(b"\x00", lba, size, 2) +
            record(b"\x01", parent_lba, parent_size, 2) + b"".join(entries))
    return data + bytes(size - len(data))


def address(lba):
    fad = lba + 150
    parts = fad // 4500, fad // 75 % 60, fad % 75
    return bytes((((p // 10) * 16 + p % 10) & 255) for p in parts)


class ToyDriverExtractTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="toy-driver-extract-")
        self.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name)
        self.source = self.base / "original image"
        self.source.mkdir()
        (self.source / "data files").mkdir()
        self.gdi = self.source / "TOY_COMMANDER.gdi"
        self.output = self.base / "ToyCommander-AUDIO64.DRV"
        self.backings = {}
        rows = ["15"]
        lbas = [0, 7257, 45000, 201900, 219712, 234091, 255599,
                272017, 282799, 303092, 323352, 341314, 356690, 374201, 377422]
        for number, lba in enumerate(lbas, 1):
            if number in (1, 3, 15):
                name = f"data files/track {number:02}.bin"
                kind = 4
                self.backings[number] = self.source / name
            else:
                name, kind = f"unused track {number}.raw", 0
            rows.append(f'{number} {lba} {kind} 2352 "{name}" 0')
        self.descriptor = ("\n".join(rows) + "\n").encode()
        self.gdi.write_bytes(self.descriptor)
        for geometry in extractor.TOY_PROFILE.data_tracks:
            with self.backings[geometry.number].open("wb") as handle:
                handle.truncate(geometry.size)
        self.boot = bytes((i * 73 + i // 101 + 19) & 255 for i in range(748444))
        self.ip = bytes((i * 29 + i // 37) & 255 for i in range(32768))
        self.driver = bytes((i * 17 + i // 79 + 13) & 255 for i in range(65573))
        boot_profile = replace(extractor.TOY_PROFILE.boot,
                               descriptor_bytes=len(self.descriptor),
                               descriptor_sha256=hashlib.sha256(self.descriptor).hexdigest(),
                               boot_sha256=hashlib.sha256(self.boot).hexdigest(),
                               boot_crc32=zlib.crc32(self.boot) & 0xFFFFFFFF)
        self.profile = replace(extractor.TOY_PROFILE, boot=boot_profile,
                               ip_sha256=hashlib.sha256(self.ip).hexdigest())
        self.write_payload(45000, self.ip)
        self.write_payload(548634, self.boot)
        self.driver_lba = 548000
        self.write_payload(self.driver_lba, self.driver)
        pvd = bytearray(2048)
        pvd[:7] = b"\x01CD001\x01"
        pvd[80:88] = both(504150, 4)
        pvd[120:124] = both(1, 2)
        pvd[124:128] = both(1, 2)
        pvd[128:132] = both(2048, 2)
        pvd[156:190] = record(b"\x00", 45020, 2048, 2)
        pvd[881] = 1
        self.pvd = bytes(pvd)
        self.write_payload(45016, self.pvd)
        self.root_entries = [record(b"1GUTH.BIN;1", 548634, len(self.boot)),
                             record(b"SOUND", 45021, 2048, 2)]
        self.sound_entries = [record(b"audio64.drv;1", self.driver_lba, len(self.driver))]
        self.write_directories()

    def geometry_at(self, lba):
        return next(g for g in self.profile.data_tracks
                    if g.lba <= lba < g.lba + g.size // 2352)

    def write_payload(self, lba, data):
        for sector in range((len(data) + 2047) // 2048):
            current = lba + sector
            geometry = self.geometry_at(current)
            raw = bytearray(b"\xA6" * 2352)
            raw[:12] = b"\x00" + b"\xFF" * 10 + b"\x00"
            raw[12:15], raw[15] = address(current), 1
            piece = data[sector * 2048:(sector + 1) * 2048]
            raw[16:16 + len(piece)] = piece
            with self.backings[geometry.number].open("r+b") as handle:
                handle.seek((current - geometry.lba) * 2352)
                handle.write(raw)

    def write_directories(self):
        self.write_payload(45020, directory(45020, 45020, self.root_entries))
        self.write_payload(45021, directory(45021, 45020, self.sound_entries))

    def extract(self, output=None, profile=None):
        return extractor._extract_driver(self.gdi, output or self.output, profile or self.profile)

    def refused(self, message, profile=None):
        with self.assertRaisesRegex(extractor.ExtractionError, message):
            self.extract(profile=profile)
        self.assertFalse(os.path.lexists(self.output))
        self.assertFalse(list(self.base.glob(".toy-boot-*.tmp")))

    def change_raw(self, lba, offset, value):
        geometry = self.geometry_at(lba)
        with self.backings[geometry.number].open("r+b") as handle:
            handle.seek((lba - geometry.lba) * 2352 + offset)
            handle.write(bytes((value,)))

    def test_sound_directory_exact_payload_and_read_only_sources(self):
        before = {number: (path.stat().st_size, path.stat().st_mtime_ns)
                  for number, path in self.backings.items()}
        output, iso_path, lba, size, sha = self.extract()
        self.assertEqual((output, iso_path, lba, size),
                         (self.output, "SOUND/audio64.drv;1", self.driver_lba, 65573))
        self.assertEqual(sha, hashlib.sha256(self.driver).hexdigest())
        self.assertEqual(self.output.read_bytes(), self.driver)
        self.assertEqual(self.gdi.read_bytes(), self.descriptor)
        self.assertEqual(before, {number: (path.stat().st_size, path.stat().st_mtime_ns)
                                 for number, path in self.backings.items()})

    def test_root_case_insensitive_name_and_larger_version(self):
        self.root_entries.append(record(b"AuDiO64.DrV;12", self.driver_lba, len(self.driver)))
        self.sound_entries = []
        self.write_directories()
        result = self.extract()
        self.assertEqual(result[1], "AuDiO64.DrV;12")
        self.assertEqual(self.output.read_bytes(), self.driver)

    def test_data_track_one_is_mapped_if_iso_extent_uses_it(self):
        self.driver_lba = 1000
        self.write_payload(self.driver_lba, self.driver)
        self.sound_entries = [record(b"AUDIO64.DRV", self.driver_lba, len(self.driver))]
        self.write_directories()
        self.extract()
        self.assertEqual(self.output.read_bytes(), self.driver)

    def test_descriptor_relative_paths_ignore_current_directory(self):
        old = Path.cwd()
        try:
            os.chdir(self.base)
            self.extract()
        finally:
            os.chdir(old)
        self.assertEqual(self.output.read_bytes(), self.driver)

    def test_production_cli_refuses_generated_identity(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/extract_cdda_toy_driver.py"),
                                 str(self.gdi), str(self.output)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("preflight identity", result.stderr)
        self.assertFalse(self.output.exists())

    def test_descriptor_trailing_bytes_and_bad_geometry(self):
        self.gdi.write_bytes(self.descriptor + b"\n")
        self.refused("preflight identity")
        self.gdi.write_bytes(self.descriptor)
        geometry = replace(self.profile.data_tracks[1], lba=45001)
        changed = replace(self.profile, data_tracks=(self.profile.data_tracks[0], geometry,
                                                      self.profile.data_tracks[2]))
        self.refused("verified geometry", changed)

    def test_absolute_parent_and_nonzero_offset_backings_are_refused(self):
        for old, new in ((b'"data files/track 03.bin"', b'"../track03.bin"'),
                         (b'"data files/track 03.bin"', b'"/tmp/track03.bin"'),
                         (b'"data files/track 03.bin" 0', b'"data files/track 03.bin" 1')):
            with self.subTest(new=new):
                descriptor = self.descriptor.replace(old, new)
                self.gdi.write_bytes(descriptor)
                profile = replace(self.profile, boot=replace(self.profile.boot,
                    descriptor_bytes=len(descriptor),
                    descriptor_sha256=hashlib.sha256(descriptor).hexdigest()))
                self.refused("Invalid original GDI|verified geometry", profile)

    def test_ip_and_boot_hashes_checked_before_iso_walk(self):
        with mock.patch.object(extractor, "_find_driver") as walk:
            self.refused("IP payload", replace(self.profile, ip_sha256="0" * 64))
            walk.assert_not_called()
        profile = replace(self.profile, boot=replace(self.profile.boot, boot_crc32=0))
        with mock.patch.object(extractor, "_find_driver") as walk:
            self.refused("Boot payload", profile)
            walk.assert_not_called()

    def test_backing_sizes_are_verified_before_sector_reads(self):
        with self.backings[3].open("r+b") as handle:
            handle.truncate(2352)
        self.refused("backing size")

    def test_raw_header_sync_mode_and_address_are_validated(self):
        for offset, value, text in ((1, 0, "sync"), (15, 2, "Mode 1"), (14, 255, "address")):
            with self.subTest(offset=offset):
                self.change_raw(self.driver_lba, offset, value)
                self.refused(text)
                self.write_payload(self.driver_lba, self.driver[:2048])

    def test_short_raw_read_is_refused(self):
        real_open = Path.open

        class ShortReader:
            def __init__(self, handle): self.handle = handle
            def __enter__(self): return self
            def __exit__(self, *args): self.handle.close()
            def fileno(self): return self.handle.fileno()
            def seek(self, offset): return self.handle.seek(offset)
            def read(self, count): return self.handle.read(count)[:-1]

        def opened(path, *args, **kwargs):
            handle = real_open(path, *args, **kwargs)
            return ShortReader(handle) if path == self.backings[3] else handle

        with mock.patch.object(Path, "open", new=opened):
            self.refused("Truncated raw sector")

    def test_only_bounded_metadata_identity_and_target_sectors_are_read(self):
        sectors = []
        real_sector = extractor.Image.sector

        def watched(image, lba):
            sectors.append(lba)
            return real_sector(image, lba)

        with mock.patch.object(extractor.Image, "sector", new=watched):
            self.extract()
        self.assertEqual(sectors, list(range(45000, 45016)) + list(range(548634, 549000)) +
                         [45016, 45020, 45021] + list(range(548000, 548033)))

    def test_multiple_matches_across_directories_are_refused(self):
        self.root_entries.append(record(b"AUDIO64.DRV;2", self.driver_lba, len(self.driver)))
        self.write_directories()
        self.refused("Multiple AUDIO64")

    def test_missing_driver_does_not_export_anything(self):
        self.sound_entries = [record(b"OTHER.DRV;1", self.driver_lba, len(self.driver))]
        self.write_directories()
        self.refused("not found")

    def test_multi_extent_interleave_and_extended_attributes_are_refused(self):
        for field, value in ((1, 1), (25, 0x80), (26, 1), (27, 1)):
            with self.subTest(field=field):
                entry = bytearray(record(b"AUDIO64.DRV;1", self.driver_lba, len(self.driver)))
                entry[field] = value
                self.sound_entries = [bytes(entry)]
                self.write_directories()
                self.refused("extended, interleaved or multi-extent")

    def test_driver_size_zero_and_over_limit_are_refused(self):
        for size in (0, 1048577):
            with self.subTest(size=size):
                self.sound_entries = [record(b"AUDIO64.DRV;1", self.driver_lba, size)]
                self.write_directories()
                self.refused("file size")

    def test_extents_in_audio_gaps_or_past_end_are_refused(self):
        for lba, size in ((7107, 1), (201750, 2048), (549149, 4096)):
            with self.subTest(lba=lba):
                self.sound_entries = [record(b"AUDIO64.DRV;1", lba, size)]
                self.write_directories()
                self.refused("outside verified data tracks")

    def test_directory_cycle_and_alias_are_refused(self):
        self.sound_entries = [record(b"LOOP", 45020, 2048, 2)]
        self.write_directories()
        self.refused("alias or cycle")

    def test_record_crossing_sector_and_nonzero_padding_are_refused(self):
        payload = bytearray(directory(45021, 45020, self.sound_entries))
        payload[68] = 255
        self.write_payload(45021, payload)
        self.refused("record length")
        payload = bytearray(directory(45021, 45020, self.sound_entries))
        payload[-1] = 1
        self.write_payload(45021, payload)
        self.refused("sector padding")

    def test_iso_endian_copies_and_volume_geometry_are_validated(self):
        for offset, value, text in ((84, 1, "byte-order"), (129, 4, "byte-order"),
                                    (0, 4, "descriptor type"), (881, 0, "primary")):
            with self.subTest(offset=offset):
                payload = bytearray(self.pvd)
                payload[offset] = value
                self.write_payload(45016, payload)
                self.refused(text)

    def test_boot_record_before_primary_uses_absolute_directory_extents(self):
        descriptor = bytearray(2048)
        descriptor[:7] = b"\x00CD001\x01"
        self.write_payload(45016, descriptor)
        self.write_payload(45017, self.pvd)
        sectors = []
        real_sector = extractor.Image.sector

        def watched(image, lba):
            sectors.append(lba)
            return real_sector(image, lba)

        with mock.patch.object(extractor.Image, "sector", new=watched):
            self.extract()
        self.assertEqual(self.output.read_bytes(), self.driver)
        self.assertEqual(sectors[382:386], [45016, 45017, 45020, 45021])

    def test_terminator_before_primary_and_descriptor_scan_limit_are_refused(self):
        descriptor = bytearray(2048)
        descriptor[:7] = b"\xffCD001\x01"
        self.write_payload(45016, descriptor)
        self.refused("terminator precedes primary")
        descriptor[0] = 0
        for lba in range(45016, 45032):
            self.write_payload(lba, descriptor)
        self.refused("scan limit")

    def test_directory_record_endian_sequence_and_name_padding_are_validated(self):
        for changes, text in ((((9, 0),), "byte-order"),
                              (((28, 2), (31, 2)), "volume sequence"),
                              (((47, 1),), "padding")):
            with self.subTest(changes=changes):
                entry = bytearray(record(b"AUDIO64.DRV;12", self.driver_lba, len(self.driver)))
                for field, value in changes:
                    entry[field] = value
                self.sound_entries = [bytes(entry)]
                self.write_directories()
                self.refused(text)

    def test_directory_self_and_parent_records_are_required(self):
        payload = bytearray(directory(45021, 45020, self.sound_entries))
        payload[33] = 1
        self.write_payload(45021, payload)
        self.refused("self or parent")

    def test_identifier_paths_and_invalid_version_are_refused(self):
        for name in (b"../AUDIO64.DRV;1", b"AUDIO64.DRV;0", b"AUDIO64.DRV;1;2", b"\xff"):
            with self.subTest(name=name):
                self.sound_entries = [record(name, self.driver_lba, len(self.driver))]
                self.write_directories()
                self.refused("identifier|suffix|ASCII")

    def test_bounded_directory_count_records_and_reads(self):
        for limit, value, text in (("MAX_DIRECTORIES", 1, "count limit"),
                                    ("MAX_RECORDS", 5, "record limit"),
                                    ("MAX_DIRECTORY_READ_BYTES", 2048, "read limit")):
            with self.subTest(limit=limit):
                with mock.patch.object(extractor, limit, value):
                    self.refused(text)

    def test_exact_directory_and_driver_limits_are_accepted(self):
        with mock.patch.multiple(extractor, MAX_DIRECTORIES=2, MAX_RECORDS=7,
                                 MAX_DIRECTORY_READ_BYTES=4096,
                                 MAX_DRIVER_BYTES=len(self.driver)):
            self.extract()
        self.assertEqual(self.output.read_bytes(), self.driver)

    def test_existing_output_and_inputs_are_not_overwritten(self):
        self.output.write_bytes(b"existing output")
        with self.assertRaisesRegex(extractor.ExtractionError, "already exists"):
            self.extract()
        self.assertEqual(self.output.read_bytes(), b"existing output")
        self.output.unlink()
        with self.assertRaisesRegex(extractor.ExtractionError, "already exists"):
            self.extract(output=self.gdi)
        unused = self.source / "unused track 2.raw"
        with self.assertRaisesRegex(extractor.ExtractionError, "game backing path"):
            self.extract(output=unused)
        self.assertFalse(unused.exists())

    def test_dangling_destination_symlink_is_not_followed(self):
        target = self.base / "missing"
        self.output.symlink_to(target)
        with self.assertRaisesRegex(extractor.ExtractionError, "already exists"):
            self.extract()
        self.assertTrue(self.output.is_symlink())
        self.assertFalse(target.exists())

    def test_backing_symlink_cannot_escape_image_directory(self):
        external = self.base / "outside.bin"
        self.backings[1].rename(external)
        self.backings[1].symlink_to(external)
        self.refused("escapes")

    def test_publication_race_cannot_replace_existing_file(self):
        real_link = os.link

        def competing(source, destination):
            Path(destination).write_bytes(b"concurrent output")
            return real_link(source, destination)

        with mock.patch.object(extractor.boot.os, "link", side_effect=competing):
            with self.assertRaisesRegex(extractor.ExtractionError, "already exists"):
                self.extract()
        self.assertEqual(self.output.read_bytes(), b"concurrent output")
        self.assertFalse(list(self.base.glob(".toy-boot-*.tmp")))

    def test_unsupported_links_explain_home_folder_and_leave_no_partial_output(self):
        with mock.patch.object(extractor.boot.os, "link", side_effect=PermissionError("not permitted")):
            self.refused("computer home folder")


if __name__ == "__main__":
    unittest.main()
