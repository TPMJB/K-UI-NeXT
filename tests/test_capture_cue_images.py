#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""BIN/CUE preserves the accepted raw capture profile on real FAT32/exFAT."""
from pathlib import Path
import hashlib
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

BINARY = str(ROOT / "build/capture-image")


def run(image, *args, opts="cue", root=None, mode=1, expected=0):
    env = dict(os.environ, KUI_TEST_OPTS=opts, KUI_TEST_DATA_MODE=str(mode), KUI_TEST_TITLE="MDK2")
    for key in ("KUI_TEST_OUTPUT_ROOT", "KUI_TEST_GAME_NAMES", "KUI_TEST_JOB"):
        env.pop(key, None)
    if root is not None:
        env.update(KUI_TEST_OUTPUT_ROOT=root, KUI_TEST_GAME_NAMES="1")
    result = subprocess.run([BINARY, str(image), *args], env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert result.returncode == expected, (args, opts, expected, result.returncode, result.stdout)
    return result.stdout


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def export(image, destination, root=None):
    destination.mkdir()
    run(image, "export", str(destination), root=root)
    folders = list(destination.iterdir())
    assert len(folders) == 1, folders
    return folders[0]


def raw_hashes(folder):
    return {p.name: digest(p) for p in folder.glob("track*")}


def cue_manifest(folder, name="disc.cue", mode=1):
    manifest = json.loads((folder / "manifest.json").read_text())
    assert manifest["output_format"] == "bin_cue" and manifest["cue_file"] == name
    assert manifest["gdi_file"] == ".capture.gdi"
    assert not (folder / "disc.gdi").exists() and not (folder / "MDK2.gdi").exists()
    assert [t["sector_mode"] for t in manifest["tracks"]] == [mode, 0, mode, 0, 0, mode]
    assert verify_dump.verify(folder)
    cue = (folder / name).read_text()
    assert cue.count("PREGAP 00:02:00") == 3 and "INDEX 00" not in cue
    assert cue.count("REM HIGH-DENSITY AREA") == cue.count("REM SINGLE-DENSITY AREA") == 1
    return manifest


def main():
    with tempfile.TemporaryDirectory(prefix="kui-cue-capture-") as temp:
        base = Path(temp)
        for kind in ("fat32", "exfat"):
            clean = base / f"{kind}-clean.img"
            with clean.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            subprocess.run(["mkfs.fat", "-F", "32", str(clean)] if kind == "fat32" else
                           ["mkfs.exfat", str(clean)], check=True, stdout=subprocess.PIPE)
            run(clean, "seed")
            number = 0

            def fresh(label):
                nonlocal number
                number += 1
                image = base / f"{kind}-{number}-{label}.img"
                shutil.copyfile(clean, image)
                return image

            baseline = fresh("gdi")
            run(baseline, "new", opts="")
            expected_raw = raw_hashes(export(baseline, base / f"{kind}-gdi"))
            image = fresh("cue")
            output = run(image, "new")
            assert "SAVED DATA VERIFIED" in output and "OUTPUT_FORMAT 1" in output
            original = export(image, base / f"{kind}-cue")
            cue_manifest(original)
            subprocess.run([str(ROOT / "build/test-recovery-manifest"), str(original / "manifest.json")],
                           check=True, stdout=subprocess.PIPE)
            assert raw_hashes(original) == expected_raw
            before = digest(image)
            output = run(image, "verify", opts="")  # Stored choice wins over current GDI preference.
            assert "WRITES 0" in output and digest(image) == before and "OUTPUT_NAME disc.cue" in output
            subprocess.run(["fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image)],
                           check=True, stdout=subprocess.PIPE)

            # Mode2 Form1 receives its actual TRACK tag and unchanged raw bytes.
            mode2_gdi = fresh("mode2-gdi")
            run(mode2_gdi, "new", opts="", mode=2)
            mode2_raw = raw_hashes(export(mode2_gdi, base / f"{kind}-mode2-gdi"))
            mode2_cue = fresh("mode2-cue")
            run(mode2_cue, "new", mode=2)
            mode2_folder = export(mode2_cue, base / f"{kind}-mode2-cue")
            cue_manifest(mode2_folder, mode=2)
            subprocess.run([str(ROOT / "build/test-recovery-manifest"), str(mode2_folder / "manifest.json")],
                           check=True, stdout=subprocess.PIPE)
            assert raw_hashes(mode2_folder) == mode2_raw

            # Changed output preferences cannot convert an already-started GDI job.
            gdi_job = fresh("retain-gdi")
            run(gdi_job, "new", "stop-middle", opts="", expected=3)
            output = run(gdi_job, "resume", opts="cue")
            assert "OUTPUT_FORMAT 0" in output and "OUTPUT_NAME disc.gdi" in output
            assert verify_dump.verify(export(gdi_job, base / f"{kind}-retain-gdi"))

            for fault in ("stop-early", "stop-middle", "stop-late", "stop-verify",
                          "cue-write-fail", "manifest-write-fail"):
                interrupted = fresh(fault)
                run(interrupted, "new", fault, opts="cue,dma", expected=3 if fault.startswith("stop") else 1)
                output = run(interrupted, "resume", opts="dma")
                assert "OUTPUT_FORMAT 1" in output and "SAVED DATA VERIFIED" in output
                folder = export(interrupted, base / f"{kind}-{fault}")
                cue_manifest(folder)
                assert raw_hashes(folder) == expected_raw

            # CRC-only and size-only resume retain the saved mode bits; Verify stays read-only.
            crc = fresh("crc-size")
            run(crc, "new", "stop-middle", opts="cue,crc32,noend", mode=2, expected=3)
            output = run(crc, "resume", opts="crc32,noend,size", mode=2)
            assert "CAPTURED:" in output and "SAVED DATA VERIFIED" not in output
            folder = export(crc, base / f"{kind}-crc-size")
            assert cue_manifest(folder, mode=2)["schema"] == 2
            before = digest(crc)
            assert "WRITES 0" in run(crc, "verify", opts="", mode=2) and digest(crc) == before

            named = fresh("named")
            run(named, "new", root="/Games")
            cue_manifest(export(named, base / f"{kind}-named", root="/Games"), "MDK2.cue")

            for mutation in ("cue", "gdi-internal", "prefix", "manifest"):
                broken = fresh(f"tampered-{mutation}")
                shutil.copyfile(image, broken)
                run(broken, "mutate", mutation)
                before = digest(broken)
                output = run(broken, "verify", expected=1)
                assert "WRITES 0" in output and digest(broken) == before

            mixed = fresh("mixed-mode")
            output = run(mixed, "new", "mixed-mode", opts="cue,dma", expected=1)
            assert "Unsupported BIN/CUE layout: mixed data sector modes" in output
            assert "Read retry" not in output and "BAD_ATTEMPTS 0" in output
            partial = export(mixed, base / f"{kind}-mixed-mode")
            assert list(partial.glob("track*")) and not list(partial.glob("*.cue"))
            assert not (partial / "manifest.json").exists()

            # PC verification checks the primary descriptor and refuses path/selector ambiguity.
            extra = original / "unrelated.gdi"
            extra.write_text("1\n")
            try:
                verify_dump.verify(original)
            except ValueError as exc:
                assert "ambiguous" in str(exc)
            else:
                raise AssertionError("Extra primary descriptor accepted")
            extra.unlink()
            manifest_path = original / "manifest.json"
            manifest = json.loads(manifest_path.read_text())
            manifest["gdi_file"] = "../disc.gdi"
            manifest_path.write_text(json.dumps(manifest))
            try:
                verify_dump.verify(original)
            except ValueError as exc:
                assert "Unsafe GDI" in str(exc)
            else:
                raise AssertionError("Escaping internal descriptor accepted")
            print(f"PASS {kind}: BIN/CUE exact raw bytes, modes/gaps, resume/finalization faults, read-only Verify", flush=True)


if __name__ == "__main__":
    main()
