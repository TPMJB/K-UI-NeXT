# SPDX-License-Identifier: GPL-3.0-only
"""Exercise installed files, archive identity and the existing publication boundary."""
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from package import (candidate_evidence, candidate_requested, development_experiment, development_notice,
                     development_test, package_kind, package_metadata,
                     release_metadata, write_release_assets, write_release_bundle,
                     write_release_guides)
from publish_release import verify_assets
from runtime_package import envelope
from retail_package import relocation_header, HEADER_OFFSET, STAGE_BLOB_OFFSET

STABLE_RELEASE = {"version": "1.7", "name": 'K-UI V1.7 "Dáinsleif"',
                  "short_name": "K-UI V1.7", "artifact_prefix": "kui-1.7-dainsleif"}
TEST_RELEASE = {"version": "1.7-2048-test", "name": 'K-UI V1.7 2048-byte test "Dáinsleif"',
                "short_name": "K-UI V1.7 2048 test", "artifact_prefix": "kui-1.7-2048-test-dainsleif"}
FORMATS_RELEASE = {"version": "1.7-formats-test", "name": 'K-UI V1.7 Games formats test "Dáinsleif"',
                   "short_name": "K-UI V1.7 formats test", "artifact_prefix": "kui-1.7-formats-test-dainsleif"}


class CandidateInstallation(unittest.TestCase):
    def setUp(self):
        # Stable/candidate publication tests keep exercising the actual 1.7
        # policy even when this source branch names an experimental build.
        metadata = patch("package.release_metadata", return_value=STABLE_RELEASE)
        metadata.start()
        self.addCleanup(metadata.stop)
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.dist = Path(self.temporary.name)
        self.sd = self.dist / "sd/KUI"
        self.commit = "0123456789abcdef" * 2 + "01234567"
        self.music = {"tracks": [{"ogg": {"file": "menu.ogg"}}]}
        files = ("runtime.kui", "redump.db", "tosec.db", "apps/music/menu.ogg",
                 "apps/games/probe.kui", "apps/games/image-probe.kui",
                 "apps/games/retail-boot.kui", "apps/games/probe.dat",
                 "apps/games/ce-probe.kui", "tests/scan/fixture.bin", "preferences.ini")
        for name in files:
            path = self.sd / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(("fixture " + name).encode())
        for name in files:
            if name.endswith(".kui"):
                (self.sd / name).write_bytes(envelope(b"\x09\0\x09\0", 4, self.commit[:12]))
        for name in ("STORAGE-TRANSPORTS.md", "EXT4-BOOTSTRAP.md", "BOOT-RECOVERY.md",
                     "WINDOWS-CE-PLACEMENT-TEST.md", "GAMES-BACKGROUND-READER.md", "WIFI.md", "WIFI-FLASH-ARCH.md",
                     "SCI-CONNECTOR.md", "FTP.md", "LICENSE", "THIRD_PARTY.md"):
            (self.dist / name).write_text(name + "\n")
        (self.dist / "LICENSES").mkdir()
        (self.dist / "LICENSES/notice.txt").write_text("fixture license\n")
        self.cdi = self.dist / "kui-diagnostic.cdi"
        self.cdi.write_bytes(b"fixture boot image")
        self.source = self.dist / "source"
        self.source.mkdir()
        for name in ("kui-source.tar.gz", "kos-source.tar.gz"):
            (self.source / name).write_bytes(("fixture " + name).encode())

    def assemble(self, candidate):
        release = package_metadata(candidate)
        candidate = candidate and not development_test(release)
        record = {"commit": self.commit, "release": release, "hardware_tested": False,
                  "kind": package_kind(release, candidate)}
        if candidate:
            record.update(candidate=candidate_evidence())
        if development_test(release):
            record.update(experiment=development_experiment(release))
        (self.dist / "build.json").write_text(json.dumps(record))
        write_release_guides(self.dist, candidate)
        bundle = write_release_bundle(self.dist, self.sd, self.cdi, self.music, record,
                                      {"bootstrap": {"fixture": True}}, candidate)
        write_release_assets(self.dist, bundle, self.source, release)
        return bundle, release

    def assert_checksums(self, directory, filename):
        lines = (directory / filename).read_text().splitlines()
        listed = set()
        for line in lines:
            digest, name = line.split("  ", 1)
            self.assertNotIn(name, listed)
            listed.add(name)
            self.assertEqual(hashlib.sha256((directory / name).read_bytes()).hexdigest(), digest)
        expected = {str(path.relative_to(directory)) for path in directory.rglob("*")
                    if path.is_file() and path.name != filename}
        self.assertEqual(listed, expected)

    def test_candidate_contents_evidence_and_exact_source_are_preserved(self):
        bundle, release = self.assemble(True)
        record = json.loads((bundle / "build.json").read_text())
        self.assertEqual(record["kind"], "release-candidate")
        self.assertFalse(record["hardware_tested"])
        self.assertEqual(record["candidate"]["ata_hardware"], "untested")
        self.assertEqual(record["candidate"]["sci512_hardware"], "rejected; restored 256-byte allowance")
        self.assertEqual(record["commit"], self.commit)
        for name in ("runtime.kui", "apps/games/retail-boot.kui", "apps/games/ce-probe.kui"):
            self.assertEqual((bundle / "KUI" / name).read_bytes(), (self.sd / name).read_bytes())
        self.assertTrue((bundle / "WINDOWS-CE-PLACEMENT-TEST.md").is_file())
        for name in ("WIFI.md", "WIFI-FLASH-ARCH.md", "SCI-CONNECTOR.md", "FTP.md"):
            self.assertEqual((bundle / name).read_bytes(), (self.dist / name).read_bytes())
        self.assertTrue((bundle / "boot-cd" / (release["artifact_prefix"] + ".cdi")).is_file())
        self.assertFalse((bundle / "KUI/preferences.ini").exists())
        self.assertFalse((bundle / "KUI/tests").exists())
        for name in ("START-HERE.md", "RELEASE-NOTES.md", "SOURCE.txt"):
            text = (bundle / name).read_text()
            self.assertIn("candidate", text)
            self.assertIn("256", text)
            self.assertIn("untested", text)
            self.assertNotIn("— final release", text)
        self.assert_checksums(bundle, "SHA256SUMS")
        assets = self.dist / "release-assets"
        self.assert_checksums(assets, "SHA256SUMS.txt")
        with zipfile.ZipFile(assets / (release["artifact_prefix"] + "-source.zip")) as archive:
            source_record = json.loads(archive.read("build.json"))
            self.assertEqual(source_record["commit"], self.commit)
            self.assertEqual(source_record["kind"], "release-candidate")
            self.assertEqual(source_record["release"], release)
            self.assertEqual(archive.read("source/kui-source.tar.gz"),
                             (self.source / "kui-source.tar.gz").read_bytes())

    def test_candidate_cannot_pass_existing_public_release_verification(self):
        _, release = self.assemble(True)
        with self.assertRaisesRegex(SystemExit, "Refusing a diagnostic or candidate package"):
            verify_assets(self.dist / "release-assets", release, self.commit)

    def test_stable_build_after_candidate_keeps_launch_payloads_and_remains_promotable(self):
        self.assemble(True)
        bundle, release = self.assemble(False)
        self.assertEqual(release, STABLE_RELEASE)
        self.assertEqual(json.loads((bundle / "build.json").read_text())["kind"], "release")
        self.assertEqual((bundle / "KUI/apps/games/ce-probe.kui").read_bytes(),
                         (self.sd / "apps/games/ce-probe.kui").read_bytes())
        self.assertTrue((bundle / "WINDOWS-CE-PLACEMENT-TEST.md").exists())
        self.assertTrue((bundle / "GAMES-BACKGROUND-READER.md").exists())
        self.assertTrue((bundle / "ANNOUNCEMENTS.md").exists())
        self.assertEqual((bundle / "release-banner.jpg").read_bytes(),
                         (ROOT / "resources/branding/release-v1.7-banner.jpg").read_bytes())
        self.assertEqual({path.name for path in (bundle / "boot-cd").iterdir()},
                         {"kui-v" + release["version"] + ".cdi"})
        self.assertIn("— final release", (bundle / "SOURCE.txt").read_text())
        self.assertIn(release["version"], (bundle / "START-HERE.md").read_text())
        self.assertEqual(len(verify_assets(self.dist / "release-assets", release, self.commit)), 3)
        self.assert_checksums(bundle, "SHA256SUMS")

    def test_release_cannot_omit_windows_ce_payload_or_misidentify_source(self):
        for corruption in ("ce-payload", "commit"):
            with self.subTest(corruption=corruption):
                bundle, release = self.assemble(False)
                if corruption == "ce-payload":
                    (bundle / "KUI/apps/games/ce-probe.kui").unlink()
                else:
                    record = json.loads((bundle / "build.json").read_text())
                    record["commit"] = "f" * 40
                    (bundle / "build.json").write_text(json.dumps(record))
                write_release_assets(self.dist, bundle, self.source, release)
                expected = "Incomplete release" if corruption == "ce-payload" else "source identity"
                with self.assertRaisesRegex(SystemExit, expected):
                    verify_assets(self.dist / "release-assets", release, self.commit)

    def test_archive_tampering_remains_rejected(self):
        _, release = self.assemble(False)
        archive = self.dist / "release-assets" / (release["artifact_prefix"] + "-release.zip")
        with archive.open("ab") as stream:
            stream.write(b"unexpected bytes")
        with self.assertRaisesRegex(SystemExit, "checksum mismatch"):
            verify_assets(self.dist / "release-assets", release, self.commit)

    def test_experimental_bundle_omits_banner_and_cannot_be_published(self):
        for release in (TEST_RELEASE, FORMATS_RELEASE):
            with self.subTest(version=release["version"]):
                self.check_experimental_bundle(release)

    def check_experimental_bundle(self, test_release):
        # Reuse the destination after a stable bundle to catch stale artwork
        # as well as accidental final-release identity in either archive.
        self.assemble(False)
        with patch("package.release_metadata", return_value=test_release):
            bundle, release = self.assemble(False)
        self.assertEqual(release, test_release)
        self.assertFalse((bundle / "release-banner.jpg").exists())
        self.assertEqual({path.name for path in (bundle / "boot-cd").iterdir()},
                         {release["artifact_prefix"] + ".cdi"})
        record = json.loads((bundle / "build.json").read_text())
        self.assertEqual(record["kind"], "development-test")
        self.assertFalse(record["hardware_tested"])
        self.assertEqual(record["release"], test_release)
        self.assertEqual(record["experiment"], development_experiment(test_release))
        self.assertIn("experimental hardware test", (bundle / "SOURCE.txt").read_text())
        self.assertIn("not a public release", (bundle / "SOURCE.txt").read_text())
        if test_release == FORMATS_RELEASE:
            self.assertIn("GDI, ISO, BIN/CUE, CDI and standalone BIN/IMG", (bundle / "SOURCE.txt").read_text())
            self.assertNotIn("2048-byte sector hardware test", (bundle / "SOURCE.txt").read_text())
            installed = (bundle / "GAMES-FORMATS-TEST.md").read_text()
            source = (ROOT / "docs/games-formats-test.md").read_text()
            self.assertEqual(installed.splitlines()[0], source.splitlines()[0])
            for label in ("| GDI |", "| ISO |", "| BIN/CUE |", "| CDI |"):
                self.assertIn(label, installed)
            # Supporting guides omitted from the old experimental bundle
            # become links to this exact source commit.
            self.assertNotIn("](games-formats.md)", installed)
            if "](games-formats.md)" in source:
                self.assertIn("/blob/" + self.commit + "/docs/games-formats.md", installed)
            self.assertIn("(GAMES-FORMATS-TEST.md)", (bundle / "START-HERE.md").read_text())
        for name in ("START-HERE.md", "RELEASE-NOTES.md", "ANNOUNCEMENTS.md", "SOURCE.txt"):
            self.assertNotIn("— final release", (bundle / name).read_text())
        assets = self.dist / "release-assets"
        with zipfile.ZipFile(assets / (release["artifact_prefix"] + "-source.zip")) as archive:
            source_record = json.loads(archive.read("build.json"))
            self.assertEqual(source_record["kind"], "development-test")
            self.assertEqual(source_record["release"], test_release)
            self.assertEqual(source_record["experiment"], development_experiment(test_release))
        self.assert_checksums(bundle, "SHA256SUMS")
        self.assert_checksums(assets, "SHA256SUMS.txt")
        with self.assertRaisesRegex(SystemExit, "Refusing a diagnostic or candidate package"):
            verify_assets(assets, release, self.commit)

    def test_185_install_keeps_matching_native_ce_payloads_tools_and_codec_notices(self):
        release185 = {"version": "1.8.5", "name": 'K-UI V1.8.5 "Dáinsleif"',
                      "short_name": "K-UI V1.8.5", "artifact_prefix": "kui-1.8.5-dainsleif"}
        for ce, name in ((False, "retail-boot.kui"), (True, "ce-probe.kui")):
            payload = bytearray(STAGE_BLOB_OFFSET + 4)
            payload[HEADER_OFFSET:HEADER_OFFSET + 64] = relocation_header(4, ce=ce, low=not ce)
            payload[-4:] = b"\x09\0\x09\0"
            (self.sd / "apps/games" / name).write_bytes(envelope(payload, len(payload), self.commit[:12]))
        with patch("package.release_metadata", return_value=release185):
            bundle, release = self.assemble(False)
        self.assertEqual(release, release185)
        for name in ("gdi_optimize.py", "game_image_import.py"):
            self.assertEqual((bundle / "tools" / name).read_bytes(), (ROOT / "tools" / name).read_bytes())
        for name in ("GAMES-FORMATS.md", "GDI-2048-TEST.md", "RIPPER-CONTROLS.md", "CAPTURE-FORMAT.md"):
            self.assertTrue((bundle / name).is_file())
        for name in ("capture-codecs.md", "miniz-NOTICES.txt", "minilzo-COPYING", "minilzo-README.LZO"):
            self.assertTrue((bundle / "LICENSES" / name).is_file())
        self.assertIn("Martin Raiber", (bundle / "LICENSES/miniz-NOTICES.txt").read_text())
        self.assertIn("free and unencumbered", (bundle / "LICENSES/miniz-NOTICES.txt").read_text())
        self.assertEqual(len(verify_assets(self.dist / "release-assets", release185, self.commit)), 3)
        self.assert_checksums(bundle, "SHA256SUMS")
        with zipfile.ZipFile(self.dist / "release-assets" / (release["artifact_prefix"] + "-source.zip")) as archive:
            self.assertIn("LICENSES/minilzo-COPYING", archive.namelist())

    def test_mixed_payload_build_or_invalid_envelope_is_rejected_even_with_fresh_zip_checksums(self):
        for corruption in ("wrong-build", "bad-payload"):
            with self.subTest(corruption=corruption):
                bundle, release = self.assemble(False)
                path = bundle / "KUI/apps/games/ce-probe.kui"
                if corruption == "wrong-build":
                    path.write_bytes(envelope(b"\x09\0\x09\0", 4, "f" * 12))
                else:
                    damaged = bytearray(path.read_bytes())
                    damaged[-1] ^= 1
                    path.write_bytes(damaged)
                write_release_assets(self.dist, bundle, self.source, release)
                message = "payload source identity" if corruption == "wrong-build" else "Payload checksum"
                with self.assertRaisesRegex(SystemExit, message):
                    verify_assets(self.dist / "release-assets", release, self.commit)



class CandidateSelection(unittest.TestCase):
    def setUp(self):
        metadata = patch("package.release_metadata", return_value=STABLE_RELEASE)
        metadata.start()
        self.addCleanup(metadata.stop)

    def test_only_explicit_candidate_flag_changes_package_identity(self):
        for value in ("", "0"):
            with self.subTest(value=value), patch.dict(os.environ, {"KUI_RELEASE_CANDIDATE": value}):
                self.assertFalse(candidate_requested())
                self.assertEqual(package_metadata(), STABLE_RELEASE)
        with patch.dict(os.environ, {"KUI_RELEASE_CANDIDATE": "1"}):
            candidate = package_metadata()
        self.assertEqual(candidate["version"], STABLE_RELEASE["version"])
        self.assertEqual(candidate["short_name"], STABLE_RELEASE["short_name"])
        self.assertIn("-ata-readiness-candidate", candidate["artifact_prefix"])
        with self.assertRaisesRegex(ValueError, "KUI_RELEASE_CANDIDATE"):
            candidate_requested({"KUI_RELEASE_CANDIDATE": "true"})
        self.assertFalse(candidate_requested({"KUI_EXPERIMENTAL": "1"}))

    def test_both_development_versions_stay_outside_candidate_and_public_identity(self):
        for release in (TEST_RELEASE, FORMATS_RELEASE):
            with self.subTest(version=release["version"]), patch("package.release_metadata", return_value=release):
                self.assertTrue(development_test())
                self.assertEqual(package_metadata(True), release)
                self.assertEqual(package_kind(release, True), "development-test")
                self.assertIn(development_experiment(release), development_notice())
        self.assertFalse(development_test(STABLE_RELEASE))


if __name__ == "__main__":
    unittest.main()
