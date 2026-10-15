# SPDX-License-Identifier: GPL-3.0-only
"""PC receivers/converters must work from each extracted distribution root."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import package

TYRFING = {"version": "2.0-rc10", "name": 'K-UI V2.0 RC10 "Tyrfing"',
           "short_name": "K-UI V2.0 RC10", "artifact_prefix": "kui-2.0-rc10-tyrfing"}


class PackagedPcHelpers(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.dist = Path(temporary.name)
        metadata = patch("package.release_metadata", return_value=TYRFING)
        metadata.start()
        self.addCleanup(metadata.stop)

    def test_diagnostic_install_and_update_keep_exact_helpers_and_local_guides(self):
        for tree in (self.dist, self.dist / "release", self.dist / "sd-update"):
            tree.mkdir(exist_ok=True)
            package.write_game_tools(tree)
            for helper in ("gdi_optimize.py", "game_image_import.py", "network_rip_server.py"):
                self.assertEqual((tree / "tools" / helper).read_bytes(),
                                 (ROOT / "tools" / helper).read_bytes())
            for guide in ("NETWORK-RIP.md", "REPAIR-COPY.md", "CD-CAPTURE.md", "GAMES-FORMATS.md",
                          "CAPTURE-FORMAT.md", "RIPPER-CONTROLS.md", "GDI-2048-TEST.md"):
                self.assertTrue((tree / guide).is_file(), str(tree / guide))
            ripper = (tree / "RIPPER-CONTROLS.md").read_text()
            for guide in ("NETWORK-RIP.md", "REPAIR-COPY.md", "CD-CAPTURE.md"):
                self.assertIn("(" + guide + ")", ripper)
            self.assertIn("tools/network_rip_server.py", (tree / "NETWORK-RIP.md").read_text())

    def test_update_receiver_is_self_contained_and_runs_from_package_root(self):
        update = self.dist / "sd-update"
        update.mkdir()
        package.write_game_tools(update)
        result = subprocess.run([sys.executable, "tools/network_rip_server.py", "--help"],
                                cwd=update, capture_output=True, text=True, check=True)
        self.assertIn("init", result.stdout)
        self.assertIn("serve", result.stdout)
        self.assertFalse(list(update.glob("*.key")))
        self.assertFalse(list(update.glob("*.cfg")))


if __name__ == "__main__":
    unittest.main()
