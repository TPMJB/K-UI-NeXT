#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real FAT32/exFAT Games reads, injected faults and whole-card preservation."""
from pathlib import Path
import argparse
import hashlib
import shutil
import sys
import tempfile
from test_images import run
from games_fixture import make_fixture
from zso_fixture import make_zso_fixture

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "build/games-image")
CASES = ("valid", "valid-session-size", "listing", "missing-root", "missing-track", "truncated-audio",
         "truncated-boot", "unsafe-gdi", "bad-ip", "unsupported-format",
         "connect-fail", "mount-fail", "open-fail", "read-fail", "short-read",
         "seek-fail", "close-fail", "dir-read-fail", "dir-close-fail",
         "cancel-before", "cancel-read", "cancel-list", "invalid-root",
         "invalid-path", "offset-limit", "long-root",
         "variant-pair", "variant-reverse", "variant-pages", "variant-original-only",
         "variant-converted-only", "variant-invalid", "variant-mismatch-lba",
         "variant-mismatch-count", "variant-mismatch-length", "variant-raw-data",
         "variant-ambiguous-original", "variant-ambiguous-converted", "variant-layout-only",
         "variant-open-fail", "variant-read-fail", "variant-short-read", "variant-seek-fail",
         "variant-close-fail", "variant-cancel-pair")
CASES += ("variant-chain", "variant-index-overflow", "variant-pair-read-once",
          "variant-pair-close-once", "variant-mismatch-control", "variant-missing-track",
          "variant-reverse-pair-read-once", "variant-reverse-pair-close-once")
CASES += ("variant-stat-sfn", "variant-stat-case", "variant-case", "variant-descriptor-name",
          "variant-mixed-original", "variant-all-cooked-original", "variant-many-tracks",
          "variant-nested", "variant-cache-navigation", "variant-cache-refresh",
          "variant-cache-root-change", "variant-cache-root-lru", "variant-cache-callback", "variant-cache-dir-read-once",
          "variant-cache-dir-close-once", "variant-cache-cancel-lazy")
CASES += ("variant-unicode-case", "variant-unicode-exact", "variant-sfn-alias",
          "variant-cache-pair-read-once", "variant-cache-pair-close-once",
          "variant-cache-index-read-once", "variant-list-cache-overflow",
          "variant-cache-callback-cancel")
CASES += ("format-root-extensions", "format-gdi-payload", "format-capture-payload", "format-capture-malformed", "format-cue-listing",
          "format-folder-cue", "format-folder-iso", "format-folder-ambiguous",
          "format-inspect-iso", "format-inspect-bin", "format-inspect-mode2",
          "format-cue-shared", "format-compressed", "format-inspect-cdi", "format-inspect-cdi-invalid", "format-bad-boot")
CASES += ("format-capture-zso", "format-capture-chd", "format-capture-gdi", "format-capture-cue",
          "format-capture-gdi-malformed", "format-capture-unrelated-bin")
CASES += ("format-alias-cue", "format-alias-reverse", "format-alias-gdi", "format-alias-independent",
          "format-alias-scrambled", "format-alias-media", "format-alias-layout",
          "format-alias-malformed", "format-alias-read-once", "format-alias-cancel")
ZSO_INSPECT_CASES = ("inspect-zso-sci", "inspect-zso-scif", "inspect-zso-ce", "inspect-zso-lzo")
CASES += ZSO_INSPECT_CASES
ORDERING_CASES = ("sort-cache", "sort-overflow")
CASES += ORDERING_CASES


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").digest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zso-only", action="store_true", help="Only direct-ZSO inspection and compressed-format refusals")
    parser.add_argument("--ordering-only", action="store_true", help="Only sorted listing and exact row lookup checks")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="kui-games-") as temp:
        base = Path(temp)
        fixture = base / "original-gdi"
        make_fixture(fixture)
        run(sys.executable,str(ROOT / "tools/gdi_optimize.py"),
            str(fixture / "disc.gdi"),str(base / "cooked-gdi"))
        for kind in ("fat32", "exfat"):
            clean = base / f"{kind}-clean.img"
            with clean.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            if kind == "fat32":
                run("mkfs.fat", "-F", "32", str(clean))
            else:
                run("mkfs.exfat", str(clean))
            selected_cases = ORDERING_CASES if args.ordering_only else ZSO_INSPECT_CASES + ("format-compressed",) if args.zso_only else CASES
            for case in selected_cases:
                selected_fixture = fixture
                if case in ZSO_INSPECT_CASES:
                    selected_fixture = base / "zso-inspect"
                    make_zso_fixture(selected_fixture, "zso-windows-ce" if case == "inspect-zso-ce" else "zso-lzo-gd-shift0" if case == "inspect-zso-lzo" else "zso-gd-shift0")
                image = base / f"{kind}-{case}.img"
                shutil.copyfile(clean, image)
                run(BINARY, str(image), str(selected_fixture), "seed", case)
                before = digest(image)
                output = run(BINARY, str(image), str(selected_fixture), "check", case)
                assert f"PASS Games check {case}; no active-operation writes" in output
                assert digest(image) == before, f"Games changed {kind} card image in {case}"
                if case in ZSO_INSPECT_CASES:
                    physical_bytes = (selected_fixture / "selected.zso").stat().st_size
                    assert f"physical bytes={physical_bytes}, native SCI admission={int(case in ('inspect-zso-sci', 'inspect-zso-lzo'))}, Windows CE={int(case == 'inspect-zso-ce')}" in output
                run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                image.unlink()
                print(f"PASS {kind} Games: {case}; whole-card SHA256 unchanged", flush=True)


if __name__ == "__main__":
    main()
