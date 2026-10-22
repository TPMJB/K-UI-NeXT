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
                    assert report["sci_profile"] == {
                        "timing_scope": "successful_dma_payloads",
                        "rx_dma_blocks": 4294967295, "tx_dma_blocks": 90210,
                        "polled_blocks": 3, "dma_failures": 2,
                        "profiled_rx_blocks": 17, "profiled_tx_blocks": 18,
                        "rx_setup_us": 5000000001, "rx_transfer_us": 5000000002,
                        "rx_check_us": 5000000003, "tx_setup_us": 5000000004,
                        "tx_transfer_us": 5000000005,
                    }
                    unprofiled = json.loads((folder / "unprofiled.json").read_text())
                    assert "sci_profile" not in unprofiled
                    failed = json.loads((folder / "failed-result.json").read_text())
                    assert failed["message"] == "hardware CRC failure"
                    assert failed["sci_profile"] == report["sci_profile"]
                    # Version/size and the entire payload are unchanged;
                    # only run IDs and their final checksums may differ.
                    plain = (folder / "unprofiled.bin").read_bytes()
                    profiled = (folder / "profiled.bin").read_bytes()
                    assert len(plain) == len(profiled) == 1536
                    assert plain[:16] == profiled[:16] == b"KUITEST1\x01\x00\x00\x00\x00\x06\x00\x00"
                    assert plain[20:-4] == profiled[20:-4]
                    with (folder / "result.csv").open(newline="") as stream:
                        rows = list(csv.DictReader(stream))
                    assert len(rows) == 1 and rows[0]["card_label"] == report["card_label"]
                    assert len(rows[0]) == 17
                    assert int(rows[0]["read_us"]) == report["samples"][0]["read_us"]
                print(f"PASS {kind} storage test persistence {scenario}", flush=True)


if __name__ == "__main__":
    main()
