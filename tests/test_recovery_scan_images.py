#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Advanced CRC scanner on real FAT32/exFAT: read-only sources and failure states."""
from pathlib import Path
import importlib.util
import shutil
import tempfile
from test_images import run

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/recovery-scan-image")
_spec = importlib.util.spec_from_file_location("scan_fixtures", ROOT / "tools/make_scan_fixtures.py")
_fixture = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_fixture)


def main():
    with tempfile.TemporaryDirectory(prefix="kui-recovery-scan-") as temp:
        base = Path(temp)
        _fixture.make_fixture(base / "clean")
        _fixture.make_fixture(base / "damaged", damaged=True)
        _fixture.make_fixture(base / "sha", sha=True)
        cases = ("clean", "damaged", "sha", "repeat", "unsupported", "bad-manifest",
                 "one-checkpoint", "both-checkpoints", "conflict", "gdi-trailing", "truncated",
                 "cancel-before", "cancel-scan", "read-fail", "read-fail-cancel", "write-fail",
                 "sync-fail", "final-sync-fail", "close-fail", "rename-fail",
                 "named", "named-sha", "long-folder", "no-checkpoints", "missing-gdi", "parent",
                 "no-metadata", "multiple-gdi", "imported", "imported-quotes",
                 "imported-damaged", "imported-overlap")
        for kind in ("fat32", "exfat"):
            seed = base / f"{kind}-seed.img"
            with seed.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(seed)) if kind == "fat32" else run("mkfs.exfat", str(seed))
            for case in cases:
                image = base / f"{kind}-{case}.img"
                shutil.copyfile(seed, image)
                fixture = base / ("damaged" if case in ("damaged", "imported-damaged") else
                                  "sha" if case in ("sha", "named-sha") else "clean")
                output = run(BINARY, str(image), str(fixture), case)
                assert f"PASS Advanced CRC {case}; original files unchanged" in output
                if case not in ("write-fail", "sync-fail", "final-sync-fail"):
                    run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                image.unlink()
                print(f"PASS {kind} Advanced CRC: {case}", flush=True)


if __name__ == "__main__":
    main()
