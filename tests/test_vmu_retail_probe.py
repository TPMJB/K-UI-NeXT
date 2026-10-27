#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Safety and SH4 candidate-finding checks for the read-only retail probe."""
import copy
import hashlib
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("vmu_retail_probe", ROOT / "tools/vmu_retail_probe.py")
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)
BASE = 0x8C010000


def image(words, literal=0xA05F6C18):
    data = bytearray(64)
    for index, word in enumerate(words):
        struct.pack_into("<H", data, index * 2, word)
    struct.pack_into("<I", data, 32, literal)
    return bytes(data)


class ProbeTests(unittest.TestCase):
    def test_exact_pc_load_and_dma_store(self):
        # MOV.L @(28,PC),R2; MOV #1,R3; MOV.L R3,@R2; RTS; NOP.
        data = image([0xD207, 0xE301, 0x2232, 0x000B, 0x0009])
        report = probe.analyze(data)
        self.assertFalse(report["retail_admitted"])
        self.assertFalse(report["patches_generated"])
        self.assertEqual(report["reference_count"], 1)
        access = report["references"][0]["mmio_accesses"][0]
        self.assertEqual(access, {"pc": "0x8c010004", "access": "write", "width": 4,
                                  "address": "0xa05f6c18", "register": "MAPLE_STATE",
                                  "constant_value": "0x00000001"})

    def test_physical_aliases_and_constructed_offset(self):
        for literal in (0x005F6C00, 0x805F6C00, 0xA05F6C00):
            data = image([0xD207, 0x7218, 0xE301, 0x2232, 0x000B, 0x0009], literal)
            accesses = probe.analyze(data)["references"][0]["mmio_accesses"]
            self.assertEqual(accesses[0]["register"], "MAPLE_STATE")
        self.assertIsNone(probe.maple_register(0xE05F6C18))

    def test_unknown_instruction_drops_unproven_address(self):
        data = image([0xD207, 0xFFFF, 0xE301, 0x2232, 0x000B, 0x0009])
        result = probe.analyze(data)["references"][0]
        self.assertEqual(result["mmio_accesses"], [])
        self.assertEqual(result["constant_knowledge_discarded_at"], ["0x8c010002"])

    def test_stops_after_exactly_one_delay_slot(self):
        data = image([0xD207, 0xE301, 0x000B, 0x2232, 0x2232])
        result = probe.analyze(data)["references"][0]
        self.assertEqual(len(result["mmio_accesses"]), 1)
        self.assertEqual(result["instructions"][-1]["pc"], "0x8c010006")

    def test_conditional_branch_does_not_assume_taken_path(self):
        data = image([0xD207, 0x8900, 0xE301, 0x2232])
        result = probe.analyze(data)["references"][0]
        self.assertEqual(result["mmio_accesses"], [])
        self.assertEqual(len(result["instructions"]), 2)

    def test_aligned_pc_relative_base_at_halfword_offset(self):
        data = bytearray(image([0x0009, 0xD207, 0xE301, 0x2232, 0x000B, 0x0009]))
        result = probe.analyze(bytes(data))["references"][0]
        self.assertEqual(result["pc"], "0x8c010002")
        self.assertEqual(result["literal"]["address"], "0x8c010020")
        self.assertEqual(len(result["mmio_accesses"]), 1)

    def test_out_of_file_pc_literal_is_ignored(self):
        report = probe.analyze(image([0xD2FF, 0x2232]))
        self.assertEqual(report["reference_count"], 0)

    def test_file_identity_never_proves_runtime_lease(self):
        data = image([0xD207, 0x000B, 0x0009])
        profile = copy.deepcopy(probe.TOY_HEAP_PROFILE)
        profile.update(bytes=len(data), crc32=zlib.crc32(data), sha256=hashlib.sha256(data).hexdigest(),
                       witnesses=((BASE, 2, 0xD207, "synthetic startup witness"),))
        result = probe.heap_evidence(data, BASE, profile)
        self.assertTrue(result["full_identity_matches"])
        self.assertTrue(result["all_startup_witnesses_match"])
        self.assertFalse(result["runtime_lease_proven"])
        altered = bytearray(data)
        altered[-1] ^= 1
        result = probe.heap_evidence(bytes(altered), BASE, profile)
        self.assertFalse(result["full_identity_matches"])
        self.assertTrue(result["all_startup_witnesses_match"])

    def test_rejects_invalid_size_range_alignment_and_window(self):
        for data, address, window in ((b"", BASE, 24), (b"abc", BASE, 24),
                                      (b"abcd", BASE + 1, 24), (b"abcd", 0x8C000000, 24),
                                      (b"abcdefgh", 0x8CFFFFFC, 24),
                                      (b"abcd", 0xEC010000, 24), (b"abcd", BASE, 0),
                                      (b"abcd", BASE, 65)):
            with self.subTest(data=data, address=address, window=window):
                with self.assertRaises(ValueError):
                    probe.analyze(data, address, window)
        with self.assertRaises(ValueError):
            probe.analyze(b"abcd", ip=b"IP")

    def test_cli_reports_to_stdout_and_preserves_owner_input(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "1ST_READ.BIN"
            data = image([0xD207, 0xE301, 0x2232, 0x000B, 0x0009])
            path.write_bytes(data)
            result = subprocess.run([sys.executable, str(ROOT / "tools/vmu_retail_probe.py"), str(path)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('"retail_admitted": false', result.stdout)
            self.assertEqual(path.read_bytes(), data)
            path.write_bytes(b"invalid")
            result = subprocess.run([sys.executable, str(ROOT / "tools/vmu_retail_probe.py"), str(path)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, "")
            self.assertIn("refused", result.stderr)


if __name__ == "__main__":
    unittest.main()
