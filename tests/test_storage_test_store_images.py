#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Persistence and torn-save recovery using the real FAT32/exFAT implementation."""
import csv
import json
from pathlib import Path
import shutil
import tempfile
from test_images import run

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="kui-test-store-") as temp:
        folder = Path(temp)
        for kind in ("fat32", "exfat"):
            clean = folder / f"{kind}-clean.img"
            with clean.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            if kind == "fat32":
                run("mkfs.fat", "-F", "32", str(clean))
            else:
                run("mkfs.exfat", str(clean))
            for scenario in ("normal", "baseline-fail", "save-fail", "start-fail"):
                image = folder / f"{kind}-{scenario}.img"
                shutil.copyfile(clean, image)
                args = [str(ROOT / "build/storage-test-store-image"), str(image), str(folder)]
                if scenario != "normal":
                    args.append(scenario)
                assert "PASS storage test store:" in run(*args)
                if scenario == "normal":
                    run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                    report = json.loads((folder / "result.json").read_text())
                    assert report["card_label"] == 'SD "A",\\test'
                    assert report["message"] == 'Quoted "pass" \\ control\n\t\x01'
                    assert report["free_bytes"] == 32_000_000_000
                    assert report["written_bytes"] == report["verified_bytes"] == 4 * 1048576
                    assert report["samples"][0]["verified"] is True
                    with (folder / "result.csv").open(newline="") as stream:
                        rows = list(csv.DictReader(stream))
                    assert len(rows) == 1 and rows[0]["card_label"] == report["card_label"]
                    assert int(rows[0]["read_us"]) == report["samples"][0]["read_us"]
                print(f"PASS {kind} storage test persistence {scenario}", flush=True)


if __name__ == "__main__":
    main()
