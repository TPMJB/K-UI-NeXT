#!/usr/bin/env python3
"""Create the original G3 loader fixture; contains no retail game material."""
import argparse
from pathlib import Path

FILE_BYTES = 56832


def fixture_bytes():
    data = bytearray(FILE_BYTES)
    for sector in range(24):
        base = sector * 2352
        for offset in range(2352):
            data[base + offset] = ((sector * 37) ^ (offset * 13) ^
                                   (offset >> 8) ^ 0xA5) & 0xFF
        if sector < 8 or sector >= 12:
            fad = (sector if sector < 12 else sector - 12 + 45000) + 150
            minute, rem = divmod(fad, 4500)
            second, frame = divmod(rem, 75)
            data[base:base + 12] = b"\0" + b"\xff" * 10 + b"\0"
            data[base + 12:base + 16] = bytes(
                ((x // 10) * 16 + x % 10) for x in (minute, second, frame)
            ) + b"\1"
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(fixture_bytes())


if __name__ == "__main__":
    main()
