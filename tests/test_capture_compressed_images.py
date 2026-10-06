#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""On-device compressed derivatives on FAT32/exFAT; raw jobs stay authoritative."""
from pathlib import Path
import ctypes.util
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import verify_dump
import game_image_import as importer

BINARY = str(ROOT / "build/capture-image")


def run(image, *args, opts="", layout="single-data", mode=1, expected=0):
    env = dict(os.environ, KUI_TEST_OPTS=opts, KUI_TEST_DATA_MODE=str(mode), KUI_TEST_LAYOUT=layout)
    for key in ("KUI_TEST_OUTPUT_ROOT", "KUI_TEST_GAME_NAMES", "KUI_TEST_JOB"):
        env.pop(key, None)
    result = subprocess.run([BINARY, str(image), *args], env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert result.returncode == expected, (args, opts, expected, result.returncode, result.stdout[-3000:])
    return result.stdout


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def export(image, destination, **kwargs):
    destination.mkdir()
    run(image, "export", str(destination), **kwargs)
    folders = list(destination.iterdir())
    assert len(folders) == 1, folders
    return folders[0]


def tracks(folder):
    return {p.name: digest(p) for p in folder.glob("track*")}


def validate(folder, kind):
    manifest = json.loads((folder / "manifest.json").read_text())
    assert manifest["output_format"] == kind and manifest["output_file"] == f"disc.{kind}"
    assert manifest["gdi_file"] == ".capture.gdi" and (folder / ".capture.gdi").is_file()
    output = folder / manifest["output_file"]
    assert output.stat().st_size == manifest["output_bytes"]
    assert digest(output) == manifest["output_sha256"]
    assert verify_dump.verify(folder)
    if kind in ("cso", "zso"):
        assert manifest["output_data_track"] == 3
        source = (folder / "track03.bin").read_bytes()
        mode = source[15]
        offset = 16 if mode == 1 else 24
        expected = b"".join(source[start + offset:start + offset + 2048]
                            for start in range(0, len(source), 2352))
        assert manifest["output_logical_bytes"] == len(expected)
        library = os.environ.get("KUI_LZO2_LIBRARY") or ctypes.util.find_library("lzo2")
        if kind == "cso" or library:
            with tempfile.TemporaryDirectory(prefix="kui-capture-import-") as temporary:
                restored = Path(temporary) / "expanded"
                importer.import_image(output, restored, zso_codec="lzo" if kind == "zso" else "lz4",
                                      lzo_library=library)
                assert (restored / "disc.iso").read_bytes() == expected
    else:
        assert manifest["output_data_track"] == 0 and manifest["output_logical_bytes"] % 2448 == 0
    return manifest


def main():
    with tempfile.TemporaryDirectory(prefix="kui-compressed-capture-") as temporary:
        base = Path(temporary)
        for filesystem in ("fat32", "exfat"):
            clean = base / f"{filesystem}-clean.img"
            with clean.open("wb") as stream:
                stream.truncate(256 * 1024 * 1024)
            subprocess.run(["mkfs.fat", "-F", "32", str(clean)] if filesystem == "fat32" else
                           ["mkfs.exfat", str(clean)], check=True, stdout=subprocess.PIPE)
            run(clean, "seed")
            image = base / f"{filesystem}-work.img"

            def fresh():
                shutil.copyfile(clean, image)
                return image

            run(fresh(), "new")
            original = tracks(export(image, base / f"{filesystem}-raw"))
            for number, kind in enumerate(("cso", "zso", "chd"), 2):
                output = run(fresh(), "new", opts=f"crc32,noend,size,{kind}")
                assert "SAVED DATA VERIFIED" in output and f"OUTPUT_FORMAT {number}" in output
                folder = export(image, base / f"{filesystem}-{kind}")
                validate(folder, kind)
                assert tracks(folder) == original
                before = digest(image)
                verified = run(image, "verify")
                assert "WRITES 0" in verified and digest(image) == before
                assert f"OUTPUT_NAME disc.{kind}" in verified
                # Repeated completed Resume reuses the existing verified final.
                resumed = run(image, "resume")
                assert "Verifying existing" in resumed
                assert tracks(export(image, base / f"{filesystem}-{kind}-resume")) == original
                run(image, "mutate", "output")
                before = digest(image)
                failed = run(image, "verify", expected=1)
                assert "WRITES 0" in failed and digest(image) == before
                # Existing damaged derivative is preserved on Resume as well.
                preserved = export(image, base / f"{filesystem}-{kind}-damaged")
                bad_digest = digest(preserved / f"disc.{kind}")
                run(image, "resume", expected=1)
                after = export(image, base / f"{filesystem}-{kind}-preserved")
                assert digest(after / f"disc.{kind}") == bad_digest and tracks(after) == original

            # Unsupported multi-data CSO/ZSO are refused before any card writes.
            for kind in ("cso", "zso"):
                fresh()
                before = digest(image)
                refused = run(image, "new", opts=kind, layout="six-track", expected=1)
                assert "WRITES 0" in refused and digest(image) == before

            # Controlled stop in creation and in decoded readback publishes no
            # partial primary. Resume completes from untouched verified raw files.
            for fault in ("stop-export", "stop-export-verify"):
                run(fresh(), "new", fault, opts="cso", expected=3)
                stopped = export(image, base / f"{filesystem}-{fault}")
                assert not (stopped / "disc.cso").exists() and (stopped / ".capture.gdi").exists()
                source_hashes = tracks(stopped)
                run(image, "resume")
                finished = export(image, base / f"{filesystem}-{fault}-resumed")
                validate(finished, "cso")
                assert tracks(finished) == source_hashes == original

            # Both sector layouts must export exactly the cooked data bytes.
            run(fresh(), "new", opts="cso", mode=2)
            validate(export(image, base / f"{filesystem}-mode2", mode=2), "cso")
            subprocess.run(["fsck.fat" if filesystem == "fat32" else "fsck.exfat", "-n", str(image)],
                           check=True, stdout=subprocess.PIPE)
            image.unlink()
            clean.unlink()
            print(f"{filesystem}: CSO/ZSO/CHD complete, read-only Verify, preservation, cancellation and Mode2 passed")


if __name__ == "__main__":
    main()
