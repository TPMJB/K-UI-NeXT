#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate tiny synthetic Advanced CRC jobs, never retail/game data.

Usage: make_scan_fixtures.py OUTDIR
Copy the generated test folders under /KUI/tests/scan on the card.
The Mode 1 oracle uses independent polynomial parity generation from the
host recovery checks. Generated dump-shaped folders must stay outside Git.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location("recovery_vectors", ROOT / "tests/make_recovery_vectors.py")
_vectors = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_vectors)


def make_fixture(folder, damaged=False, sha=False):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    identity = hashlib.sha256(b"K-UI synthetic Advanced CRC fixture v1; not a retail disc").digest()
    tracks = []
    content = []
    for number, session, control, start, end, toc_end in (
        (1, 0, 4, 150, 155, 305), (2, 0, 0, 305, 309, 309),
        (3, 1, 4, 45150, 45155, 45155),
    ):
        if control == 4:
            data = b"".join(_vectors.sector(fad, number * 17 + fad % 128) for fad in range(start, end))
        else:
            data = bytes((i * 13 + 97) % 256 for i in range((end-start) * 2352))
        name = f"track{number:02d}.{'bin' if control == 4 else 'raw'}"
        track = dict(number=number, session=session, control=control, start_fad=start,
                     end_fad=end, toc_end_fad=toc_end, excluded_tail_sectors=toc_end-end,
                     file=name, bytes=len(data), crc32=f"{zlib.crc32(data):08x}")
        if sha:
            track["sha256"] = hashlib.sha256(data).hexdigest()
        tracks.append(track)
        content.append(data)
    manifest = dict(schema=1 if sha else 2, complete=True,
                    profile="gdi-raw2352-typegap150-v1", identity=identity.hex(),
                    capture_build="000000000000", title="SYNTHETIC SCAN TEST",
                    sector_bytes=2352, audio="synthetic audio; not disc data", tracks=tracks)
    if sha:
        manifest["saved_data_verified"] = True
        manifest["reference"] = "not compared"
    else:
        manifest["hashes"] = ["crc32"]
    (folder / "manifest.json").write_text(json.dumps(manifest, indent=2)+"\n")
    (folder / "disc.gdi").write_text("3\n"+"".join(
        f"{t['number']} {t['start_fad']-150} {t['control']} 2352 {t['file']} 0\n" for t in tracks))
    for slot, sequence in (("a", 1), ("b", 2)):
        record = bytearray(4096)
        record[:8] = b"KUICKP1\0"
        struct.pack_into("<IIQ", record, 8, 1, 4096, sequence)
        record[24:56] = identity
        struct.pack_into("<II", record, 56, len(tracks), 0)
        record[64:76] = b"000000000000"
        struct.pack_into("<I", record, 76, 0 if sha else 1)
        for index, track in enumerate(tracks):
            struct.pack_into("<II", record, 96+index*40, track["bytes"]//2352, int(track["crc32"], 16))
            if sha:
                record[104+index*40:136+index*40] = bytes.fromhex(track["sha256"])
        struct.pack_into("<I", record, 4092, zlib.crc32(record[:4092]))
        (folder / f"checkpoint-{slot}.bin").write_bytes(record)
    for index, (track, data) in enumerate(zip(tracks, content)):
        if damaged:
            data = bytearray(data)
            # Parity-only defect; one audio byte; one data payload byte.
            offset = 2248 if index == 0 else 2352+17 if index == 1 else 2*2352+37
            data[offset] ^= 1
        (folder / track["file"]).write_bytes(data)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("outdir", type=Path)
    args = parser.parse_args()
    make_fixture(args.outdir / "clean")
    make_fixture(args.outdir / "damaged", damaged=True)
    imported = args.outdir / "gdi-only"
    make_fixture(imported)
    for name in ("manifest.json", "checkpoint-a.bin", "checkpoint-b.bin"):
        (imported / name).unlink()
    (args.outdir / "README.txt").write_text(
        "K-UI Advanced CRC synthetic test jobs. These are not games.\n"
        "Ripper > Advanced > Advanced CRC scan > choose one test folder.\n"
        "clean: CLEAN, 10 data sectors, 4 audio sectors, 0 suspect, 0 CRC mismatches.\n"
        "damaged: ISSUES, 2 suspect data sectors, 0 unsupported, 3 CRC mismatches.\n"
        "gdi-only: STRUCTURAL ONLY; 10 data + 4 audio sectors; NO expected hashes.\n"
        "A clean GDI-only structure check does not verify the audio bytes.\n"
        "Expected suspect FADs: 150 (P/Q only), 45152 (EDC/PQ).\n"
        "The altered audio track is detected by its CRC; audio has no Mode 1 parity.\n"
        "Reports: /KUI/recovery/scan-XXXX.txt; incomplete reports use .part.\n"
        "No optical reads, repair, zero-fill or game reference claim.\n"
    )


if __name__ == "__main__":
    main()
