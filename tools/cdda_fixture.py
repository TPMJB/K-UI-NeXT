#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate an independent sector-aligned little-endian stereo PCM fixture."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import zlib

RATE = 44100
SECTOR = 2352
AMPLITUDE = 8192
SECONDS = 12


def make_fixture():
    data = bytearray()
    # Three seconds per phase, exactly 225 CDDA sectors. Soft transitions
    # let a hardware listener distinguish actual glitches from tone edges.
    segments = ((440, 0), (0, 660), (440, 660), (0, 0))
    fade = RATE * 15 // 1000
    phase_frames = RATE * 3
    for left_hz, right_hz in segments:
        for frame in range(phase_frames):
            gain = min(1.0, frame / fade, (phase_frames - 1 - frame) / fade)
            left = round(AMPLITUDE * gain * math.sin(2 * math.pi * left_hz * frame / RATE))
            right = round(AMPLITUDE * gain * math.sin(2 * math.pi * right_hz * frame / RATE))
            data += struct.pack("<hh", left, right)
    assert len(data) == SECONDS * RATE * 4 and len(data) % SECTOR == 0
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    data = make_fixture()
    raw = args.output / "stereo.raw"
    raw.write_bytes(data)
    metadata = {
        "file": raw.name, "sample_rate": RATE, "channels": 2,
        "bits_per_sample": 16, "byte_order": "little", "interleaved": True,
        "bytes": len(data), "sectors_2352": len(data) // SECTOR,
        "frames": len(data) // 4, "seconds": SECONDS,
        "sha256": hashlib.sha256(data).hexdigest(),
        "crc32": f"{zlib.crc32(data):08x}",
        "peak_amplitude": AMPLITUDE, "fade_ms": 15,
        "phases": [
            {"start_seconds": 0, "end_seconds": 3, "left_hz": 440, "right_hz": 0},
            {"start_seconds": 3, "end_seconds": 6, "left_hz": 0, "right_hz": 660},
            {"start_seconds": 6, "end_seconds": 9, "left_hz": 440, "right_hz": 660},
            {"start_seconds": 9, "end_seconds": 12, "left_hz": 0, "right_hz": 0},
        ],
    }
    (args.output / "stereo.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
