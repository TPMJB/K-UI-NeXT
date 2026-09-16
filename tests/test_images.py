#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual FatFs adapter/probe against disposable filesystem images."""
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(*args, expected=0):
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode != expected:
        raise RuntimeError(f"{args}: expected exit {expected}, got {result.returncode}\n{result.stdout}")
    return result.stdout


def main():
    for binary in ("mkfs.fat", "mkfs.exfat", "fsck.fat", "fsck.exfat", "mcopy"):
        if not shutil.which(binary):
            raise SystemExit(f"Missing test prerequisite: {binary}")
    with tempfile.TemporaryDirectory(prefix="kui-images-") as temp:
        base = Path(temp)
        for kind in ("fat32", "exfat"):
            clean = base / f"{kind}-clean.img"
            with clean.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            if kind == "fat32":
                run("mkfs.fat", "-F", "32", str(clean))
            else:
                run("mkfs.exfat", str(clean))
            for failure in (None, "write-fail", "sync-fail", "cancel", "full"):
                image = base / f"{kind}-{failure}.img"
                shutil.copyfile(clean, image)
                args = [str(ROOT / "build/storage-image"), str(image)]
                if failure: args.append(failure)
                output = run(*args, expected=1 if failure else 0)
                if failure:
                    assert "STORAGE PASS" not in output
                    if failure == "full":
                        assert "Write failed" in output
                        run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                else:
                    assert "STORAGE PASS" in output
                    checker = "fsck.fat" if kind == "fat32" else "fsck.exfat"
                    run(checker, "-n", str(image))
                    # Run a second time to ensure existing probe data isn't overwritten.
                    run(str(ROOT / "build/storage-image"), str(image))
                    run(checker, "-n", str(image))
                    if kind == "fat32":
                        output_dir = base / "export"
                        output_dir.mkdir(exist_ok=True)
                        for name in ("storage.bin", "storage.json"):
                            run("mcopy", "-i", str(image), f"::/KUI/probes/p0001/{name}", str(output_dir / name))
                        run("python3", str(ROOT / "tools/verify_probe.py"), str(output_dir))
                print(f"PASS {kind}: {failure or 'write/reopen/verify, fsck, repeat'}", flush=True)
            # MBR translation: a selected partition plus sentinel bytes outside it.
            mbr_image = base / f"{kind}-mbr.img"
            mbr = bytearray(512)
            mbr[450] = 0x0C if kind == "fat32" else 0x07
            struct.pack_into("<II", mbr, 454, 2048, clean.stat().st_size // 512)
            mbr[510:512] = b"\x55\xaa"
            sentinel = b"\xa7" * (1024 * 1024 - 512)
            with mbr_image.open("wb") as stream, clean.open("rb") as source:
                stream.write(mbr)
                stream.write(sentinel)
                shutil.copyfileobj(source, stream)
                stream.write(b"\xc3" * 4096)
            run(str(ROOT / "build/storage-image"), str(mbr_image))
            with mbr_image.open("rb") as stream:
                assert stream.read(512) == mbr
                assert stream.read(len(sentinel)) == sentinel
                stream.seek(-4096, 2)
                assert stream.read() == b"\xc3" * 4096
            print(f"PASS {kind}: MBR partition translation and outside-range sentinels", flush=True)


if __name__ == "__main__":
    main()
