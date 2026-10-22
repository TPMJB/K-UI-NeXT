#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Repair-to-copy and reread evidence on real FAT32/exFAT fault images."""
from pathlib import Path
import hashlib
import json
import shutil
import struct
import tempfile
import zlib
from test_images import run
from make_recovery_vectors import sector

ROOT = Path(__file__).resolve().parents[1]
CASES = ("clean", "repair", "imported", "imported-quotes", "repeat", "wrong-disc", "wrong-disc-valid", "imported-wrong-disc", "layout",
         "truncated", "source-read-fail", "source-unstable", "evidence-write", "evidence-readback",
         "candidate-sync", "copy-write", "copy-readback", "copy-close", "gdi-publish", "report-publish",
         "cancel-before", "cancel-copy", "optical-fatal", "optical-retry", "optical-parity",
         "optical-disagree", "audio-unresolved", "valid-wrong-sector", "unsupported", "forensic-clean", "forensic", "forensic-unstable", "forensic-limit", "cancel-forensic")


def fixture(folder):
    folder.mkdir()
    tracks = []
    content = []
    identity = hashlib.sha256(b"gdi-raw2352-typegap150-v1" + struct.pack("<I", 3))
    for i, session, control, start, end, toc_end in ((1, 0, 4, 150, 190, 340),
                                                   (2, 0, 0, 340, 344, 344),
                                                   (3, 1, 4, 45150, 45190, 45190)):
        data = b"".join(sector(fad, fad) for fad in range(start, end)) if control == 4 else bytes(
            (i * 31 + 5) & 255 for i in range((end - start) * 2352))
        content.append(data)
        name = f"track{i:02d}.{'bin' if control == 4 else 'raw'}"
        (folder / name).write_bytes(data)
        tracks.append(dict(number=i, session=session, control=control, start_fad=start, end_fad=end,
                           toc_end_fad=toc_end, excluded_tail_sectors=toc_end-end, file=name,
                           bytes=len(data), crc32=f"{zlib.crc32(data):08x}", sha256=hashlib.sha256(data).hexdigest()))
        identity.update(struct.pack("<6I", i, control, session, start, end, toc_end))
        for fad in (start, start + (end - start - 1) // 2, end - 1):
            identity.update(struct.pack("<I", fad))
            identity.update(data[(fad-start)*2352:(fad-start+1)*2352])
    (folder / "alternate03.bin").write_bytes(b"".join(sector(fad, fad+1) for fad in range(45150, 45190)))
    manifest = dict(schema=1, complete=True, profile="gdi-raw2352-typegap150-v1", identity=identity.hexdigest(),
                    capture_build="000000000000", title="SYNTHETIC REPAIR TEST", sector_bytes=2352,
                    audio="synthetic audio; not disc data", tracks=tracks, saved_data_verified=True, reference="not compared")
    (folder / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (folder / "disc.gdi").write_text("3\n" + "".join(
        f"{t['number']} {t['start_fad']-150} {t['control']} 2352 {t['file']} 0\n" for t in tracks))
    for slot in ("a", "b"):
        (folder / f"checkpoint-{slot}.bin").write_bytes(bytes(4096))


def main():
    with tempfile.TemporaryDirectory(prefix="kui-repair-copy-") as temp:
        base = Path(temp)
        fixture(base / "fixture")
        for kind in ("fat32", "exfat"):
            seed = base / f"{kind}-seed.img"
            with seed.open("wb") as stream:
                stream.truncate(96 * 1024 * 1024)
            run("mkfs.fat", "-F", "32", str(seed)) if kind == "fat32" else run("mkfs.exfat", str(seed))
            for case in CASES:
                image = base / f"{kind}-{case}.img"
                shutil.copyfile(seed, image)
                out = run(str(ROOT / "build/repair-copy-image"), str(image), str(base / "fixture"), case)
                assert f"PASS repair copy {case}" in out
                run("fsck.fat" if kind == "fat32" else "fsck.exfat", "-n", str(image))
                image.unlink()
                print(f"PASS {kind} repair copy: {case}", flush=True)


if __name__ == "__main__":
    main()
