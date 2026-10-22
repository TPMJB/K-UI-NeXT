#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The observer ZIP must replace the native module the released Games app loads."""
import hashlib
from pathlib import Path
import re
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from package_vmu_maple_observe import write_test_archive


class ObserverInstallRouteTests(unittest.TestCase):
    def test_archive_matches_existing_frontend_route(self):
        self.check_archive_matches_existing_frontend_route(1, "vmu-maple-observer-test.zip")

    def test_silver_archive_matches_existing_frontend_route(self):
        self.check_archive_matches_existing_frontend_route(2, "vmu-silver-observer-test.zip")

    def test_silver_readonly_archive_matches_existing_frontend_route(self):
        self.check_archive_matches_existing_frontend_route(2, "vmu-silver-readonly-card-test.zip", readonly_proof=True)

    def test_silver_sd_readonly_archive_matches_existing_frontend_route(self):
        self.check_archive_matches_existing_frontend_route(2, "vmu-silver-sd-readonly-card-test.zip", sd_readonly_proof=True)

    def check_archive_matches_existing_frontend_route(self, maple_profile, expected_name, *,
                                                     readonly_proof=False, sd_readonly_proof=False):
        header = (ROOT / "include/kui/games_retail.h").read_text()
        route = re.search(r'^#define KUI_GAMES_RETAIL_PACKAGE "0:/([^"\n]+)"$',
                          header, re.MULTILINE)
        self.assertIsNotNone(route, "Native frontend package route is missing")
        destination = route.group(1)
        module = b"unique observer module bytes\0"
        report = b'{"virtual_vmu_enabled": false}\n'
        with tempfile.TemporaryDirectory() as name:
            target = write_test_archive(Path(name), module, report, maple_profile=maple_profile,
                                        readonly_proof=readonly_proof, sd_readonly_proof=sd_readonly_proof)
            self.assertEqual(target.name, expected_name)
            with zipfile.ZipFile(target) as archive:
                # Match the actual frontend contract, rather than a second
                # test-only spelling of the installation filename.
                installed = [entry for entry in archive.namelist()
                             if entry.endswith(".kui")]
                self.assertEqual(installed, [destination])
                self.assertEqual(archive.read(destination), module)
                self.assertNotIn("KUI/retail.kui", archive.namelist())
                readme = archive.read("README-FIRST.md").decode()
                self.assertIn("Back up the installed 2.0.0 `" + destination + "`",
                              readme)
                self.assertIn("Restore the backed-up 2.0.0 `" + destination + "`",
                              readme)
                self.assertIn("RAM patches Off", readme)
                self.assertNotIn("KUI/retail.kui", readme)
                for line in archive.read("SHA256SUMS.txt").decode().splitlines():
                    digest, entry = line.split("  ", 1)
                    self.assertEqual(hashlib.sha256(archive.read(entry)).hexdigest(),
                                     digest, entry)


if __name__ == "__main__":
    unittest.main()
