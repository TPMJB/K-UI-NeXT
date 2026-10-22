#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual cooked GDI exports, read-only verification and interruption recovery."""
from pathlib import Path
import json
import shutil
import subprocess
import tempfile
from test_capture_compressed_images import run, export, digest
import verify_dump


def validate(folder):
    manifest = json.loads((folder / "manifest.json").read_text())
    assert manifest["output_format"] == "gdi_2048"
    assert manifest["gdi_file"] == ".capture.gdi"
    cooked = folder / manifest["output_file"]
    assert digest(cooked) == manifest["output_sha256"]
    lines = cooked.read_text().splitlines()
    assert int(lines[0]) == len(manifest["tracks"])
    logical = 0
    for row, track in zip(lines[1:], manifest["tracks"]):
        data = track["control"] == 4
        name = f'track{track["number"]:02}.iso' if data else track["file"]
        assert row.split() == [str(track["number"]), str(track["start_fad"] - 150),
                               str(track["control"]), "2048" if data else "2352", name, "0"]
        if data:
            raw = (folder / track["file"]).read_bytes()
            expected = b"".join(raw[i + (16 if raw[i + 15] == 1 else 24):
                                    i + (16 if raw[i + 15] == 1 else 24) + 2048]
                                for i in range(0, len(raw), 2352))
            assert (folder / name).read_bytes() == expected
            logical += len(expected)
    assert manifest["output_logical_bytes"] == logical and manifest["output_data_track"] == 0
    assert verify_dump.verify(folder)
    return {p.name: digest(p) for p in folder.glob("track*")}


def main():
    with tempfile.TemporaryDirectory(prefix="kui-cooked-") as temporary:
        base = Path(temporary)
        for fs in ("fat32", "exfat"):
            clean = base / f"{fs}.img"
            with clean.open("wb") as f:
                f.truncate(96 * 1024 * 1024)
            subprocess.run(["mkfs.fat", "-F", "32", str(clean)] if fs == "fat32" else
                           ["mkfs.exfat", str(clean)], check=True, stdout=subprocess.DEVNULL)
            run(clean, "seed", layout="six-track")
            for mode in (1, 2):
                image = base / f"{fs}-{mode}.img"
                shutil.copyfile(clean, image)
                run(image, "new", opts="crc32,noend,cooked", layout="six-track", mode=mode)
                folder = export(image, base / f"{fs}-{mode}-files", layout="six-track", mode=mode)
                original = validate(folder)
                before = digest(image)
                assert "WRITES 0" in run(image, "verify", layout="six-track", mode=mode)
                assert digest(image) == before
                run(image, "resume", layout="six-track", mode=mode)
                assert validate(export(image, base / f"{fs}-{mode}-again", layout="six-track", mode=mode)) == original
                run(image, "mutate", "gdi-cooked", layout="six-track", mode=mode)
                before = digest(image)
                assert "WRITES 0" in run(image, "verify", layout="six-track", mode=mode, expected=1)
                assert digest(image) == before
                run(image, "resume", layout="six-track", mode=mode, expected=1)
                image.unlink()
            image = base / f"{fs}-stop.img"
            shutil.copyfile(clean, image)
            run(image, "new", "stop-export", opts="cooked", layout="six-track", expected=3)
            folder = export(image, base / f"{fs}-stopped", layout="six-track")
            assert not (folder / "disc.gdi").exists()
            run(image, "resume", layout="six-track")
            validate(export(image, base / f"{fs}-finished", layout="six-track"))
            image.unlink()
            image = base / f"{fs}-damaged-track.img"
            shutil.copyfile(clean, image)
            run(image, "new", opts="cooked", layout="six-track")
            run(image, "mutate", "cooked-track", layout="six-track")
            before = digest(image)
            assert "WRITES 0" in run(image, "verify", layout="six-track", expected=1)
            assert digest(image) == before
            bad = export(image, base / f"{fs}-bad-track", layout="six-track")
            bad_hash = digest(bad / "track03.iso")
            run(image, "resume", layout="six-track", expected=1)
            kept = export(image, base / f"{fs}-kept-track", layout="six-track")
            assert digest(kept / "track03.iso") == bad_hash
            image.unlink()
            print(f"PASS {fs} cooked GDI: multi-track Mode1/2, read-only Verify, preservation, stop/resume")


if __name__ == "__main__":
    main()
