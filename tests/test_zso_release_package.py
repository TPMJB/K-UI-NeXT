# SPDX-License-Identifier: GPL-3.0-only
"""ZSO installation identity, corresponding-source identity and RC boundaries."""
import hashlib
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import package
import publish_release
import test_package_candidate as candidate
from runtime_package import ADDRESS, envelope, flatten_elf, verify
from test_image_loader_layout import executable
from zso_package import HEADER_BYTES, HEADER_OFFSET, STAGE_BLOB_OFFSET, inspect_zso, relocation_header


TYRFING = {"version": "2.0-rc10", "name": 'K-UI V2.0 RC10 "Tyrfing"',
           "short_name": "K-UI V2.0 RC10", "artifact_prefix": "kui-2.0-rc10-tyrfing"}


class ZsoReleaseInstallation(unittest.TestCase):
    def setUp(self):
        # Reuse only the installation fixture, without inheriting/rerunning its
        # separate stable-release tests under this module's discovery.
        self.fixture = candidate.CandidateInstallation(methodName="runTest")
        self.addCleanup(self.fixture.doCleanups)
        self.fixture.setUp()
        metadata = patch("package.release_metadata", return_value=TYRFING)
        metadata.start()
        self.addCleanup(metadata.stop)
        self.path = self.fixture.sd / "apps/games/zso-boot.kui"
        payload = bytearray(STAGE_BLOB_OFFSET + 64)
        payload[HEADER_OFFSET:HEADER_OFFSET + HEADER_BYTES] = relocation_header(64)
        payload[STAGE_BLOB_OFFSET:STAGE_BLOB_OFFSET + 4] = b"\x09\0\x09\0"
        payload[-16:-3] = self.fixture.commit[:12].encode("ascii") + b"\0"
        self.elf = executable(ADDRESS, payload, len(payload), {})
        self.payload, memory = flatten_elf(self.elf)
        self.expected = envelope(self.payload, memory, self.fixture.commit[:12])
        self.path.write_bytes(self.expected)
        # Distinct content detects accidental substitution by another guide,
        # regeneration, truncation or encoding changes during tree/ZIP copying.
        self.guide = ("# ZSO installation identity fixture\n\n"
                      "Standard LZ4 ZISO v1; native SCI Standard reader only.\n"
                      "Windows CE, background loading, CDDA and RAM patches: Off/unavailable.\n"
                      "Keep the original image. Console acceptance remains unconfirmed.\n"
                      "Preparation for Tyrfing \u2014 no release or tag.\n").encode("utf-8")
        (self.fixture.dist / "GAMES-ZSO.md").write_bytes(self.guide)

    def assemble(self):
        return self.fixture.assemble(False)

    def test_rc_install_and_archive_retain_exact_zso_payload_guide_and_source_identity(self):
        bundle, release = self.assemble()
        self.assertEqual(release, TYRFING)
        self.assertEqual(self.path.read_bytes(), self.expected)
        self.assertEqual((bundle / "KUI/apps/games/zso-boot.kui").read_bytes(), self.expected)
        self.assertEqual((bundle / "GAMES-ZSO.md").read_bytes(), self.guide)
        info = inspect_zso(self.expected)
        self.assertEqual(info["build"], self.fixture.commit[:12])
        self.assertEqual(info["payload_bytes"], len(self.payload))
        self.assertEqual(info["memory_bytes"], len(self.payload))
        self.assertEqual(info["manifest_magic"], "KUIRZS01")
        self.assertEqual((info["transport"], info["reader"]), ("SCI", "Standard"))
        self.assertEqual(self.expected[64:], self.payload)
        self.assertNotEqual(self.expected, (self.fixture.sd / "apps/games/retail-boot.kui").read_bytes())
        self.fixture.assert_checksums(bundle, "SHA256SUMS")
        assets = self.fixture.dist / "release-assets"
        self.fixture.assert_checksums(assets, "SHA256SUMS.txt")
        with zipfile.ZipFile(assets / (release["artifact_prefix"] + "-release.zip")) as install:
            self.assertEqual(install.read("KUI/apps/games/zso-boot.kui"), self.expected)
            self.assertEqual(install.read("GAMES-ZSO.md"), self.guide)
            record = json.loads(install.read("build.json"))
            self.assertEqual(record["commit"], self.fixture.commit)
            self.assertEqual(record["kind"], "release-candidate")
            self.assertFalse(record["hardware_tested"])
            self.assertEqual(verify(install.read("KUI/apps/games/zso-boot.kui"))["build"], record["commit"][:12])
            sums = dict(reversed(line.split("  ", 1)) for line in install.read("SHA256SUMS").decode().splitlines())
            self.assertEqual(sums["KUI/apps/games/zso-boot.kui"], hashlib.sha256(self.expected).hexdigest())
            self.assertEqual(sums["GAMES-ZSO.md"], hashlib.sha256(self.guide).hexdigest())
            self.assertIn("release-banner.jpg", install.namelist())
            self.assertIn("RC-TEST-PLAN.md", install.namelist())
        with zipfile.ZipFile(assets / (release["artifact_prefix"] + "-source.zip")) as source:
            record = json.loads(source.read("build.json"))
            self.assertEqual((record["commit"], record["release"], record["kind"]),
                             (self.fixture.commit, TYRFING, "release-candidate"))
            self.assertEqual(source.read("source/kui-source.tar.gz"),
                             (self.fixture.source / "kui-source.tar.gz").read_bytes())
            self.assertEqual(source.read("source/kos-source.tar.gz"),
                             (self.fixture.source / "kos-source.tar.gz").read_bytes())

    def test_missing_zso_payload_refuses_install_assembly(self):
        self.path.unlink()
        with self.assertRaises(FileNotFoundError) as failure:
            self.assemble()
        self.assertEqual(Path(failure.exception.filename), self.path)
        self.assertFalse((self.fixture.dist / "release-assets").exists())

    def test_missing_zso_guide_refuses_install_assembly(self):
        guide = self.fixture.dist / "GAMES-ZSO.md"
        guide.unlink()
        with self.assertRaises(FileNotFoundError) as failure:
            self.assemble()
        self.assertEqual(Path(failure.exception.filename), guide)
        self.assertFalse((self.fixture.dist / "release-assets").exists())

    def test_rc_and_unversioned_tyrfing_remain_ineligible_for_publication(self):
        _, release = self.assemble()
        self.assertEqual(publish_release.SUPPORTED_RELEASES, {"1.7", "1.8.5", "2.0.0"})
        for version in (release["version"], "2.0"):
            with self.subTest(version=version), self.assertRaisesRegex(SystemExit, "explicitly scoped"):
                publish_release.release_tag({**release, "version": version})
        with self.assertRaisesRegex(SystemExit, "Refusing a diagnostic or candidate package"):
            publish_release.verify_assets(self.fixture.dist / "release-assets", release, self.fixture.commit)


class LzoReleaseInstallation(unittest.TestCase):
    def setUp(self):
        self.fixture = candidate.CandidateInstallation(methodName="runTest")
        self.addCleanup(self.fixture.doCleanups)
        self.fixture.setUp()
        metadata = patch("package.release_metadata", return_value=TYRFING)
        metadata.start()
        self.addCleanup(metadata.stop)
        self.path = self.fixture.sd / "apps/games/lzo-boot.kui"
        payload = bytearray(STAGE_BLOB_OFFSET + 64)
        payload[HEADER_OFFSET:HEADER_OFFSET + HEADER_BYTES] = relocation_header(64, "lzo")
        payload[STAGE_BLOB_OFFSET:STAGE_BLOB_OFFSET + 4] = b"\x09\0\x09\0"
        self.expected = envelope(payload, len(payload), self.fixture.commit[:12])
        self.path.write_bytes(self.expected)

    def test_typed_lzo_payload_is_required_and_survives_all_install_archives(self):
        bundle, release = self.fixture.assemble(False)
        self.assertEqual((bundle / "KUI/apps/games/lzo-boot.kui").read_bytes(), self.expected)
        self.assertEqual(inspect_zso(self.expected, "lzo")["manifest_magic"], "KUIRLS01")
        with self.assertRaisesRegex(ValueError, "relocation header"):
            inspect_zso(self.expected)
        assets = self.fixture.dist / "release-assets"
        with zipfile.ZipFile(assets / (release["artifact_prefix"] + "-release.zip")) as install:
            self.assertEqual(install.read("KUI/apps/games/lzo-boot.kui"), self.expected)
            for helper in ("gdi_optimize.py", "game_image_import.py", "network_rip_server.py"):
                self.assertEqual(install.read("tools/" + helper), (ROOT / "tools" / helper).read_bytes())
            for guide in ("NETWORK-RIP.md", "REPAIR-COPY.md", "CD-CAPTURE.md"):
                self.assertEqual(install.read(guide), (bundle / guide).read_bytes())
        self.fixture.assert_checksums(bundle, "SHA256SUMS")
        self.fixture.assert_checksums(assets, "SHA256SUMS.txt")

    def test_missing_lzo_refuses_normal_install_assembly(self):
        self.path.unlink()
        with self.assertRaises(FileNotFoundError) as failure:
            self.fixture.assemble(False)
        self.assertEqual(Path(failure.exception.filename), self.path)


class CsoReleaseInstallation(unittest.TestCase):
    def setUp(self):
        self.fixture = candidate.CandidateInstallation(methodName="runTest")
        self.addCleanup(self.fixture.doCleanups)
        self.fixture.setUp()
        metadata = patch("package.release_metadata", return_value=TYRFING)
        metadata.start()
        self.addCleanup(metadata.stop)
        self.path = self.fixture.sd / "apps/games/cso-boot.kui"
        payload = bytearray(STAGE_BLOB_OFFSET + 64)
        payload[HEADER_OFFSET:HEADER_OFFSET + HEADER_BYTES] = relocation_header(64, "cso")
        payload[STAGE_BLOB_OFFSET:STAGE_BLOB_OFFSET + 4] = b"\x09\0\x09\0"
        self.expected = envelope(payload, len(payload), self.fixture.commit[:12])
        self.path.write_bytes(self.expected)
        self.guides = {name.upper() + ".md": package.guide(name + ".md").encode("utf-8")
                       for name in ("games-cso", "games-chd", "ata-dma-plan")}
        for name, contents in self.guides.items():
            (self.fixture.dist / name).write_bytes(contents)

    def test_typed_cso_payload_and_exact_source_identity_survive_install_archives(self):
        bundle, release = self.fixture.assemble(False)
        self.assertEqual((bundle / "KUI/apps/games/cso-boot.kui").read_bytes(), self.expected)
        info = inspect_zso(self.expected, "cso")
        self.assertEqual(info["manifest_magic"], "KUIRCS01")
        self.assertEqual((info["transport"], info["reader"]), ("SCI", "Standard"))
        self.assertIn("raw DEFLATE", info["codec"])
        for codec in ("lz4", "lzo"):
            with self.subTest(codec=codec), self.assertRaisesRegex(ValueError, "relocation header"):
                inspect_zso(self.expected, codec)
        assets = self.fixture.dist / "release-assets"
        with zipfile.ZipFile(assets / (release["artifact_prefix"] + "-release.zip")) as install:
            self.assertEqual(install.read("KUI/apps/games/cso-boot.kui"), self.expected)
            for name, contents in self.guides.items():
                self.assertEqual((bundle / name).read_bytes(), contents)
                self.assertEqual(install.read(name), contents)
            self.assertIn(b"(GAMES-CHD.md)", install.read("GAMES-CSO.md"))
            self.assertNotIn(b"(games-chd.md)", install.read("GAMES-CSO.md"))
            record = json.loads(install.read("build.json"))
            self.assertEqual(record["commit"], self.fixture.commit)
            self.assertEqual(record["kind"], "release-candidate")
            self.assertFalse(record["hardware_tested"])
            self.assertEqual(verify(install.read("KUI/apps/games/cso-boot.kui"))["build"], record["commit"][:12])
            sums = dict(reversed(line.split("  ", 1)) for line in install.read("SHA256SUMS").decode().splitlines())
            self.assertEqual(sums["KUI/apps/games/cso-boot.kui"], hashlib.sha256(self.expected).hexdigest())
            for name, contents in self.guides.items():
                self.assertEqual(sums[name], hashlib.sha256(contents).hexdigest())
        with zipfile.ZipFile(assets / (release["artifact_prefix"] + "-source.zip")) as source:
            record = json.loads(source.read("build.json"))
            self.assertEqual((record["commit"], record["release"], record["kind"]),
                             (self.fixture.commit, TYRFING, "release-candidate"))
            for name in ("kui-source.tar.gz", "kos-source.tar.gz"):
                self.assertEqual(source.read("source/" + name), (self.fixture.source / name).read_bytes())
        self.fixture.assert_checksums(bundle, "SHA256SUMS")
        self.fixture.assert_checksums(assets, "SHA256SUMS.txt")

    def test_missing_cso_refuses_install_assembly_before_any_release_archive(self):
        self.path.unlink()
        with self.assertRaises(FileNotFoundError) as failure:
            self.fixture.assemble(False)
        self.assertEqual(Path(failure.exception.filename), self.path)
        self.assertFalse((self.fixture.dist / "release-assets").exists())

    def test_missing_compressed_or_dma_guide_refuses_install_assembly(self):
        for name, contents in self.guides.items():
            path = self.fixture.dist / name
            with self.subTest(guide=name):
                path.unlink()
                try:
                    with self.assertRaises(FileNotFoundError) as failure:
                        self.fixture.assemble(False)
                    self.assertEqual(Path(failure.exception.filename), path)
                    self.assertFalse((self.fixture.dist / "release-assets").exists())
                finally:
                    path.write_bytes(contents)


if __name__ == "__main__":
    unittest.main()
