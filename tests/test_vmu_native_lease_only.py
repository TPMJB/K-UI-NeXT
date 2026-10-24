#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retirement/dispatch evidence from the actual linked reservation-only stage."""
import copy
import os
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check_vmu_native_lease_only import HELPER, SDK_INSTALL, LeaseDispatch, SdkInstall, audit
from check_vmu_native_worker_gateway import read_elf, symbol_bytes


class LeaseOnlyArtifact(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        directory = Path(os.environ.get("KUI_NATIVE_ARTIFACT_DIR", ROOT / "build/evolution-isolation-dev"))
        if not (directory / "stage.elf").exists():
            raise unittest.SkipTest("build the isolation stage or set KUI_NATIVE_ARTIFACT_DIR")
        cls.stage = read_elf(directory / "stage.elf")
        cls.resident = read_elf(directory / "resident-sci.elf")
        if HELPER not in cls.stage[4]:
            raise unittest.SkipTest("selected artifact predates reservation-only mode")

    def test_actual_helper_selector_and_mutation_families(self):
        report = audit(self.stage, self.resident, self_test=True)
        self.assertEqual(report["instruction_cases"], 540)
        self.assertEqual(report["selector_instruction_cases"], 228)
        self.assertGreaterEqual(report["mutation_rejections"], 9)
        self.assertTrue(report["terminal_and_sd_callbacks_null"])
        self.assertTrue(report["mode_three_returns_before_first_retail_veneer"])
        self.assertTrue(report["mode_three_returns_before_sdk_call_target_install"])
        install=report["sdk_call_target_installation"]
        self.assertEqual(install["instruction_cases"],624)
        self.assertEqual(install["mutation_rejections"],4)
        self.assertEqual((install["success_return"],install["admission_refusal_return"],
                          install["readback_refusal_return"]),(1,0xffffffff,0xfffffffe))
        self.assertTrue(report["installed_status_written_last"])

    def test_typed_exact_low_control_is_required(self):
        name = "_kui_vmu_observe_boot_control"
        for index, value in ((0, self.resident[4][name][0] - 4), (1, 60), (1, 0), (2, 2)):
            symbols = copy.deepcopy(self.resident[4]); row = list(symbols[name]); row[index] = value
            symbols[name] = tuple(row)
            with self.subTest(index=index, value=value), self.assertRaises((ValueError, AssertionError)):
                audit(self.stage, self.resident[:4] + (symbols,))

    def test_new_mode_cannot_fall_back_to_an_old_installer(self):
        symbols = dict(self.stage[4]); del symbols[HELPER]
        with self.assertRaisesRegex(ValueError, "lacks.*reservation-only"):
            audit(self.stage[:4] + (symbols,), self.resident)
        with self.assertRaisesRegex(ValueError, "typed resident"):
            audit(self.stage, None)

    def test_linked_selector_cannot_swap_profile_or_normal(self):
        _, body, _ = symbol_bytes(self.stage, "_native_passive_run", 2)
        at = LeaseDispatch(self.stage, self.resident, 1, 6, 3).selector_offset + 6
        for value in (0x8800, 0x8801, 0x8802):
            changed = bytearray(body); struct.pack_into("<H", changed, at, value)
            with self.subTest(value=value), self.assertRaises((ValueError, AssertionError)):
                LeaseDispatch(self.stage, self.resident, 1, 6, 3, body=bytes(changed)).run().check()

    def test_cold_install_precedes_veneer_in_all_nonlease_modes(self):
        for mode in (0,1,2,4,5,6):
            for result in (1,0xffffffff,0xfffffffe):
                with self.subTest(mode=mode,result=result):
                    selector=LeaseDispatch(self.stage,self.resident,1,6,mode,result).run()
                    selector.check()
                    self.assertEqual(selector.calls,["mode","sdk-install",
                        "veneer" if result==1 else "refusal"])
        lease=LeaseDispatch(self.stage,self.resident,1,6,3,0xffffffff).run();lease.check()
        self.assertEqual(lease.calls,["mode","lease-only"])

    def test_sdk_install_result_requires_actual_slot_store(self):
        _,body,_=symbol_bytes(self.stage,SDK_INSTALL,2)
        store=struct.pack("<H",0x2122)
        self.assertEqual(body.count(store),1)
        changed=bytearray(body);struct.pack_into("<H",changed,body.index(store),9)
        with self.assertRaises((ValueError,AssertionError)):
            SdkInstall(self.stage,1,6,0,body=bytes(changed)).run().check()


if __name__ == "__main__":
    unittest.main()
