#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline CLI tests with synthetic filesystem/payload bytes only."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
WRAPPER = ROOT / "tools/vmu-image-subset.py"
HOST = Path(os.environ.get("KUI_VMU_SUBSET_HOST", ROOT / "build/vmu-subset-tool/vmu-image-subset")).resolve()
SPEC = importlib.util.spec_from_file_location("vmu_subset_tool", WRAPPER)
TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOOL)
SIZE = 131072


def directory_offset(slot):
    return (253 - slot // 16) * 512 + slot % 16 * 32


def fixture():
    image = bytearray(SIZE)
    for block in range(241):
        image[block * 512:(block + 1) * 512] = bytes((block * 19 + p * 7) & 255 for p in range(512))
    for block in range(256):
        value = 0xfffc if block < 241 else block - 1 if 242 <= block <= 253 else 0xfffa
        struct.pack_into("<H", image, 254 * 512 + block * 2, value)
    root = bytearray((p * 13 + 17) & 255 for p in range(512))
    root[:16] = b"\x55" * 16
    root[16] = 1
    for off, value in ((0x46, 254), (0x48, 1), (0x4a, 253), (0x4c, 13), (0x4e, 123), (0x50, 200)):
        struct.pack_into("<H", root, off, value)
    image[255 * 512:] = root
    records = {
        0: (b"SETUP.CFG\0\0\0", [199, 50], 0x33, 1),
        16: (b"CHAPTER.001\0", [10, 80, 3], 0x33, 0),
        207: (b"OTHER-NAME\0\0", [18, 26], 0x33, 1),
        15: (b"CHAPTER.002 ", [33], 0x33, 0),
        1: (b"MINI-GAME\0\0\0", [0, 1, 2, 3, 4], 0xcc, 3),
    }
    # Keep allocated chains distinct, including the contiguous mini-game.
    records[16] = (records[16][0], [10, 80, 9], 0x33, 0)
    for slot, (name, chain, kind, header) in records.items():
        assert len(name) == 12
        entry = bytearray(32)
        entry[0:2] = bytes((kind, 0xff))
        struct.pack_into("<H", entry, 2, chain[0])
        entry[4:16] = name
        entry[16:24] = bytes((slot + i * 23) & 255 for i in range(8))
        struct.pack_into("<HH", entry, 24, len(chain), header)
        off = directory_offset(slot)
        image[off:off + 32] = entry
        for n, block in enumerate(chain):
            struct.pack_into("<H", image, 254 * 512 + block * 2, chain[n + 1] if n + 1 < len(chain) else 0xfffa)
            image[block * 512:(block + 1) * 512] = bytes((slot * 29 + n * 17 + p) & 255 for p in range(512))
    # Inactive directory bytes are legal stale data; their trailer stays zero.
    off = directory_offset(206)
    image[off + 1:off + 28] = b"\xa7" * 27
    return bytes(image), records


def oracle(source, records, slots):
    result = bytearray(SIZE)
    result[254 * 512:] = source[254 * 512:]
    for block in range(200):
        struct.pack_into("<H", result, 254 * 512 + block * 2, 0xfffc)
    for slot in slots:
        off = directory_offset(slot)
        result[off:off + 32] = source[off:off + 32]
        for block in records[slot][1]:
            result[block * 512:(block + 1) * 512] = source[block * 512:(block + 1) * 512]
            fat = 254 * 512 + block * 2
            result[fat:fat + 2] = source[fat:fat + 2]
    return bytes(result)


class SubsetToolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not HOST.is_file():
            raise RuntimeError("Build the host adapter with make -f Makefile.vmu-subset-tool first.")

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="kui-subset-tool-test-")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.source = self.base / "source card.vmd"
        self.output = self.base / "fresh card.image"
        self.original, self.records = fixture()
        self.source.write_bytes(self.original)
        self.source.chmod(0o400)

    def run_tool(self, *arguments, host=HOST, environment=None):
        return subprocess.run([sys.executable, str(WRAPPER), "--host-tool", str(host), *map(str, arguments)],
                              capture_output=True, text=True, cwd=ROOT, env=environment, timeout=10, check=False)

    def create(self, *selectors, host=HOST, environment=None):
        return self.run_tool(self.source, self.output, *selectors, host=host, environment=environment)

    def unchanged(self):
        self.assertEqual(self.source.read_bytes(), self.original)

    def test_validated_inventory(self):
        result = self.run_tool(self.source, "--list")
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report["summary"], {"files": 5, "game_files": 1, "used_blocks": 13, "free_blocks": 187})
        self.assertEqual(report["source_sha256"], hashlib.sha256(self.original).hexdigest())
        self.assertEqual([e["slot"] for e in report["entries"]], [0, 1, 15, 16, 207])
        self.assertEqual(next(e for e in report["entries"] if e["slot"] == 15)["name_hex"], self.records[15][0].hex())
        self.assertFalse(self.output.exists())
        self.unchanged()

    def test_explicit_related_group_and_proof(self):
        result = self.create("--slot", 207, "--slot", 0, "--slot", 16)
        self.assertEqual(result.returncode, 0, result.stderr)
        derived = self.output.read_bytes()
        self.assertEqual(derived, oracle(self.original, self.records, [207, 0, 16]))
        report = json.loads(result.stdout)
        self.assertFalse(report["loader_pair_created"])
        self.assertEqual(report["output_sha256"], hashlib.sha256(derived).hexdigest())
        self.assertEqual(report["output_summary"], {"files": 3, "game_files": 0, "used_blocks": 7, "free_blocks": 193})
        for item in report["selection"]:
            slot = item["slot"]
            payload = b"".join(self.original[b * 512:(b + 1) * 512] for b in self.records[slot][1])
            self.assertEqual(item["allocated_payload_sha256"], hashlib.sha256(payload).hexdigest())
            off = directory_offset(slot)
            self.assertEqual(item["directory_hex"], self.original[off:off + 32].hex())
        self.assertEqual(self.output.stat().st_mode & 0o777, 0o600)
        self.unchanged()

    def test_exact_names_and_no_prefix_inference(self):
        result = self.create("--name", "SETUP.CFG", "--name", "CHAPTER.001", "--name", "OTHER-NAME")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.output.read_bytes(), oracle(self.original, self.records, [0, 16, 207]))
        self.unchanged()

    def test_raw_space_padded_name(self):
        result = self.create("--name-hex", self.records[15][0].hex())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.output.read_bytes(), oracle(self.original, self.records, [15]))
        self.output.unlink()
        result = self.create("--name", "CHAPTER.002")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.output.exists())

    def test_mini_game_prefix_and_header_offset(self):
        result = self.create("--slot", 1, "--slot", 16)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.output.read_bytes(), oracle(self.original, self.records, [1, 16]))
        self.assertEqual(json.loads(result.stdout)["output_summary"]["game_files"], 1)
        self.unchanged()

    def test_invalid_selections_leave_no_output(self):
        cases = [
            ("--slot", 208), ("--slot", -1), ("--slot", 2),
            ("--slot", 0, "--slot", 0), ("--slot", 0, "--slot", 2),
            ("--name", "CHAPTER"), ("--name", "setup.cfg"), ("--name", ""),
            ("--name", "x" * 13), ("--name", "café"), ("--name", "bad\nname"),
            ("--name-hex", "00"), ("--name-hex", "zz" * 12), ("--name-hex", "ab" * 12),
            ("--name", "SETUP.CFG", "--name", "SETUP.CFG"),
            ("--slot", 0, "--name", "SETUP.CFG"), (),
        ]
        for selectors in cases:
            with self.subTest(selectors=selectors):
                result = self.create(*selectors)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(self.output.exists())
                self.unchanged()

    def test_invalid_source_lengths(self):
        for length in (0, SIZE - 1, SIZE + 1):
            with self.subTest(length=length):
                bad = self.base / f"length-{length}.image"
                contents = (self.original + b"x")[:length]
                bad.write_bytes(contents)
                result = self.run_tool(bad, self.output, "--slot", 0)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(self.output.exists())
                self.assertEqual(bad.read_bytes(), contents)

    def test_whole_source_admission(self):
        for mutation in ("geometry", "unselected-cycle", "orphan", "unknown-type"):
            with self.subTest(mutation=mutation):
                contents = bytearray(self.original)
                if mutation == "geometry":
                    contents[255 * 512] = 0
                elif mutation == "unselected-cycle":
                    struct.pack_into("<H", contents, 254 * 512 + 33 * 2, 33)
                elif mutation == "orphan":
                    struct.pack_into("<H", contents, 254 * 512 + 42 * 2, 0xfffa)
                else:
                    contents[directory_offset(206)] = 0x34
                bad = self.base / f"{mutation}.image"
                bad.write_bytes(contents)
                result = self.run_tool(bad, self.output, "--slot", 0)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("admission failed", result.stderr)
                self.assertFalse(self.output.exists())
                self.assertEqual(bad.read_bytes(), contents)
        # Entry bytes 0x1c-0x1f are opaque, even in a free slot.
        contents = bytearray(self.original)
        contents[directory_offset(206) + 31] = 1
        odd = self.base / "inactive-trailer.image"
        odd.write_bytes(contents)
        result = self.run_tool(odd, self.output, "--slot", 0)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(self.output.exists())
        self.output.unlink()

    @unittest.skipUnless(hasattr(os, "mkfifo"), "requires FIFO support")
    def test_nonregular_source_rejected_without_blocking(self):
        fifo = self.base / "fifo"
        os.mkfifo(fifo)
        result = self.run_tool(fifo, self.output, "--slot", 0)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("regular file", result.stderr)
        self.assertFalse(self.output.exists())

    def test_existing_output_and_aliases(self):
        self.output.write_bytes(b"existing output")
        result = self.create("--slot", 0)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.output.read_bytes(), b"existing output")
        self.output.unlink()
        os.link(self.source, self.output)
        result = self.create("--slot", 0)
        self.assertNotEqual(result.returncode, 0)
        self.output.unlink()
        for target in (self.source, self.base / "absent"):
            self.output.symlink_to(target)
            result = self.create("--slot", 0)
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(self.output.is_symlink())
            self.output.unlink()
        result = self.run_tool(self.source, self.source, "--slot", 0)
        self.assertNotEqual(result.returncode, 0)
        self.unchanged()

    def test_missing_adapter(self):
        result = self.create("--slot", 0, host=self.base / "missing-host")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Build once", result.stderr)
        self.assertFalse(self.output.exists())

    def fake_host(self, body):
        path = self.base / "fake host"
        path.write_text(f"#!{sys.executable}\n" + body)
        path.chmod(0o700)
        return path

    def test_malformed_or_failed_adapter(self):
        malformed = {"protocol": "kui-vmu-subset-v1", "entries": []}
        invalid_count = {"protocol": "kui-vmu-subset-v1", "entries": [],
                         "summary": {"files": False, "game_files": 0, "used_blocks": 0, "free_blocks": 200}}
        for body in ("print('not JSON')\n", "print('{}')\n", f"print({json.dumps(malformed)!r})\n",
                     f"print({json.dumps(invalid_count)!r})\n", "import sys\nsys.stderr.write('host failure')\nsys.exit(7)\n"):
            with self.subTest(body=body):
                host = self.fake_host(body)
                result = self.create("--slot", 0, host=host)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("Traceback", result.stderr)
                self.assertFalse(self.output.exists())

    def test_short_adapter_output(self):
        host = self.fake_host("import subprocess, sys\n"
                              "if sys.argv[1] == '--inspect':\n"
                              f"    sys.exit(subprocess.run([{str(HOST)!r}, *sys.argv[1:]]).returncode)\n"
                              "sys.stdout.buffer.write(b'short')\n")
        result = self.create("--slot", 0, host=host)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("output length/root", result.stderr)
        self.assertFalse(self.output.exists())

    def test_output_creation_race_never_overwrites(self):
        host = self.fake_host("import pathlib, subprocess, sys\n"
                              f"result = subprocess.run([{str(HOST)!r}, *sys.argv[1:]], capture_output=True)\n"
                              "if sys.argv[1] == '--construct' and result.returncode == 0:\n"
                              f"    pathlib.Path({str(self.output)!r}).write_bytes(b'racing owner')\n"
                              "sys.stdout.buffer.write(result.stdout)\n"
                              "sys.stderr.buffer.write(result.stderr)\n"
                              "sys.exit(result.returncode)\n")
        result = self.create("--slot", 0, host=host)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.output.read_bytes(), b"racing owner")
        self.unchanged()

    def test_inspection_and_construction_use_one_source_snapshot(self):
        # A concurrent external edit after inspection must not change which
        # bytes the constructor sees. The real adapter never edits the input.
        host = self.fake_host("import pathlib, subprocess, sys\n"
                              f"result = subprocess.run([{str(HOST)!r}, *sys.argv[1:]], capture_output=True)\n"
                              "if sys.argv[1] == '--inspect' and result.returncode == 0:\n"
                              f"    original = pathlib.Path({str(self.source)!r})\n"
                              "    original.chmod(0o600)\n"
                              "    original.write_bytes(b'changed by another process')\n"
                              "sys.stdout.buffer.write(result.stdout)\n"
                              "sys.stderr.buffer.write(result.stderr)\n"
                              "sys.exit(result.returncode)\n")
        result = self.create("--slot", 0, host=host)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.output.read_bytes(), oracle(self.original, self.records, [0]))
        self.assertEqual(json.loads(result.stdout)["source_sha256"], hashlib.sha256(self.original).hexdigest())

    def test_running_needs_no_compiler_or_path_commands(self):
        environment = dict(os.environ, PATH="")
        result = self.create("--slot", 0, environment=environment)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.output.read_bytes(), oracle(self.original, self.records, [0]))

    def test_failed_write_removes_only_new_output(self):
        with mock.patch.object(TOOL.os, "fsync", side_effect=OSError("injected sync failure")):
            with self.assertRaisesRegex(OSError, "injected sync failure"):
                TOOL.write_new(self.output, b"new bytes")
        self.assertFalse(self.output.exists())
        self.output.write_bytes(b"keep this")
        with self.assertRaises(FileExistsError):
            TOOL.write_new(self.output, b"replacement")
        self.assertEqual(self.output.read_bytes(), b"keep this")


if __name__ == "__main__":
    unittest.main(verbosity=2)
