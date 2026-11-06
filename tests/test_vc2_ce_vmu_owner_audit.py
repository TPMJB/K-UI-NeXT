#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check exact-owner metadata gates without executing or distributing game code.

Run with --kernel OWNER/VIRTCOP2/0WINCEOS.BIN for all source mutation tests.
The identity/CLI tests also run under ordinary test discovery without that file.
"""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import vc2_ce_vmu_owner_audit as owner
from armada_ce_rom_audit import ROMError

OWNER = os.environ.get("KUI_VC2_OWNER_KERNEL")


class PublicGateTests(unittest.TestCase):
    def test_identity_precedes_table_parsing(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "synthetic.bin"
            for data, message in ((b"authored non-ROM", "Exact kernel size"),
                                  (bytes(owner.KERNEL_BYTES), "Exact kernel SHA256")):
                with self.subTest(message=message):
                    path.write_bytes(data)
                    with self.assertRaisesRegex(ROMError, message):
                        owner.audit(path)

    def test_cli_rejects_unknown_source_without_metadata_output(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "synthetic.bin"
            path.write_bytes(b"authored non-ROM")
            result = subprocess.run([sys.executable, str(ROOT / "tools/vc2_ce_vmu_owner_audit.py"),
                                     str(path)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, "")
            self.assertIn("Exact kernel size", result.stderr)
            self.assertEqual(path.read_bytes(), b"authored non-ROM")


class OwnerTests(unittest.TestCase):
    def setUp(self):
        if not OWNER:
            self.skipTest("Supply --kernel or KUI_VC2_OWNER_KERNEL for private owner checks")
        self.kernel = Path(OWNER)
        self.rom = owner.VC2SourceROM(self.kernel)

    def test_report_reproduces_checked_in_metadata_and_does_not_modify_input(self):
        before = hashlib.sha256(self.kernel.read_bytes()).hexdigest()
        report = owner.audit(self.kernel)
        expected = json.loads((ROOT / "docs/evidence/vc2-ce-vmu-owner.json").read_text())
        self.assertEqual(report, expected)
        self.assertEqual(hashlib.sha256(self.kernel.read_bytes()).hexdigest(), before)
        for key in ("runtime_admitted", "patching_admitted", "card_read_admitted", "card_write_admitted"):
            self.assertIs(report[key], False)
        self.assertFalse(report["memory"]["reservation_lifetime_proven"])
        self.assertFalse(report["candidate_bridge_padding"]["reference_exclusion_proven"])
        self.assertFalse(report["candidate_bridge_padding"]["relocation_exclusion_proven"])
        self.assertEqual(report["completion_root"]["rva"], "0x000082b8")
        selected = {x["name"]: x for x in report["imports"]}
        self.assertEqual(selected["SetKMode"]["ordinal"], 630)
        self.assertEqual(selected["SetThreadPriority"]["ordinal"], 514)
        continuation=report["continuation_source"]
        self.assertEqual(continuation["main_worker_handle_offset"],"0x00000180")
        self.assertEqual(continuation["dma_event_offset"],"0x00000178")
        self.assertNotEqual(continuation["main_worker_handle_offset"],continuation["dma_event_offset"])
        self.assertEqual(continuation["wait_thunk_rva"],"0x000077a8")
        self.assertEqual(continuation["wait"]["caller_home_bytes"],16)
        self.assertEqual(continuation["wait"]["caller_home_offsets"],[0,4,8,12])
        self.assertEqual(continuation["wait"]["callee_frame_bytes"],28)
        self.assertFalse(continuation["live_continuation_admitted"])

    def test_changed_kernel_header_module_code_data_and_padding_refuse(self):
        maple = self.rom.modules["maple.dll"]
        offsets = {"load_prefix": 16, "ROMHDR": self.rom.address_offset(self.rom.header_address)}
        offsets.update({name: self.rom.address_offset(self.rom.rva_address(maple, rva))
                        for name, rva, *_ in owner.CONTRACTS})
        offsets.update({name: self.rom.address_offset(self.rom.rva_address(maple, rva))
                        for name, rva, _ in owner.LITERALS})
        for name, module in self.rom.modules.items():
            if name in owner.MODULES:
                for section in module["sections"]:
                    offsets[f"{name}.section{section['index']}"] = section["file_offset"]
        offsets["candidate_padding"] = self.rom.address_offset(self.rom.rva_address(maple, 0x7c34))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "changed.bin"
            for name, offset in offsets.items():
                with self.subTest(name=name):
                    changed = bytearray(self.rom.data)
                    changed[offset] ^= 1
                    path.write_bytes(changed)
                    with self.assertRaisesRegex(ROMError, "Exact kernel SHA256"):
                        owner.audit(path)
            for data in (self.rom.data[:-1], self.rom.data + b"\0"):
                path.write_bytes(data)
                with self.assertRaisesRegex(ROMError, "Exact kernel size"):
                    owner.audit(path)

    def test_each_window_independently_rechecks_fingerprints(self):
        maple = self.rom.modules["maple.dll"]
        # Deliberately bypass the *outer* identity gate only in this unit test
        # to establish that local window checks do not trust cached hashes.
        for contract in owner.CONTRACTS:
            with self.subTest(contract=contract[0]):
                rom = copy.copy(self.rom)
                changed = bytearray(rom.data)
                at = rom.address_offset(rom.rva_address(maple, contract[1]))
                changed[at] ^= 1
                rom.data = bytes(changed)
                with self.assertRaisesRegex(ROMError, contract[0] + " SHA256"):
                    owner.source_window(rom, maple, contract)

    def test_changed_parsed_layout_and_allocator_refuse(self):
        for name, key, change in (
            ("maple.dll", "nominal_virtual_base", 0x10000),
            ("mapledev.dll", "virtual_bytes", 0x1000),
        ):
            with self.subTest(module=name, key=key):
                rom = copy.deepcopy(self.rom)
                rom.modules[name][key] += change
                with self.assertRaisesRegex(ROMError, "layout"):
                    owner.check_source_metadata(rom)
        for key in ("ram_free", "ram_end", "driver_globals_address", "driver_globals_bytes"):
            with self.subTest(header=key):
                rom = copy.deepcopy(self.rom)
                rom.header[key] += 4
                with self.assertRaisesRegex(ROMError, "ROMHDR " + key):
                    owner.check_source_metadata(rom)
        rom = copy.deepcopy(self.rom)
        rom.modules["maple.dll"]["sections"][1]["virtual_bytes"] = 0x2b8
        with self.assertRaisesRegex(ROMError, "section 1 layout"):
            owner.check_source_metadata(rom)

    def test_SetKMode_import_does_not_accept_priority_ordinal(self):
        original = self.rom.imports

        def wrong_imports(module):
            imports = copy.deepcopy(original(module))
            for dll in imports:
                if dll["dll"].lower() == "coredll.dll":
                    for symbol in dll["symbols"]:
                        if symbol["iat_rva"] == 0x8044:
                            symbol["ordinal"] = 514
            return imports

        with patch.object(self.rom, "imports", side_effect=wrong_imports):
            with self.assertRaisesRegex(ROMError, "SetKMode import ordinal"):
                owner.check_source_metadata(self.rom)

    def test_continuation_distinguishes_self_thread_handle_from_dma_event(self):
        maple=self.rom.modules["maple.dll"]
        for name,rva,_ in owner.SHORT_LITERALS:
            with self.subTest(field=name):
                rom=copy.copy(self.rom);changed=bytearray(rom.data)
                at=rom.address_offset(rom.rva_address(maple,rva,2))
                changed[at]^=1;rom.data=bytes(changed)
                with self.assertRaisesRegex(ROMError,name):
                    owner.continuation_source_metadata(rom)
        rom=copy.copy(self.rom);changed=bytearray(rom.data)
        at=rom.address_offset(rom.rva_address(maple,0x1d82,2))
        # Change only the reviewed GBR displacement from thread+180 to DMA
        # event+178. The local relation must refuse independently of hashes.
        changed[at]=0x178//4;rom.data=bytes(changed)
        with self.assertRaisesRegex(ROMError,"handle store displacement"):
            owner.continuation_source_metadata(rom)

    def test_wait_body_and_argument_home_prologue_are_source_bound(self):
        core=self.rom.modules["coredll.dll"]
        for delta in (0,owner.WAIT_ABI["prologue_bytes"],owner.WAIT_ABI["body_bytes"]-1):
            with self.subTest(delta=delta):
                rom=copy.copy(self.rom);changed=bytearray(rom.data)
                at=rom.address_offset(rom.rva_address(core,owner.WAIT_ABI["export_rva"]+delta))
                changed[at]^=1;rom.data=bytes(changed)
                with self.assertRaisesRegex(ROMError,"WaitForSingleObject body SHA256"):
                    owner.continuation_source_metadata(rom)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path)
    args, remaining = parser.parse_known_args()
    if args.kernel:
        OWNER = args.kernel
    unittest.main(argv=[sys.argv[0]] + remaining)
