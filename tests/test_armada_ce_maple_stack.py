#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Keep the new CE metadata callback separate from legacy audio bindings."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_retail_stack import CE_INDIRECT, worst


class MapleStackTests(unittest.TestCase):
    def fixture(self, callback, source):
        frames = {name: 0 for values in CE_INDIRECT.values() for name in values}
        frames.update(root=8, probe=96)
        frames[callback] = 12
        return frames, {"root": [("probe", "retail_resident.c:1")],
                        "probe": [("__indirect_call", source + ":1")]}

    def test_maple_callback_frame_is_counted(self):
        frames, calls = self.fixture("read_word", "armada_ce_maple_observe.h")
        report = worst(frames, calls, ce=True, armada_bind=True,
                       armada_maple=True, roots_override={"root": 28})
        self.assertEqual(report["root"]["bytes"], 144)
        self.assertIn("read_word(12)", report["root"]["path"])

    def test_legacy_binding_still_uses_its_original_callback(self):
        frames, calls = self.fixture("read_ram", "armada_ce_table_trace.h")
        report = worst(frames, calls, ce=True, armada_bind=True,
                       roots_override={"root": 0})
        self.assertEqual(report["root"]["bytes"], 116)

    def test_unknown_indirect_call_is_not_admitted(self):
        frames, calls = self.fixture("read_word", "arbitrary_guest_reader.h")
        with self.assertRaisesRegex(ValueError, "Unexpected indirect call"):
            worst(frames, calls, ce=True, armada_bind=True,
                  armada_maple=True, roots_override={"root": 0})

    def test_missing_callback_cannot_be_treated_as_leaf(self):
        frames, calls = self.fixture("read_word", "armada_ce_maple_observe.h")
        del frames["read_word"]
        with self.assertRaisesRegex(ValueError, "Missing callback frame: read_word"):
            worst(frames, calls, ce=True, armada_bind=True,
                  armada_maple=True, roots_override={"root": 0})

    def test_maple_cannot_mix_with_excerpt_or_native_path(self):
        frames, calls = self.fixture("read_word", "armada_ce_maple_observe.h")
        for flags in ({"ce": False}, {"ce": True, "armada_excerpt": True},
                      {"ce": True, "armada_stream": True}):
            with self.subTest(flags=flags), self.assertRaises(ValueError):
                worst(frames, calls, armada_bind=True, armada_maple=True,
                      roots_override={"root": 0}, **flags)

    def proof_fixture(self):
        frames = {name: 0 for values in CE_INDIRECT.values() for name in values}
        frames.update(root=8, read_word=12, read_cache=16,
                      kui_armada_ce_maple_proof_read=20,
                      kui_armada_ce_maple_proof_cache_read=24,
                      kui_armada_ce_excerpt_array_read=0)
        calls = {
            "root": [("kui_armada_ce_maple_proof_cache_read", "armada_ce_maple_probe.c:1")],
            "kui_armada_ce_maple_proof_read": [("__indirect_call", "armada_ce_maple_proof.h:1")],
            "kui_armada_ce_maple_proof_cache_read": [("__indirect_call", "armada_ce_maple_proof.h:2")],
            "read_cache": [("__indirect_call", "armada_ce_maple_probe.c:1")],
        }
        return frames, calls

    def proof_worst(self, frames, calls):
        return worst(frames, calls, ce=True, armada_bind=True, armada_maple=True,
                     armada_maple_proof=True, roots_override={"root": 0})

    def test_proof_counts_cache_and_p2_leaf_frames(self):
        frames, calls = self.proof_fixture()
        report = self.proof_worst(frames, calls)["root"]
        self.assertEqual(report["bytes"], 48)
        self.assertIn("read_cache(16)", report["path"])
        frames["kui_armada_ce_excerpt_array_read"] = 2000
        grown = self.proof_worst(frames, calls)["root"]
        self.assertEqual(grown["bytes"], 2048)
        self.assertIn("kui_armada_ce_excerpt_array_read(2000)", grown["path"])

    def test_headers_callback_counts_existing_budget_reader(self):
        frames, calls = self.proof_fixture()
        frames["kui_armada_ce_maple_headers_read"] = 28
        calls["root"] = [("kui_armada_ce_maple_headers_read", "armada_ce_maple_state.h:1")]
        calls["kui_armada_ce_maple_headers_read"] = [("__indirect_call", "armada_ce_maple_headers.h:1")]
        self.assertEqual(self.proof_worst(frames, calls)["root"]["bytes"], 76)

    def test_proof_cache_and_metadata_callbacks_remain_separate(self):
        frames, calls = self.proof_fixture()
        frames["read_cache"] = 3000
        calls["root"] = [("kui_armada_ce_maple_proof_read", "armada_ce_maple_probe.c:1")]
        self.assertEqual(self.proof_worst(frames, calls)["root"]["bytes"], 40)

    def test_proof_missing_assembly_frame_refuses(self):
        frames, calls = self.proof_fixture()
        del frames["kui_armada_ce_excerpt_array_read"]
        with self.assertRaisesRegex(ValueError, "Missing callback frame"):
            self.proof_worst(frames, calls)

    def test_proof_unknown_callback_site_refuses(self):
        for source in ("armada_ce_maple_proof.h", "armada_ce_maple_mapping.h",
                       "armada_ce_cache_word.h", "armada_ce_maple_probe.c",
                       "armada_ce_maple_headers.h"):
            frames, calls = self.proof_fixture()
            frames["unexpected"] = 4
            calls["root"] = [("unexpected", "armada_ce_maple_probe.c:1")]
            calls["unexpected"] = [("__indirect_call", source + ":1")]
            with self.subTest(source=source), self.assertRaisesRegex(ValueError, "Unexpected .*callback"):
                self.proof_worst(frames, calls)


if __name__ == "__main__":
    unittest.main()
