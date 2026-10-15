# SPDX-License-Identifier: GPL-3.0-only
"""Release integration: exact CE build flags, provenance and profile admission."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import package
from retail_package import HEADER_BYTES, HEADER_OFFSET, STAGE_BLOB_OFFSET, relocation_header
from runtime_package import ADDRESS, envelope, verify
from test_image_loader_layout import executable


class MusicReleasePackaging(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.games = self.root / "dist/sd/KUI/apps/games"
        self.games.mkdir(parents=True)
        self.commit = "0123456789abcdef" * 2 + "01234567"
        self.payload = bytearray(STAGE_BLOB_OFFSET + 32)
        self.payload[HEADER_OFFSET:HEADER_OFFSET + HEADER_BYTES] = relocation_header(32, ce=True)
        self.payload[-32:-19] = self.commit[:12].encode() + b"\0"
        for name in package.WINCE_CDDA_PROFILES:
            directory = self.root / "build" / (name + "-ce-stream")
            directory.mkdir(parents=True)
            elf = directory / "entry.elf"
            elf.write_bytes(executable(ADDRESS, self.payload, len(self.payload), {}))
            self.provenance(elf)
        self.root_patch = patch("package.ROOT", self.root)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)
        self.audit_patch = patch("package.audit_wince_cdda", side_effect=self.audit)
        self.auditor = self.audit_patch.start()
        self.addCleanup(self.audit_patch.stop)

    def provenance(self, elf, **changes):
        record = {"commit": self.commit, "source_dirty": False,
                  "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest()}
        record.update(changes)
        elf.with_suffix(".compile.json").write_text(json.dumps(record))

    @staticmethod
    def audit(directory, data, profile):
        # Linked machine-code auditing is exercised by its own mutation suite.
        # This fixture tests that release packaging requires and retains it.
        return ({"wire_all_zero": True, "stage_offset": 64}, {
            "title_profile": package.WINCE_CDDA_PROFILES[profile], "stream_profile": True,
            "distributed_pcm_bytes": 0, "distributed_audio_map_bytes": 4096,
            "stream_boot_arm": {"attach_only_after_relay": True,
                                "preserves_controller_zero_check": True},
            "native_owner_reader": {"kernel_profile": package.WINCE_CDDA_PROFILES[profile]},
            "seed_address": "0x8ce10040", "linked_direct_calls": {"checked": True}})

    def build(self):
        return package.write_wince_cdda_modules(self.games, self.root / "dist", self.commit)

    def test_all_four_modules_match_source_and_preserve_their_linked_audits(self):
        profiles = self.build()
        self.assertEqual(set(profiles), {"armada", "bam4", "worms", "wwp"})
        self.assertEqual(self.auditor.call_count, 4)
        for name, info in profiles.items():
            path = self.games / ("ce-cdda-" + name + ".kui")
            self.assertEqual(verify(path.read_bytes())["build"], self.commit[:12])
            self.assertEqual(info["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
            self.assertEqual(info["compile"]["commit"], self.commit)
            self.assertFalse(info["new_binary_hardware_tested"])
            audit = self.root / "dist" / (name + "-ce-stream-build") / "linked-audit.json"
            self.assertEqual(json.loads(audit.read_text()), info["linked_audit"])

    def test_world_party_requires_owner_seed_and_linked_call_proof(self):
        ordinary = self.audit
        for field in ("owner", "seed", "direct_calls"):
            with self.subTest(field=field):
                def changed(directory, data, profile):
                    seed, audit = copy.deepcopy(ordinary(directory, data, profile))
                    if profile == "wwp":
                        if field == "owner":
                            audit["native_owner_reader"]["kernel_profile"] = "armada-us"
                        elif field == "seed":
                            audit["seed_address"] = "0x8ce10044"
                        else:
                            audit["linked_direct_calls"] = None
                    return seed, audit
                self.auditor.side_effect = changed
                with self.assertRaisesRegex(ValueError, "World Party"):
                    self.build()

    def test_missing_dirty_foreign_or_modified_modules_cannot_be_packaged(self):
        elf = self.root / "build/armada-ce-stream/entry.elf"
        for changes in ({"source_dirty": True}, {"commit": "f" * 40}, {"elf_sha256": "0" * 64}):
            with self.subTest(changes=changes):
                self.provenance(elf, **changes)
                with self.assertRaisesRegex(ValueError, "provenance"):
                    self.build()
        self.provenance(elf)
        elf.unlink()
        with self.assertRaises(FileNotFoundError):
            self.build()

    def test_another_profile_nonblank_source_or_unchecked_bootstrap_is_refused(self):
        for key, value in (("title_profile", "worms-us"), ("distributed_pcm_bytes", 4),
                           ("distributed_audio_map_bytes", 16), ("stream_profile", False),
                           ("attach_only_after_relay", False), ("wire_all_zero", False)):
            with self.subTest(field=key):
                seed, audit = copy.deepcopy(self.audit(None, None, "armada"))
                if key == "wire_all_zero":
                    seed[key] = value
                elif key == "attach_only_after_relay":
                    audit["stream_boot_arm"][key] = value
                else:
                    audit[key] = value
                self.auditor.side_effect = None
                self.auditor.return_value = (seed, audit)
                with self.assertRaisesRegex(ValueError, "audit differs"):
                    self.build()

    def test_matching_compile_record_cannot_hide_a_stale_display_or_invalid_envelope(self):
        elf = self.root / "build/armada-ce-stream/entry.elf"
        bad = bytearray(self.payload)
        bad[-32:-19] = b"f" * 12 + b"\0"
        elf.write_bytes(executable(ADDRESS, bad, len(bad), {}))
        self.provenance(elf)
        with self.assertRaisesRegex(ValueError, "display build"):
            self.build()
        bad = bytearray(self.payload)
        bad[0x1000] = 1
        elf.write_bytes(executable(ADDRESS, bad, len(bad), {}))
        self.provenance(elf)
        with self.assertRaisesRegex(ValueError, "blank card-specific manifest"):
            self.build()

    def test_audio_off_slot_requires_the_ordinary_ce_loader_with_tested_resume_fix(self):
        directory = self.root / "build/retail-ce"
        directory.mkdir()
        config = directory / "layout-config"
        good = "LOW=0 TRACE=0 SONIC=0 CE_SCI_RESUME_FIX=1"
        config.write_text(good)
        plain = envelope(self.payload, len(self.payload), self.commit[:12])
        package.validate_ordinary_ce(directory, plain)
        for text in (good.replace("LOW=0", "LOW=1"), good.replace("CE_SCI_RESUME_FIX=1", ""),
                     good.replace("TRACE=0", "TRACE=3")):
            config.write_text(text)
            with self.assertRaisesRegex(ValueError, "tested placement"):
                package.validate_ordinary_ce(directory, plain)
        config.write_text(good)
        for marker in (b"CEX2", b"CE CD MUSIC PROFILE MISMATCH", b"ARMADA AUDIO TEST"):
            with self.assertRaisesRegex(ValueError, "specialized audio"):
                package.validate_ordinary_ce(directory, plain + marker)


class DefaultMusicBuild(unittest.TestCase):
    def dry_run(self, target, *settings):
        with tempfile.TemporaryDirectory() as temporary:
            kos = Path(temporary)
            (kos / "Makefile.rules").write_text("")
            result = subprocess.run(["make", "--no-print-directory", "-n", "-f", "Makefile.dc", target,
                                     "KOS_BASE=" + str(kos), "BUILD_ID=0123456789ab", *settings],
                                    cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def test_normal_all_target_reaches_four_separately_linked_music_profiles(self):
        text = self.dry_run("all")
        for name in ("armada", "bam4", "worms", "wwp"):
            self.assertIn("-o build/" + name + "-ce-stream/entry.elf", text)
            self.assertIn("tools/check_armada_ce_excerpt.py --stream", text)
        self.assertIn("-DKUI_WINCE_CDDA_BAM4=1", text)
        self.assertIn("-DKUI_WINCE_CDDA_WORMS=1", text)
        self.assertIn("-DKUI_WINCE_CDDA_WWP=1", text)
        self.assertIn("--stream --wwp", text)
        self.assertIn("python3 tools/build_native_cdda_compat.py", text)

    def test_native_diagnostics_do_not_enter_music_modules_or_audio_off_fallback(self):
        for target in ("wince-cdda", "build/retail-ce/entry.elf"):
            with self.subTest(target=target):
                text = self.dry_run(target, "KUI_RETAIL_LOW_RESIDENT=1", "KUI_RETAIL_STARTUP_TRACE=3",
                                    "KUI_RETAIL_SONIC_STACK_TEST=1")
                compile_lines = [line for line in text.splitlines()
                                 if line.startswith("sh-elf-gcc ") and " -c " in line]
                self.assertTrue(compile_lines)
                for line in compile_lines:
                    self.assertIn("-DKUI_RETAIL_LOW_RESIDENT=0", line)
                    self.assertIn("-DKUI_RETAIL_STARTUP_TRACE=0", line)
                    self.assertIn("-DKUI_RETAIL_SONIC_STACK_TEST=0", line)
                self.assertIn("-DKUI_CE_SCI_RESUME_FIX=1", text)


class MusicAuditRouting(unittest.TestCase):
    def test_world_party_always_selects_the_wwp_linked_audit(self):
        with patch("package_armada_ce_stream.inspect_stream_seed", return_value={}) as seed, \
                patch("check_armada_ce_excerpt.check", return_value={}) as audit:
            package.audit_wince_cdda(Path("/audit"), b"module", "wwp")
        seed.assert_called_once_with(b"module")
        audit.assert_called_once_with(Path("/audit"), stream=True, wwp=True)

    def test_unknown_profile_cannot_fall_through_to_armada(self):
        with self.assertRaisesRegex(ValueError, "Unknown WinCE"):
            package.audit_wince_cdda(Path("/audit"), b"module", "other")


if __name__ == "__main__":
    unittest.main()
